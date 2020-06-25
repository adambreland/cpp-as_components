#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>         // <cstdlib> does not define setenv. 
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "fcgi_si.h"

// Key:
// BAZEL DEPENDENCY  This marks a feature which is provided by the Bazel
//                   testing run-time environment. 

namespace fcgi_si_test {

// Create a temporary file in the temporary directory offered by Bazel.
// The descriptor value is written to the int pointer to by descriptor_ptr.
//
// BAZEL DEPENDENCY: TEST_TMPDIR environment variable.
void CreateBazelTemporaryFile(int* descriptor_ptr)
{
  static const char* tmpdir_ptr {std::getenv("TEST_TMPDIR")};
  if(!tmpdir_ptr)
    FAIL() << "The directory for temporary files supplied by Bazel is missing.";
  std::string temp_template {tmpdir_ptr};
  temp_template += "/fcgi_si_TEST_XXXXXX";
  std::unique_ptr<char []> char_array_uptr {new char[temp_template.size() + 1]};
    // Initialize the new character array.
  std::memcpy(char_array_uptr.get(), temp_template.data(), 
    temp_template.size() + 1);
  int temp_descriptor {mkstemp(char_array_uptr.get())};
  if(temp_descriptor < 0)
    FAIL() << "An error occurred while trying to create a temporary file." <<
      '\n' << strerror(errno);
  if(unlink(char_array_uptr.get()) < 0)
  {
    std::string errno_message {strerror(errno)};
    close(temp_descriptor);
    FAIL() << "The temporary file could not be unlinked." << '\n' <<
      errno_message;
  }
  *descriptor_ptr = temp_descriptor;
}

bool PrepareTemporaryFile(int descriptor)
{
  // Truncate the temporary file and seek to the beginning.
  if(ftruncate(descriptor, 0) < 0)
  {
    ADD_FAILURE() << "A call to ftruncate failed.";
    return false;
  }
  off_t lseek_return {lseek(descriptor, 0, SEEK_SET)};
  if(lseek_return < 0)
  {
    ADD_FAILURE() << "A call to lseek failed.";
    return false;
  }
  return true;
}

// Utility functions and testing of those functions for testing fcgi_si. 

//    A utility function used for testing. ExtractContent reads a file which
// contains a sequence of FastCGI records. These records are assumed to be
// from a single, complete record sequence. (Multiple records may be present in
// a sequence when it is associated with a stream record type from the FastCGI
// protocol.) Two operations are performed.
//
//    First, several error checks are performed.
// 1) Each header is validated for type and request identifer. Header
//    errors terminate sequence processing.
// 2) The actual number of bytes present for each section of a record is
//    compared to the expected number. Logcially, incomplete sections may only
//    occur when the end of the file is reached.
// 3) The total length of each record is verified to be a multiple of eight
//    bytes.
//
//    Second, the content byte sequence formed from the concatenation of
// the record content sections is constructed and returned.
//
// Parameters:
// fd: The file descriptor of the file to be read.
// type: The expected FastCGI record type of the record sequence.
// id: The expected FastCGI request identifier of each record in the sequence.
//
// Preconditions:
// 1) The file offset of fd is assumed to be at the start of the record
//    sequence.
// 2) It is assumed that no other data is present in the file.
// 3) Only EINTR is handled when fd is read. (Other errors cause function
//    return with a false value for the first boolean variable of the returned
//    tuple.)
//
// Effects:
// 1) Meaning of returned tuple elements.
//       Access: std::get<0>; Type: bool; True if no unrecoverable errors
//    were encountered when the file was read. False otherwise. The values of
//    the other members of the tuple are unspecified when this member is false.
//       Access: std::get<1>; Type: bool; True if neither a FastCGI type error
//    nor an identifier error was present and no incomplete record section was
//    present. False otherwise.
//       Access: std::get<2>; Type: bool; If no header errors or incomplete
//    section occurred while reading the sequence, this flag indicates if the
//    sequence was terminated by a record with zero content length (true) or
//    not (false). If header errors or incomplete section occurred, the flag is
//    false.
//       Access: std::get<3>; Type: bool; If no read errors were present
//    and no header or incomplete section errors were present, this flag is
//    true if no records were present or if all processed records had a total
//    record length which was a multiple of eight. The flag is false if header
//    or incomplete section errors were present or if a record was present
//    whose total length was not a multiple of eight bytes.
//       Access: std::get<4>; Type: std::vector<uint8_t>; The extracted
//    content of the records processed up to:
//    a) the point of error (such as the end of a partial record)
//    b) a record with a zero content length
//    c) the end of the file.
std::tuple<bool, bool, bool, bool, std::vector<uint8_t>>
ExtractContent(int fd, fcgi_si::FCGIType type, uint16_t id)
{
  constexpr uint16_t buffer_size {1 << 10};
  uint8_t byte_buffer[buffer_size];

  uint32_t local_offset {0};
  ssize_t number_bytes_read {0};
  uint8_t local_header[fcgi_si::FCGI_HEADER_LEN];
  int header_bytes_read {0};
  std::vector<uint8_t> content_bytes {};
  uint16_t FCGI_id {};
  uint16_t content_length {0};
  uint16_t content_bytes_read {0};
  uint8_t padding_length {0};
  uint8_t padding_bytes_read {0};
  bool read_error {false};
  bool header_error {false};
  bool sequence_terminated {false};
  bool aligned {true};
  int state {0};

  while((number_bytes_read = read(fd, byte_buffer, buffer_size)))
  {
    if(number_bytes_read == -1)
    {
      if(errno == EINTR)
        continue;
      else
      {
        read_error = true;
        break;
      }
    }

    local_offset = 0;
    while(local_offset < number_bytes_read)
    {
      // Note that, below, the condition "section_bytes_read < expected_amount"
      // implies that local_offset == number_bytes_read.
      switch(state) {
        case 0 : {
          if(header_bytes_read < fcgi_si::FCGI_HEADER_LEN)
          {
            // Safe narrowing as this can never exceed FCGI_HEADER_LEN.
            int header_bytes_to_copy(std::min<ssize_t>(fcgi_si::FCGI_HEADER_LEN
              - header_bytes_read, number_bytes_read - local_offset));
            std::memcpy((void*)(local_header + header_bytes_read),
              (void*)(byte_buffer + local_offset), header_bytes_to_copy);
            local_offset += header_bytes_to_copy;
            header_bytes_read += header_bytes_to_copy;
            if(header_bytes_read < fcgi_si::FCGI_HEADER_LEN)
              break;
          }
          // The header is complete and there are some bytes left to process.

          // Extract header information.
          (FCGI_id = local_header[fcgi_si::kHeaderRequestIDB1Index]) <<= 8; 
            // One byte.
          FCGI_id += local_header[fcgi_si::kHeaderRequestIDB0Index];
          (content_length = local_header[fcgi_si::kHeaderContentLengthB1Index]) 
            <<= 8;
          content_length += local_header[fcgi_si::kHeaderContentLengthB0Index];
          padding_length = local_header[fcgi_si::kHeaderPaddingLengthIndex];
          if((content_length + padding_length) % 8 != 0)
            aligned = false;
          // Verify header information.
          if(static_cast<fcgi_si::FCGIType>(
             local_header[fcgi_si::kHeaderTypeIndex]) != type 
             || (FCGI_id != id))
          {
            header_error = true;
            break;
          }
          if(content_length == 0)
          {
            sequence_terminated = true;
            break;
          }
          // Set or reset state.
          header_bytes_read = 0;
          state++;
          if(local_offset == number_bytes_read)
            break;
          // Fall through to start processing content.
        }
        case 1 : {
          if(content_bytes_read < content_length)
          {
            // Safe narrowing as this can never exceed content_length.
            uint16_t content_bytes_to_copy(std::min<ssize_t>(content_length
              - content_bytes_read, number_bytes_read - local_offset));
            content_bytes.insert(content_bytes.end(), byte_buffer + local_offset,
              byte_buffer + local_offset + content_bytes_to_copy);
            local_offset += content_bytes_to_copy;
            content_bytes_read += content_bytes_to_copy;
            if(content_bytes_read < content_length)
              break;
          }
          // Set or reset state.
          content_bytes_read = 0;
          state++;
          if(local_offset == number_bytes_read)
            break;
          // Fall through to start processing padding.
        }
        case 2 : {
          if(padding_bytes_read < padding_length)
          {
            // Safe narrowing as this can never exceed padding_length.
            uint8_t padding_bytes_to_process(std::min<ssize_t>(padding_length
              - padding_bytes_read, number_bytes_read - local_offset));
            local_offset += padding_bytes_to_process;
            padding_bytes_read += padding_bytes_to_process;
            if(padding_bytes_read < padding_length)
              break;
          }
          padding_bytes_read = 0;
          state = 0;
        }
      }
      if(read_error || header_error || sequence_terminated)
        break;
    }
    if(read_error || header_error || sequence_terminated)
      break;
  }
  // Check for incomplete record sections.
  // Note that, when no error is present and the sequence wasn't terminated
  // by a record with a zero content length, state represents a section which
  // is either incomplete or never began. It is expected that the sequence
  // ends with a header section that "never began."
  bool section_error {false};
  if(!(read_error || header_error || sequence_terminated))
  {
    switch(state) {
      case 0 : {
        if((0 < header_bytes_read) && 
           (header_bytes_read < fcgi_si::FCGI_HEADER_LEN))
          section_error = true;
        break;
      }
      case 1 : {
        if(content_bytes_read != content_length)
          section_error = true;
        break;
      }
      case 2 : {
        if(padding_bytes_read != padding_length)
          section_error = true;
        break;
      }
    }
  }

  return std::make_tuple(
    !read_error,
    !(header_error || section_error),
    sequence_terminated,
    (header_error || section_error) ? false : aligned,
    content_bytes
  );
}

} // namespace fcgi_si_test

TEST(Utility, ExtractContent)
{
  // Testing explanation
  // Examined properties:
  //  1) Content byte sequence value.
  //  2) Value of FastCGI request identifier (0, 1, small but larger than 1,
  //     and the maximum value 2^16 - 1 == uint16_t(-1)).
  //  3) Presence or absence of unaligned records.
  //  4) Record type: discrete or stream.
  //  5) For stream types, presence and absence of a terminal record with a
  //     content length of zero.
  //  6) Presence or absence of padding.
  //  7) Presence or absence of an unrecoverable read error (such as a bad
  //     file descriptor).
  //  8) Presence or absence of a header error. Two errors categories: type
  //     and FastCGI request identifier.
  //  9) Presence or absence of an incomplete section. Three sections produce
  //     three error categories.
  //
  // Test cases:
  //  1) Small file descriptor value, single header with a zero content length
  //     and no padding. The FastCGI request identifier value is one.
  //     (Equivalent to an empty record stream termination.)
  //  2) Small file descriptor value, single record with non-zero content
  //     length, no padding, and no terminal empty record. The FastCGI
  //     request identifier value is the largest possible value.
  //     (Special discrete record - FCGI_BEGIN_REQUEST.)
  //  3) As in 2, but with an unaligned record and a FastCGI request identifier
  //     value of zero.
  //  4) As in 2, but with padding and a FastCGI request identifier value of
  //     10. (Regular discrete record.)
  //  5) Small file descriptor value, a record with non-zero content length,
  //     padding, and a terminal empty record. The FastCGI request identifier
  //     value is ten. (A single-record, terminated stream.)
  //  6) Small file descriptor value, multiple records with non-zero content
  //     lengths and padding as necessary to reach a multiple of eight. Not
  //     terminated. The FastCGI request identifier value is one.
  //     (A non-terminated stream with multiple records.)
  //  7) As in 5, but terminated and the FastCGI request identifier value is
  //     one. (A typical, multi-record stream sequence.)
  // Note: The FastCGI request identifier value is one for all remaining cases.
  // Note: The remaining cases test function response to erroneous input.
  //  8) A bad file descriptor as an unrecoverable read error.
  //  9) As in 6, but with a header type error in the middle.
  // 10) As in 6, but with a header FastCGI request identifier error in the
  //     middle.
  // 11) A header with a non-zero content length and non-zero padding but
  //     no more data. A small file descriptor value. (An incomplete record.)
  // 12) A small file descriptor value and a sequence of records with non-zero
  //     content lengths and with padding. The sequence ends with a header with
  //     a non-zero content length and padding but no additional data.
  // 13) A small file descriptor value and a sequence of records with non-zero
  //     content lengths and with padding. The sequence ends with a header that
  //     is not complete.
  // 14) As in 11, but with a final record for which the content has a length
  //     that is less than the content length given in the final header. No
  //     additional data is present.
  // 15) As in 11, but with a final record whose padding has a length that is
  //     less than the padding length given in the final header. No additional
  //     data is present.
  //
  // Modules which testing depends on:
  // 1) fcgi_si::PopulateHeader
  //
  // Other modules whose testing depends on this module:
  // 1) fcgi_si::EncodeNameValuePairs
  // 2) fcgi_si::PartitionByteSequence

  // Create a temporary file for use during this test.
  // BAZEL DEPENDENCY
  int temp_fd {};
  fcgi_si_test::CreateBazelTemporaryFile(&temp_fd);

  bool case_single_iteration {true};

  // Case 1: Small file descriptor value, a single header with zero content
  // length and no padding.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;
    
    uint8_t local_header[fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(local_header, fcgi_si::FCGIType::kFCGI_DATA,
      1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)local_header,
      fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_TRUE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {}), std::get<4>(extract_content_result))
        << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 2: Small file descriptor value, single record with non-zero content
  // length, no padding, and no terminal empty record.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;
    
    // Populate an FCGI_BEGIN_REQUEST record.
    uint8_t record[2*fcgi_si::FCGI_HEADER_LEN] = {};
    fcgi_si::PopulateHeader(record,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, uint16_t(-1), fcgi_si::FCGI_HEADER_LEN,
      0);
    // The second set of eight bytes (FCGI_HEADER_LEN) is zero except for
    // the low-order byte of the role.
    record[fcgi_si::FCGI_HEADER_LEN + fcgi_si::kBeginRequestRoleB0Index]
      = fcgi_si::FCGI_RESPONDER;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      2*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
    if(write_return < 2*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, uint16_t(-1))};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {0,1,0,0,0,0,0,0}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 3: As in 2, but with an unaligned record.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;
    
    // Populate an FCGI_BEGIN_REQUEST record.
    uint8_t record[fcgi_si::FCGI_HEADER_LEN + 4] = {};
    fcgi_si::PopulateHeader(record,
      fcgi_si::FCGIType::kFCGI_PARAMS, 0, 4,
      0);
    // The second set of eight bytes (FCGI_HEADER_LEN) is zero except for
    // the low-order byte of the role.
    record[fcgi_si::FCGI_HEADER_LEN]     = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1] = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 2] = 'a';
    record[fcgi_si::FCGI_HEADER_LEN + 3] = 'b';
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      fcgi_si::FCGI_HEADER_LEN + 4)) == -1 && errno == EINTR)
    if(write_return < (fcgi_si::FCGI_HEADER_LEN + 4))
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_PARAMS, 0)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,1,'a','b'}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 4: As in 2, but with padding. (Regular discrete record.)
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;
    
    uint8_t record[2*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 10, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]     = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1] = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2] = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3] = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4] = 5;
    // Padding was uninitialized.
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      2*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 2*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 10)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1, 2, 3, 4, 5}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 5: Small file descriptor value, a record with non-zero content
  // length, padding, and a terminal empty record. (A single-record, terminated
  // stream.)
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[3*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 10, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]     = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1] = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2] = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3] = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4] = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 10, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      3*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 3*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 10)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_TRUE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1, 2, 3, 4, 5}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 6: Small file descriptor value, multiple records with non-zero
  // content lengths and padding as necessary to reach a multiple of eight. Not
  // terminated.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;
    
    uint8_t record[6*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      6*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 6*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 7: As in 5, but terminated. (A typical, multi-record stream sequence.)
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[7*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    fcgi_si::PopulateHeader(record + 6*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_TRUE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 8: A bad file descriptor as an unrecoverable read error.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;
    // A file descriptor which is not allocated is generated by calling
    // dup on the temporary file and adding 1000. It is assumed that no
    // file descriptor will be allocated with this value.
    int file_descriptor_limit {dup(temp_fd)};
    if(file_descriptor_limit == -1)
    {
      ADD_FAILURE() << "A call to dup failed.";
      break;
    }
    auto result {fcgi_si_test::ExtractContent(file_descriptor_limit + 1000,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, 1)};
    EXPECT_FALSE(std::get<0>(result));
    close(file_descriptor_limit);
  }

  // Case 9: As in 6, but with a header type error in the middle.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[7*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_PARAMS, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    fcgi_si::PopulateHeader(record + 6*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 10: As in 6, but with a header FastCGI request identifier error in the
  // middle.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[7*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 2, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    fcgi_si::PopulateHeader(record + 6*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 11: A header with a non-zero content length and non-zero padding, but
  // no more data. (An incomplete record.) A small file descriptor value.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_PARAMS, 1, 50, 6);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_PARAMS, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 12: A small file descriptor value and a sequence of records with
  // non-zero content lengths and with padding. The sequence ends with a header
  // with a non-zero content length and padding but no more data.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[7*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    fcgi_si::PopulateHeader(record + 6*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 38, 2);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 13: A small file descriptor value and a sequence of records with
  // non-zero content lengths and with padding. The sequence ends with a header
  // that is not complete.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[6*fcgi_si::FCGI_HEADER_LEN + 3];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    // Add values for the incomplete header.
    record[6*fcgi_si::FCGI_HEADER_LEN + fcgi_si::kHeaderVersionIndex]     =
      fcgi_si::FCGI_VERSION_1;
    record[6*fcgi_si::FCGI_HEADER_LEN + fcgi_si::kHeaderTypeIndex]        =
      static_cast<uint8_t>(fcgi_si::FCGIType::kFCGI_DATA);
    record[6*fcgi_si::FCGI_HEADER_LEN + fcgi_si::kHeaderRequestIDB1Index] =
      0;
    ssize_t write_return {write(temp_fd, (void*)record,
      6*fcgi_si::FCGI_HEADER_LEN + 3)};
    if(write_return < (6*fcgi_si::FCGI_HEADER_LEN + 3))
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 14:  As in 11, but with a final record for which the content has a
  // length that is less than the content length given in the final header. No
  // additional data is present.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[7*fcgi_si::FCGI_HEADER_LEN + 1];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    fcgi_si::PopulateHeader(record + 6*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 50, 6);
    record[7*fcgi_si::FCGI_HEADER_LEN]     = 16;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN + 1)) == -1 && errno == EINTR)
      continue;
    if(write_return < (7*fcgi_si::FCGI_HEADER_LEN + 1))
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 16}),
        std::get<4>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 15: As in 11, but with a final record whose padding has a length that
  // is less than the padding length given in the final header. No additional
  // data is present.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    if(!fcgi_si_test::PrepareTemporaryFile(temp_fd))
      break;

    uint8_t record[7*fcgi_si::FCGI_HEADER_LEN + 5];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]       = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1]   = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2]   = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3]   = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 2*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[3*fcgi_si::FCGI_HEADER_LEN]     = 6;
    record[3*fcgi_si::FCGI_HEADER_LEN + 1] = 7;
    record[3*fcgi_si::FCGI_HEADER_LEN + 2] = 8;
    record[3*fcgi_si::FCGI_HEADER_LEN + 3] = 9;
    record[3*fcgi_si::FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    fcgi_si::PopulateHeader(record + 4*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[5*fcgi_si::FCGI_HEADER_LEN]     = 11;
    record[5*fcgi_si::FCGI_HEADER_LEN + 1] = 12;
    record[5*fcgi_si::FCGI_HEADER_LEN + 2] = 13;
    record[5*fcgi_si::FCGI_HEADER_LEN + 3] = 14;
    record[5*fcgi_si::FCGI_HEADER_LEN + 4] = 15;
    fcgi_si::PopulateHeader(record + 6*fcgi_si::FCGI_HEADER_LEN,
      fcgi_si::FCGIType::kFCGI_DATA, 1, 5, 3);
    record[7*fcgi_si::FCGI_HEADER_LEN]     = 16;
    record[7*fcgi_si::FCGI_HEADER_LEN + 1] = 17;
    record[7*fcgi_si::FCGI_HEADER_LEN + 2] = 18;
    record[7*fcgi_si::FCGI_HEADER_LEN + 3] = 19;
    record[7*fcgi_si::FCGI_HEADER_LEN + 4] = 20;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN + 5)) == -1 && errno == EINTR)
      continue;
    if(write_return < (7*fcgi_si::FCGI_HEADER_LEN + 5))
    {
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20}), std::get<4>(extract_content_result))
        << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  }
  close(temp_fd);
}

// fcgi_si content testing start (utility.h)

TEST(Utility, EncodeFourByteLength)
{
  // Testing explanation:
  // Examined properties:
  // 1) Positive length greater than or equal to 128.
  // 2) Values less than 128, including negative values.
  // 2) The type of the iterator used in the instantiation of the template.
  //
  // The following cases are tested:
  // 1) A random value within the acceptable values of std::int_fast32_t.
  // 2) A random value as above, but using a non-pointer iterator.
  // 3) Minimum value: 128.
  // 4) A value which requires two bytes to encode: 256.
  // 5) A value which requires three bytes to encode: 1UL << 16.
  // 6) One less than the maximum value.
  // 7) The maximum value.
  // 8) A value less than 128 and larger than zero: 1.
  // 9) Zero.
  // 10) -1.
  //
  // For some of the cases, when sizeof(uint8_t) == sizeof(unsigned char),
  // an array of unsigned char variables is used in addition to an array of
  // uint8_t variables. This is done to further test the parametric nature
  // of the function in the case that uint8_t is not an alias to
  // unsigned char.
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module:
  // 1) fcgi_si::ExtractFourByteLength

  uint8_t header_array[4] = {};
  unsigned char char_header_array[4] = {};

  // Case 1: Random value: 2,128,547
  fcgi_si::EncodeFourByteLength(2128547, header_array);
  EXPECT_EQ(128, header_array[0]);
  EXPECT_EQ(32, header_array[1]);
  EXPECT_EQ(122, header_array[2]);
  EXPECT_EQ(163, header_array[3]);

  // Case 2: Random value with back_insert_iterator.
  std::vector<uint8_t> byte_seq {};
  fcgi_si::EncodeFourByteLength(2128547, std::back_inserter(byte_seq));
  EXPECT_EQ(128, byte_seq[0]);
  EXPECT_EQ(32, byte_seq[1]);
  EXPECT_EQ(122, byte_seq[2]);
  EXPECT_EQ(163, byte_seq[3]);

  // Case 3: Minimum value, 128.
  fcgi_si::EncodeFourByteLength(128, header_array);
  EXPECT_EQ(128, header_array[0]);
  EXPECT_EQ(0, header_array[1]);
  EXPECT_EQ(0, header_array[2]);
  EXPECT_EQ(128, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    fcgi_si::EncodeFourByteLength(128, char_header_array);
    EXPECT_EQ(128, char_header_array[0]);
    EXPECT_EQ(0, char_header_array[1]);
    EXPECT_EQ(0, char_header_array[2]);
    EXPECT_EQ(128, char_header_array[3]);
  }

  // Case 4: Requires two bytes.
  fcgi_si::EncodeFourByteLength(256, header_array);
  EXPECT_EQ(128, header_array[0]);
  EXPECT_EQ(0, header_array[1]);
  EXPECT_EQ(1, header_array[2]);
  EXPECT_EQ(0, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    fcgi_si::EncodeFourByteLength(256, char_header_array);
    EXPECT_EQ(128, char_header_array[0]);
    EXPECT_EQ(0, char_header_array[1]);
    EXPECT_EQ(1, char_header_array[2]);
    EXPECT_EQ(0, char_header_array[3]);
  }

  // Case 5: Requires three bytes.
  fcgi_si::EncodeFourByteLength(1UL << 16, header_array);
  EXPECT_EQ(128, header_array[0]);
  EXPECT_EQ(1, header_array[1]);
  EXPECT_EQ(0, header_array[2]);
  EXPECT_EQ(0, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    fcgi_si::EncodeFourByteLength(1UL << 16, char_header_array);
    EXPECT_EQ(128, char_header_array[0]);
    EXPECT_EQ(1, char_header_array[1]);
    EXPECT_EQ(0, char_header_array[2]);
    EXPECT_EQ(0, char_header_array[3]);
  }

  // Case 6: Maximum value less one.
  fcgi_si::EncodeFourByteLength((1UL << 31) - 1 - 1, header_array);
  EXPECT_EQ(255, header_array[0]);
  EXPECT_EQ(255, header_array[1]);
  EXPECT_EQ(255, header_array[2]);
  EXPECT_EQ(254, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    fcgi_si::EncodeFourByteLength((1UL << 31) - 1 - 1, char_header_array);
    EXPECT_EQ(255, char_header_array[0]);
    EXPECT_EQ(255, char_header_array[1]);
    EXPECT_EQ(255, char_header_array[2]);
    EXPECT_EQ(254, char_header_array[3]);
  }

  // Case 7: Maximum value
  fcgi_si::EncodeFourByteLength((1UL << 31) - 1, header_array);
  EXPECT_EQ(255, header_array[0]);
  EXPECT_EQ(255, header_array[1]);
  EXPECT_EQ(255, header_array[2]);
  EXPECT_EQ(255, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    fcgi_si::EncodeFourByteLength((1UL << 31) - 1, char_header_array);
    EXPECT_EQ(255, char_header_array[0]);
    EXPECT_EQ(255, char_header_array[1]);
    EXPECT_EQ(255, char_header_array[2]);
    EXPECT_EQ(255, char_header_array[3]);
  }

  // Case 8: 1
  EXPECT_THROW((fcgi_si::EncodeFourByteLength(1, char_header_array)),
    std::invalid_argument);

  // Case 9: 0
  EXPECT_THROW((fcgi_si::EncodeFourByteLength(0, char_header_array)),
    std::invalid_argument);

  // Case 10: -1
  EXPECT_THROW((fcgi_si::EncodeFourByteLength(-1, char_header_array)),
    std::invalid_argument);
}

TEST(Utility, ExtractFourByteLength)
{
  // Testing explanation:
  // Examined properties:
  // 1) Value and byte length of the argument byte sequence.
  //
  // Cases:
  // 1) A random value.
  // 2) The minimum value, 128.
  // 3) A value which requires two bytes, 256.
  // 4) A value which requires three bytes, 1UL << 16.
  // 5) One less than the maximum value.
  // 6) The maximum value, (1UL << 31) - 1.
  //
  // Modules which testing depends on:
  // 1) fcgi_si::EncodeFourByteLength
  //
  // Other modules whose testing depends on this module: none.

  uint8_t seq[4] = {};
  uint32_t length {};

  // Case 1: Random value.
  fcgi_si::EncodeFourByteLength(2128547, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(2128547, length);

  // Case 2: Minimum length.
  fcgi_si::EncodeFourByteLength(128, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(128, length);

  // Case 3: Requires two bytes.
  fcgi_si::EncodeFourByteLength(256, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(256, length);

  // Case 4: Requires three bytes.
  fcgi_si::EncodeFourByteLength(1UL << 16, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(1UL << 16, length);

  // Case 5: Maximum value less one.
  fcgi_si::EncodeFourByteLength((1UL << 31) - 1 - 1, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ((1UL << 31) - 1 - 1, length);

  // Case 6: Maximum value.
  fcgi_si::EncodeFourByteLength((1UL << 31) - 1, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ((1UL << 31) - 1, length);
}

TEST(Utility, PopulateHeader)
{
  // Testing explanation
  // Examined properties:
  // 1) type value (each of the 11 types).
  // 2) FCGI_id value (0, 1, larger than 1 but less than the maximum,
  //    the maximum value).
  // 3) content_length value (0, 1, larger than 1 but less than the maximum,
  //    the maximum value).
  // 4) padding_length value (0, 1, larger than 1 but less than the maximum,
  //    the maximum value).
  //
  // Test cases:
  //  1) type:           fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST
  //     FCGI_id:        0
  //     content_length: 0
  //     padding_length: 0
  //  2) type:           fcgi_si::FCGIType::kFCGI_ABORT_REQUEST
  //     FCGI_id:        1
  //     content_length: 1
  //     padding_length: 1
  //  3) type:           fcgi_si::FCGIType::kFCGI_END_REQUEST
  //     FCGI_id:        10
  //     content_length: 10
  //     padding_length: 10
  //  4) type:           fcgi_si::FCGIType::kFCGI_PARAMS
  //     FCGI_id:        2^16 - 1 (which is equal to uint16_t(-1))
  //     content_length: 2^16 - 1
  //     padding_length: 255 (which is equal to uint8_t(-1))
  //  5) type:           fcgi_si::FCGIType::kFCGI_STDIN
  //     FCGI_id:        1
  //     content_length: 1000
  //     padding_length: 0
  //  6) type:           fcgi_si::FCGIType::kFCGI_STDOUT
  //     FCGI_id:        1
  //     content_length: 250
  //     padding_length: 2
  //  7) type:           fcgi_si::FCGIType::kFCGI_STDERR
  //     FCGI_id:        1
  //     content_length: 2
  //     padding_length: 6
  //  8) type:           fcgi_si::FCGIType::kFCGI_DATA
  //     FCGI_id:        2^16 - 1
  //     content_length: 2^16 - 1
  //     padding_length: 7
  //  9) type:           fcgi_si::FCGIType::kFCGI_GET_VALUES
  //     FCGI_id:        0
  //     content_length: 100
  //     padding_length: 4
  // 10) type:           fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT
  //     FCGI_id:        0
  //     content_length: 100
  //     padding_length: 0
  // 11) type:           fcgi_si::FCGIType::kFCGI_UNKNOWN_TYPE
  //     FCGI_id:        1
  //     content_length: 8
  //     padding_length: 8
  //
  // Modules which testing depends on:
  //
  // Other modules whose testing depends on this module:
  // 1) fcgi_si_test::ExtractContent

  std::vector<uint8_t> local_header(fcgi_si::FCGI_HEADER_LEN);
  std::vector<uint8_t> expected_result(fcgi_si::FCGI_HEADER_LEN);

  auto PopulateHeaderTester = [&local_header, &expected_result](
    std::string message,
    fcgi_si::FCGIType type,
    uint16_t FCGI_id,
    uint16_t content_length,
    uint8_t padding_length
  )->void
  {
    fcgi_si::PopulateHeader(local_header.data(), type, FCGI_id, content_length,
      padding_length);

    expected_result[0] = fcgi_si::FCGI_VERSION_1;
    expected_result[1] = static_cast<uint8_t>(type);
    expected_result[2] = (FCGI_id >> 8);
    expected_result[3] = FCGI_id;        // implicit truncation.
    expected_result[4] = (content_length >> 8);
    expected_result[5] = content_length; // implicit truncation.
    expected_result[6] = padding_length;
    expected_result[7] = 0;

    EXPECT_EQ(local_header, expected_result) << message;
  };

  // Case 1
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST};
    uint16_t FCGI_id {0};
    uint16_t content_length {0};
    uint8_t padding_length {0};

    std::string message {"Case 1, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 2
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_ABORT_REQUEST};
    uint16_t FCGI_id {1};
    uint16_t content_length {1};
    uint8_t padding_length {1};

    std::string message {"Case 2, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 3
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_END_REQUEST};
    uint16_t FCGI_id {10};
    uint16_t content_length {10};
    uint8_t padding_length {10};

    std::string message {"Case 3, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 4
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_PARAMS};
    uint16_t FCGI_id(-1);
    uint16_t content_length(-1);
    uint8_t padding_length(-1);

    std::string message {"Case 4, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 5
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_STDIN};
    uint16_t FCGI_id {1};
    uint16_t content_length {1000};
    uint8_t padding_length {0};

    std::string message {"Case 5, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 6
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_STDOUT};
    uint16_t FCGI_id {1};
    uint16_t content_length {250};
    uint8_t padding_length {2};

    std::string message {"Case 6, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 7
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_STDERR};
    uint16_t FCGI_id {1};
    uint16_t content_length {2};
    uint8_t padding_length {6};

    std::string message {"Case 7, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 8
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_DATA};
    uint16_t FCGI_id(-1);
    uint16_t content_length(-1);
    uint8_t padding_length {7};

    std::string message {"Case 8, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 9
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_GET_VALUES};
    uint16_t FCGI_id {0};
    uint16_t content_length {100};
    uint8_t padding_length {4};

    std::string message {"Case 9, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 10
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT};
    uint16_t FCGI_id {0};
    uint16_t content_length {100};
    uint8_t padding_length {0};

    std::string message {"Case 10, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
  // Case 11
  {
    fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_UNKNOWN_TYPE};
    uint16_t FCGI_id {1};
    uint16_t content_length {8};
    uint8_t padding_length {8};

    std::string message {"Case 11, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, FCGI_id, content_length, padding_length);
  }
}

TEST(Utility, ExtractBinaryNameValuePairs)
{
  // Testing explanation
  // Examined properties:
  // 1) Number of name-value pairs. (no content, one pair, or more than one.)
  // 2) Number of bytes required to encode the name or value. From the
  //    encoding format, one byte or four bytes.
  // 3) Presence or absence of data, i.e. an empty name or value.
  // 4) Improperly encoded data (see cases below).
  // 5) content_ptr == nullptr && content_length != 0;
  // 6) content_length < 0;
  //
  // Test cases:
  //  1) Nothing to process (content_length == 0), for both
  //     content_ptr == nullptr and content_ptr != nullptr.
  //  2) Single pair. Empty name and value.
  //  3) Single pair. Empty value only.
  //  4) Single pair. Both name and value are non-empty.
  //  5) Single pair. Name requires one byte, value requires four bytes.
  //  6) Single pair. Name requires four bytes, value requires one byte.
  //  7) Multiple pairs with a terminal empty value.
  //  8) Multiple pairs with an empty value in the middle.
  //  9) Incorrect encoding: a single pair with extra information at the end.
  // 10) Incorrect encoding: a correct pair followed by another pair with
  //     incorrect length information.
  // 11) content_ptr == nullptr && content_length == 100.
  // 12) content_ptr == nullptr && content_length == -100.
  // 13) content_ptr != nullptr && content_length == -100.
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module:
  // 1) fcgi_si::EncodeNameValuePairs

  using NameValuePair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

  // Case 1: Nothing to process.
  EXPECT_EQ(std::vector<NameValuePair> {},
    fcgi_si::ExtractBinaryNameValuePairs(nullptr, 0));

  uint8_t test_value {0};
  EXPECT_EQ(std::vector<NameValuePair> {},
    fcgi_si::ExtractBinaryNameValuePairs(&test_value, 0));
  EXPECT_EQ(0, test_value);

  // Case 2: Single name-value pair. (1 byte, 1 byte) for lengths. 
  // Empty name and value.
  const char* empty_name_ptr {""};
  const char* empty_value_ptr {""};
  NameValuePair empty_empty_nv_pair {{empty_name_ptr, empty_name_ptr},
    {empty_value_ptr, empty_value_ptr}};
  std::vector<uint8_t> encoded_nv_pair {};
  encoded_nv_pair.push_back(0);
  encoded_nv_pair.push_back(0);
  std::vector<NameValuePair> result {fcgi_si::ExtractBinaryNameValuePairs(
    encoded_nv_pair.data(), encoded_nv_pair.size())};
  EXPECT_EQ(result[0], empty_empty_nv_pair);

  // Case 3: Single name-value pair. (1 byte, 1 byte) for lengths. Empty value.
  encoded_nv_pair.clear();
  const char* name_ptr {"Name"};
  NameValuePair name_empty_nv_pair {{name_ptr, name_ptr + 4},
    {empty_value_ptr, empty_value_ptr}};
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(0);
  for(auto c : name_empty_nv_pair.first)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result[0], name_empty_nv_pair);

  // Case 4: Single name-value pair. (1 byte, 1 byte) for lengths.
  encoded_nv_pair.clear();
  const char* value_ptr {"Value"};
  NameValuePair one_one_nv_pair {{name_ptr, name_ptr + 4},
    {value_ptr, value_ptr + 5}};
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(5);
  for(auto c : one_one_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : one_one_nv_pair.second)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result[0], one_one_nv_pair);

  // Case 5: Single name-value pair, (1 byte, 4 bytes) for lengths.
  std::vector<uint8_t> four_value_vector(128, 'a');
  NameValuePair one_four_nv_pair {{name_ptr, name_ptr + 4}, four_value_vector};
  encoded_nv_pair.clear();
  encoded_nv_pair.push_back(4);
  fcgi_si::EncodeFourByteLength(128, std::back_inserter(encoded_nv_pair));
  for(auto c : one_four_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : one_four_nv_pair.second)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result[0], one_four_nv_pair);

  // Case 6: Single name-value pair, (4 byte, 1 bytes) for lengths.
  std::vector<uint8_t> four_name_vector(256, 'b');
  NameValuePair four_one_nv_pair {four_name_vector, {value_ptr, value_ptr + 5}};
  encoded_nv_pair.clear();
  fcgi_si::EncodeFourByteLength(256, std::back_inserter(encoded_nv_pair));
  encoded_nv_pair.push_back(5);
  for(auto c : four_one_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : four_one_nv_pair.second)
    encoded_nv_pair.push_back(c);
   result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result[0], four_one_nv_pair);

  // Case 7: Multiple name-value pairs with names and values that need one and
  // four byte lengths. Also includes an empty value.
  encoded_nv_pair.clear();
  std::vector<NameValuePair> pairs {};
  pairs.push_back({four_name_vector, four_value_vector});
  fcgi_si::EncodeFourByteLength(four_name_vector.size(),
    std::back_inserter(encoded_nv_pair));
  fcgi_si::EncodeFourByteLength(four_value_vector.size(),
    std::back_inserter(encoded_nv_pair));
  for(auto c : pairs[0].first)
    encoded_nv_pair.push_back(c);
  for(auto c : pairs[0].second)
    encoded_nv_pair.push_back(c);
  pairs.push_back({one_one_nv_pair});
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(5);
  for(auto c : pairs[1].first)
    encoded_nv_pair.push_back(c);
  for(auto c : pairs[1].second)
    encoded_nv_pair.push_back(c);
  pairs.push_back(name_empty_nv_pair);
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(0);
  for(auto c : pairs[2].first)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result, pairs);

  // Case 8: As above, but with the empty value in the middle.
  encoded_nv_pair.clear();
  pairs.clear();
  pairs.push_back({four_name_vector, four_value_vector});
  fcgi_si::EncodeFourByteLength(four_name_vector.size(),
    std::back_inserter(encoded_nv_pair));
  fcgi_si::EncodeFourByteLength(four_value_vector.size(),
    std::back_inserter(encoded_nv_pair));
  for(auto c : pairs[0].first)
    encoded_nv_pair.push_back(c);
  for(auto c : pairs[0].second)
    encoded_nv_pair.push_back(c);
  pairs.push_back(name_empty_nv_pair);
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(0);
  for(auto c : pairs[1].first)
    encoded_nv_pair.push_back(c);
  pairs.push_back({one_one_nv_pair});
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(5);
  for(auto c : pairs[2].first)
    encoded_nv_pair.push_back(c);
  for(auto c : pairs[2].second)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result, pairs);

  // Case 9: An incomplete encoding. A single name and value is present. Extra
  // information is added. ProcessBinaryNameValuePairs should return an
  // empty vector.
  encoded_nv_pair.clear();
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(5);
  for(auto c : one_one_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : one_one_nv_pair.second)
    encoded_nv_pair.push_back(c);
  encoded_nv_pair.push_back(10);
  // A byte with length information was added above, but there is no associated
  // data.
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result, std::vector<NameValuePair> {});

  // Case 10: Too many bytes were specified for the last name, but the first 
  // name-value pair was correct. An empty vector should still be returned.
  encoded_nv_pair.clear();
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(5);
  for(auto c : one_one_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : one_one_nv_pair.second)
    encoded_nv_pair.push_back(c);
  encoded_nv_pair.push_back(100);
  encoded_nv_pair.push_back(5);
  for(auto c : one_one_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : one_one_nv_pair.second)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
    encoded_nv_pair.size());
  EXPECT_EQ(result, std::vector<NameValuePair> {});

  // Case 11: content_ptr == nullptr, content_length == 100.
  EXPECT_THROW((fcgi_si::ExtractBinaryNameValuePairs(nullptr, 100)),
    std::invalid_argument);

  // Case 12: content_ptr == nullptr, content_length == -100.
  EXPECT_THROW((fcgi_si::ExtractBinaryNameValuePairs(nullptr, -100)),
    std::invalid_argument);

  // Case 13: content_ptr != nullptr, content_length == -100
  encoded_nv_pair.clear();
  encoded_nv_pair.push_back(1);
  encoded_nv_pair.push_back(1);
  encoded_nv_pair.push_back('a');
  encoded_nv_pair.push_back('b');
  EXPECT_THROW(
    (fcgi_si::ExtractBinaryNameValuePairs(encoded_nv_pair.data(), -100)),
    std::invalid_argument
  );
}

TEST(Utility, EncodeNameValuePairs)
{
  // Testing explanation
  //    Most test cases perform a sequence of calls which encodes, writes, and
  // and then decodes a sequence of name-value pairs. The goal of such a case
  // is to demonstrate that this process recovers the original name-value
  // pairs. In other words, such cases demonstrate that these operations are
  // equivalent to an identity operation.
  //    In particular, most cases construct a list of name-value pairs, call
  // EncodeNameValuePairs on the list from appropriate iterators, and then
  // perform a gather write to a temporary file using writev. The written byte
  // sequence is processed using a function developed for this purpose,
  // ExtractContent. The content is extracted and then processed with
  // ProcessBinaryNameValuePairs. Finally, the generated list of name-value
  // pairs is compared with the original list.
  //    Note that the testing of ExtractContent and ProcessBinaryNameValuePairs
  // cannot depend on EncodeNameValuePairs.
  //
  // Examined properties:
  // 1) Name-value pair sequence identity as described above.
  // 2) Record alignment: all records should have a total length which
  //    is a multiple of eight bytes.
  // 3) Specific values for name and value.
  //    a) The presence of empty names and values.
  //    b) The presence of duplicate names. Duplicates should be present to
  //       ensure that the implementation can handle accidental duplicate names.
  //    c) Names and values which have a length large enough to require
  //       four bytes to be encoded in the FastCGI name-value format.
  // 4) The need for padding. Records should be present which will likely
  //    require padding if the suggested 8-byte alignment condition is met.
  // 5) Number of records. Sequences which require more than one full record
  //    should be present.
  // 6) Presence of a name or value whose length exceeds the maximum size
  //    allowed by the FastCGI protocol. (Test case for erroneous input.)
  // 7) Large and small FCGI_id values. In particular, values greater than 255.
  // 8) A large number of sequence elements. In particular, larger than
  //    the current limit for the number of struct iovec instances passed in
  //    an array to a scatter-gather operation.
  // Note that the use of ExtractContent and ProcessBinaryNameValuePairs
  // introduces additional checks. For example, ExtractContent checks for
  // header type and FastCGI request identifier errors.
  //
  // Test cases:
  //  1) No name-value pairs, i.e. pair_iter == end.
  //  2) A name-value pair that requires a single FastCGI record.
  //     The content length of the record is a multiple of eight bytes and,
  //     as such, no padding is needed.
  //  3) A name-value pair that requires a single FastCGI record. This
  //     record requires padding.
  //  4) As in 3, but with a FCGI_id larger than 255.
  //  5) A name-value pair with an empty name and an empty value.
  //  6) A name-value pair with a non-empty name and an empty value.
  //  7) Two name-value pairs where each is a duplicate of the other.
  //  8) Multiple name-value pairs that only require a single FastCGI record.
  //     The total length of the record does not require padding.
  //  9) As in 8, but padding is required.
  // 10) A single name-value pair whose name has a length which exceeds the
  //     maximum size of a FastCGI record. Note that this also means that
  //     four bytes are required to encode the length of this element.
  // 11) As in 9, but for value instead of name.
  // 12) Multiple name-pairs that require more than one FastCGI record.
  // 13) Multiple name-value pairs where a single name is empty and several
  //     values are empty.
  // 14) Multiple name-value pairs with several cases where names are repeated.
  // 15) Multiple name-value pairs where one of the middle pairs has a name
  //     whose length exceeds the maximum size. (Invalid input.)
  // 16) As in 15, but for value instead of name. (Invalid input.)
  // 17) More than the current iovec limit of name-value pairs. This case tests
  //     the functionality of EncodeNameValuePairs which allows very long
  //     sequences to be encoded by iteratively calling the function.
  //
  // Modules which testing depends on:
  // 1) fcgi_si_test::ExtractContent
  // 2) fcgi_si::ProcessBinaryNameValuePairs
  //
  // Other modules whose testing depends on this module: none.

  using NameValuePair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

  // A lambda function for equality testing of vectors of struct iovec
  // instances. (operator= is not defined in the instantiation
  // std::vector<iovec>.)
  auto IovecVectorEquality = [](const std::vector<iovec>& lhs,
    const std::vector<iovec>& rhs)->bool
  {
    if(lhs.size() != rhs.size())
      return false;
    auto lhs_iter {lhs.begin()};
    auto rhs_iter {rhs.begin()};
    for(/*no-op*/; lhs_iter != lhs.end(); (++lhs_iter, ++rhs_iter))
      if((lhs_iter->iov_base != rhs_iter->iov_base)
        || (lhs_iter->iov_len != rhs_iter->iov_len))
        return false;
    return true;
  };

  int temp_fd {};
  fcgi_si_test::CreateBazelTemporaryFile(&temp_fd);

  // A lambda function which takes parameters which define a test case and
  // goes through the testing procedure described in the testing explanation.
  auto EncodeNameValuePairTester = [temp_fd](
    std::string message,
    const std::vector<NameValuePair>& pair_sequence,
    fcgi_si::FCGIType type,
    uint16_t FCGI_id,
    std::size_t offset_argument,
    bool expect_processing_error,
    int returned_offset_test_value,
    std::vector<NameValuePair>::const_iterator expected_pair_it
  )->void
  {
    auto encoded_result = fcgi_si::EncodeNameValuePairs(pair_sequence.begin(),
      pair_sequence.end(), type, FCGI_id, offset_argument);
    if(expect_processing_error && std::get<0>(encoded_result))
    {
      ADD_FAILURE() << "EncodeNameValuePairs did not detect an expected error "
        "as reported by std::get<0>."
        << '\n' << message;
      return;
    }
    else if(!expect_processing_error && !std::get<0>(encoded_result))
    {
      ADD_FAILURE() << "EncodeNameValuePairs encountered an unexpected error "
        "as reported by std::get<0>."
        << '\n' << message;
      return;
    }
    size_t total_to_write {std::get<1>(encoded_result)};
    // std::get<2>(encoded_result) is used in a call to writev.
    // std::get<3>(encoded_result) is implciitly used in a call to writev.
    if(returned_offset_test_value == 0 && std::get<4>(encoded_result) != 0)
    {
      ADD_FAILURE() << "EncodeNameValuePairs returned a non-zero offset as "
        "reported by std::get<4> when a zero offset was expected."
        << '\n' << message;
      // Don't return as we can still test name-value pairs.
    }
    else if(returned_offset_test_value > 0 && std::get<4>(encoded_result) == 0)
    {
      ADD_FAILURE() << "EncodeNameValuePairs returned a zero offset as "
        "reported by std::get<4> when a non-zero offset was expected."
        << '\n' << message;
      // Don't return as we can still test name-value pairs.
    }
    if(std::get<5>(encoded_result) != expected_pair_it)
    {
      ADD_FAILURE() << "EncodeNameValuePairs returned an iterator as reported by "
        "std::get<5> which did not point to the expected name-value pair."
        << '\n' << message;
      // Don't return as we can still test name-value pairs.
    }

    // Prepare the temporary file.
    bool prepared {fcgi_si_test::PrepareTemporaryFile(temp_fd)};
    if(!prepared)
      FAIL() << "A temporary file could not be prepared.";

    ssize_t write_return {0};
    while((write_return = writev(temp_fd, std::get<2>(encoded_result).data(),
      std::get<2>(encoded_result).size())) == -1 && errno == EINTR)
      continue;
    if(write_return != total_to_write)
    {
      ADD_FAILURE() << "A call to writev did not write all bytes requested."
      << '\n' << message;
      return;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }
    auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_PARAMS, FCGI_id)};
    if(!std::get<0>(extract_content_result))
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent encountered an "
        "unrecoverable read error." << '\n' << message;
      return;
    }
    if(!std::get<1>(extract_content_result))
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent reported from "
        "std::get<1> that a header error or a partial section was encountered."
        << '\n' << message;
      return;
    }
    if(std::get<2>(extract_content_result))
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent reported from "
        "std::get<2> that the record sequence was terminated."
        << '\n' << message;
      // Don't return as we can still test name-value pairs.
    }
    if(!std::get<3>(extract_content_result))
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent reported from "
        "std::get<3> that an unaligned record was present."
        << '\n' << message;
      // Don't return as we can still test name-value pairs.
    }
    std::vector<NameValuePair> pair_result_sequence {
      fcgi_si::ExtractBinaryNameValuePairs(
        std::get<4>(extract_content_result).data(),
        std::get<4>(extract_content_result).size()
      )
    };
    EXPECT_EQ(pair_sequence, pair_result_sequence);
  };

  // Case 1: No name-value pairs, i.e. pair_iter == end.
  {
    std::vector<NameValuePair> empty {};
    auto result {fcgi_si::EncodeNameValuePairs(empty.begin(), empty.end(),
      fcgi_si::FCGIType::kFCGI_PARAMS, 1, 0)};
    EXPECT_TRUE(std::get<0>(result));
    EXPECT_EQ(std::get<1>(result), 0);
    EXPECT_TRUE(IovecVectorEquality(std::get<2>(result), std::vector<iovec> {}));
    EXPECT_EQ(std::get<3>(result), std::vector<uint8_t> {});
    EXPECT_EQ(std::get<4>(result), 0);
    EXPECT_EQ(std::get<5>(result), empty.end());
  }

  // Case 2: A name-value pair that requires a single FastCGI record.
  // The content length of the record is a multiple of eight bytes and,
  // as such, no padding is needed.
  {
    std::string message {"Case 2, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'n','a','m','e'}, {'v','l'}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 3: A name-value pair that requires a single FastCGI record. This
  // record requires padding.
  {
    std::string message {"Case 3, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'n','a','m','e'},
      {'v','a','l','u','e'}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 4: As in 3, but with a FCGI_id larger than 255.
  {
    std::string message {"Case 4, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'n','a','m','e'},
      {'v','a','l','u','e'}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1000,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 5: A name-value pair with an empty name and an empty value.
  {
    std::string message {"Case 5, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{}, {}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 6: A name-value pair with a non-empty name and an empty value.
  {
    std::string message {"Case 6, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'o','n','e'}, {}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 7: Two name-value pairs where each is a duplicate of the other.
  {
    std::string message {"Case 7, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'o','n','e'}, {'t','w','o'}},
      {{'o','n','e'}, {'t','w','o'}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 8: Multiple name-value pairs that only require a single FastCGI
  // record. The total length of the record does not require padding.
  {
    std::string message {"Case 8, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{0}, {1}}, {{1}, {2}}, {{2}, {4}},
      {{3}, {8}}, {{4}, {16}}, {{5}, {32}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 9: As in 8, but padding is required.
  {
    std::string message {"Case 9, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{0}, {1}}, {{1}, {2}}, {{2}, {4}},
      {{3}, {8}}, {{4}, {16}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 10: A single name-value pair whose name has a length which exceeds
  // the maximum size of a FastCGI record. Note that this also means that
  // four bytes are required to encode the length of this element.
  {
    std::string message {"Case 10, about line: "};
    message += std::to_string(__LINE__);
    std::vector<uint8_t> large_name(100000, 'a');
    std::vector<NameValuePair> pair_sequence {{std::move(large_name), {1}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.end()
    );
  }

  // Case 11: As in 9, but for value instead of name.
  {
    std::string message {"Case 11, about line: "};
    message += std::to_string(__LINE__);
    std::vector<uint8_t> large_value(100000, 10);
    std::vector<NameValuePair> pair_sequence {{{'n','a','m','e'},
      std::move(large_value)}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.end()
    );
  }

  // Case 12: Multiple name-pairs that require more than one FastCGI record.
  {
    std::string message {"Case 12, about line: "};
    message += std::to_string(__LINE__);
    std::vector<uint8_t> large_name(100, 'Z');
    std::vector<uint8_t> large_value(100000, 10);
    std::vector<NameValuePair> pair_sequence {{{'n','a','m','e'},
      std::move(large_value)}, {{'a'}, {1}}, {{'b'}, {2}},
      {std::move(large_name), {3}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.end()
    );
  }

  // Case 13: Multiple name-value pairs where a single name is empty and
  // several values are empty.
  {
    std::string message {"Case 13, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'a'}, {}}, {{'b'}, {1}},
      {{'c'}, {2}}, {{}, {3}}, {{'e'}, {4}}, {{'f'}, {}}, {{'g'}, {}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.end()
    );
  }

  // Case 14: Multiple name-value pairs with several cases where names are
  // repeated.
  {
    std::string message {"Case 14, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'a'}, {0}}, {{'a'}, {1}},
      {{'b'}, {2}}, {{'c'}, {3}}, {{'d'}, {4}}, {{'d'}, {5}}, {{'b'}, {6}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FCGIType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.end()
    );
  }

  // Currently fails with a Bazel test setup error. Preliminary code below.
  // // Case 15: Multiple name-value pairs where one of the middle pairs has a
  // // name whose length exceeds the maximum size. (Invalid input.)
  // {
  //   std::vector<uint8_t> illegal_name((1UL << 31) + 10, 'd');
  //   std::vector<NameValuePair> pair_sequence {{{'a'}, {0}}, {{'b'}, {1}},
  //     {{'c'}, {2}}, {std::move(illegal_name), {3}}, {{'e'}, {4}},
  //     {{'f'}, {5}}, {{'g'}, {6}}};
  //
  //   // Parameters for the below code (in the style of the lambda).
  //   fcgi_si::FCGIType type {fcgi_si::FCGIType::kFCGI_PARAMS};
  //   uint16_t FCGI_id {1};
  //   std::size_t offset_argument {0};
  //   bool expect_processing_error {true};
  //   int returned_offset_test_value {0};
  //   std::vector<NameValuePair>::const_iterator expected_pair_it
  //     {pair_sequence.cbegin() + 3};
  //   while(true)
  //   {
  //     auto encoded_result = fcgi_si::EncodeNameValuePairs(pair_sequence.begin(),
  //       pair_sequence.end(), type, FCGI_id, offset_argument);
  //     if(expect_processing_error && std::get<0>(encoded_result))
  //     {
  //       ADD_FAILURE() << "EncodeNameValuePairs did not detect an expected error "
  //         "as reported by std::get<0>.";
  //       break;
  //     }
  //     size_t total_to_write {std::get<1>(encoded_result)};
  //     // std::get<2>(encoded_result) is used in a call to writev.
  //     // std::get<3>(encoded_result) is implciitly used in a call to writev.
  //     if(returned_offset_test_value == 0 && std::get<4>(encoded_result) != 0)
  //     {
  //       ADD_FAILURE() << "EncodeNameValuePairs returned a non-zero offset as "
  //         "reported by std::get<4> when a zero offset was expected.";
  //       // Don't return as we can still test name-value pairs.
  //     }
  //     if(std::get<5>(encoded_result) != expected_pair_it)
  //     {
  //       ADD_FAILURE() << "EncodeNameValuePairs returned an iterator as reported by "
  //         "std::get<5> which did not point to the expected name-value pair.";
  //       // Don't return as we can still test name-value pairs.
  //     }
  //     int temp_fd {open(".", O_RDWR | O_TMPFILE)};
  //     if(temp_fd == -1)
  //     {
  //       ADD_FAILURE() << "A call to open failed to make a temporary file.";
  //       break;
  //     }
  //     ssize_t write_return {0};
  //     while((write_return = writev(temp_fd, std::get<2>(encoded_result).data(),
  //       std::get<2>(encoded_result).size())) == -1 && errno == EINTR)
  //       continue;
  //     if(write_return != total_to_write)
  //     {
  //       ADD_FAILURE() << "A call to writev did not write all bytes requested.";
  //       break;
  //     }
  //     off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
  //     if(lseek_return == -1)
  //     {
  //       close(temp_fd);
  //       ADD_FAILURE() << "A call to lseek failed.";
  //       break;
  //     }
  //     auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
  //       fcgi_si::FCGIType::kFCGI_PARAMS, FCGI_id)};
  //     if(!std::get<0>(extract_content_result))
  //     {
  //       close(temp_fd);
  //       ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent encountered an "
  //         "unrecoverable read error.";
  //       break;
  //     }
  //     if(!std::get<1>(extract_content_result))
  //     {
  //       close(temp_fd);
  //       ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent reported from "
  //         "std::get<1> that a header error or a partial section was encountered.";
  //       break;
  //     }
  //     if(std::get<2>(extract_content_result))
  //     {
  //       close(temp_fd);
  //       ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent reported from "
  //         "std::get<2> that the record sequence was terminated.";
  //       // Don't return as we can still test name-value pairs.
  //     }
  //     if(!std::get<3>(extract_content_result))
  //     {
  //       close(temp_fd);
  //       ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent reported from "
  //         "std::get<3> that an unaligned record was present.";
  //       // Don't return as we can still test name-value pairs.
  //     }
  //     std::vector<NameValuePair> pair_result_sequence {
  //       fcgi_si::ProcessBinaryNameValuePairs(std::get<4>(
  //       extract_content_result).size(), std::get<4>(
  //       extract_content_result).data())};
  //     std::vector<NameValuePair> expected_result_pairs(pair_sequence.cbegin(),
  //       expected_pair_it);
  //     EXPECT_EQ(expected_result_pairs, pair_result_sequence);
  //     break;
  //   }
  // }

  // Case 16: As in 15, but for value instead of name. (Invalid input.)
  // (Deferred until Bazel error for case 15 is resolved.)

  // Case 17: More than the current iovec limit of name-value pairs.
  {
    bool case_single_iteration {true};
    while(case_single_iteration)
    {
      case_single_iteration = false;

      long iovec_max {sysconf(_SC_IOV_MAX)};
      if(iovec_max == -1)
        iovec_max = 1024;
      iovec_max = std::min<long>(iovec_max, std::numeric_limits<int>::max());
      NameValuePair copied_pair {{'a'}, {1}};
      std::vector<NameValuePair> many_pairs(iovec_max + 10, copied_pair);
      std::size_t offset {0};
      
      bool prepared {fcgi_si_test::PrepareTemporaryFile(temp_fd)};
      if(!prepared)
        FAIL() << "A temporary file could not be prepared.";

      bool terminal_error {false};
      ssize_t write_return {0};
      for(auto pair_iter {many_pairs.begin()}; pair_iter != many_pairs.end();
        /*no-op*/)
      {
        auto encoded_result = fcgi_si::EncodeNameValuePairs(pair_iter,
          many_pairs.end(), fcgi_si::FCGIType::kFCGI_PARAMS, 1, offset);
        if(!std::get<0>(encoded_result))
        {
          ADD_FAILURE() << "A call to fcgi_si::EncodeNameValuePairs halted "
            "due to an error as reported by std::get<0>.";
          terminal_error = true;
          break;
        }
        while((write_return = writev(temp_fd, std::get<2>(encoded_result).data(),
          std::get<2>(encoded_result).size())) == -1 && errno == EINTR)
          continue;
        if(write_return != std::get<1>(encoded_result))
        {
          ADD_FAILURE() << "A call to writev did not write all bytes requested.";
          terminal_error = true;
          break;
        }
        offset = std::get<4>(encoded_result);
        pair_iter = std::get<5>(encoded_result);
      }
      if(terminal_error)
      {
        break;
      }

      // Code for inspecting the sequence of records written to temp_fd.
      // // Copy temporary file contents for inspection.
      // const char* new_file_path
      //   {"/home/adam/Desktop/EncodeNameValuePairs_output.bin"};
      // int new_fd {open(new_file_path, O_RDWR | O_CREAT | O_TRUNC,
      //   S_IRWXU)};
      // if(new_fd == -1)
      // {
      //   ADD_FAILURE() << "A call to open encountered an error. "
      //     << std::strerror(errno);
      //   close(temp_fd);
      //   break;
      // }
      // ssize_t file_copy_return {0};
      // off_t copy_start {0};
      //
      // ssize_t number_to_write {lseek(temp_fd, 0, SEEK_END)};
      // if(number_to_write == -1)
      // {
      //   ADD_FAILURE() << "A call to lseek encountered an error.";
      //   close(temp_fd);
      //   break;
      // }
      // while(number_to_write > 0)
      // {
      //   while((file_copy_return = sendfile(new_fd, temp_fd, &copy_start,
      //     number_to_write)) == -1 && errno == EINTR)
      //     continue;
      //   if(file_copy_return == -1)
      //   {
      //     ADD_FAILURE() << "A call to sendfile encountered an unrecoverable "
      //       "error. " << std::strerror(errno);
      //     close(temp_fd);
      //     close(new_fd);
      //     break;
      //   }
      //   number_to_write -= file_copy_return;
      // }
      // if(number_to_write > 0)
      // {
      //   ADD_FAILURE() << "Calls to sendfile could not write all data.";
      //   break;
      // }
      // close(new_fd);

      // Prepare to extract content.
      off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
      if(lseek_return == -1)
      {
        ADD_FAILURE() << "A call to lseek encountered an error.";
        break;
      }
      auto extract_content_result {fcgi_si_test::ExtractContent(temp_fd,
        fcgi_si::FCGIType::kFCGI_PARAMS, 1)};
      if(!std::get<0>(extract_content_result))
      {
        ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent encountered an "
          "unrecoverable read error as reported by std::get<0>.";
        break;
      }
      if(!std::get<1>(extract_content_result))
      {
        ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent encountered a "
          "header error or an incomplete section as reported by std::get<1>.";
        break;
      }
      if(std::get<2>(extract_content_result))
      {
        ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent unexpectedly "
          "reported by std::get<2> that the record sequence was terminated.";
      }
      if(!std::get<3>(extract_content_result))
      {
        ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent detected an "
          "unaligned record as reported by std::get<3>.";
        break;
      }
      std::vector<NameValuePair> pair_result_sequence {
        fcgi_si::ExtractBinaryNameValuePairs(
          std::get<4>(extract_content_result).data(),
          std::get<4>(extract_content_result).size())
      };
      EXPECT_EQ(many_pairs, pair_result_sequence);
    }
  }
  close(temp_fd);
}

TEST(Utility, ToUnsignedCharacterVector)
{
  // Testing explanation
  // Examined properties:
  // 1) Presence of negative values.
  // 2) Zero.
  // 2) Presence of positive values.
  //
  // Test cases:
  // 1) c == std::numeric_limits<int>::min()
  // 2) c = -200
  // 3) c == -1
  // 4) c == 0
  // 5) c == 1
  // 6) c == 100
  // 7) c == std::numeric_limits<int>::max()
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module: none.
  //
  // Note: The minimum and maximum values assume 32-bit, two's complement
  // integers. If this is not the case, extreme cases are not tested.

  bool test_extremes {(std::numeric_limits<unsigned int>::max()
    == std::numeric_limits<uint32_t>::max()) 
    && (std::numeric_limits<int>::min() < -std::numeric_limits<int>::max())};

  // Case 1
  if(test_extremes)
    EXPECT_THROW(
      (fcgi_si::ToUnsignedCharacterVector(std::numeric_limits<int>::min())),
      std::invalid_argument
    );  
  // Case 2
  EXPECT_THROW(fcgi_si::ToUnsignedCharacterVector(-200), std::invalid_argument);
  // Case 3
  EXPECT_THROW(fcgi_si::ToUnsignedCharacterVector(-1), std::invalid_argument);
  // Case 4
  EXPECT_EQ(fcgi_si::ToUnsignedCharacterVector(0),
    (std::vector<uint8_t> {'0'}));
  // Case 5
  EXPECT_EQ(fcgi_si::ToUnsignedCharacterVector(1),
    (std::vector<uint8_t> {'1'}));
  // Case 6
  EXPECT_EQ(fcgi_si::ToUnsignedCharacterVector(100),
    (std::vector<uint8_t> {'1','0','0'}));
  // Case 7
  if(test_extremes)
    EXPECT_EQ(
      fcgi_si::ToUnsignedCharacterVector(std::numeric_limits<int>::max()),
      (std::vector<uint8_t> {'2','1','4','7','4','8','3','6','4','7'})
    );
}

TEST(Utility, PartitionByteSequence)
{
  // Testing explanation
  //    Tests call PartitionByteSequence, use writev to write to a temporary
  // file, and use fcgi_si_test::ExtractContent to retrieve the content of
  // the written FastCGI record sequence. ExtractContent performs checks on the
  // header values of type and request ID. The identity of the extracted
  // content is checked. Since it is unspecified how much data from the
  // range [begin_iter, end_iter) is encoded, the length of the extracted
  // content is used to calculate a new iterator value. This value is compared
  // to the iterator returned by PartitionByteSequence.
  //
  // Examined properties:
  // 1) Value of type: a type from a client, a type from the application
  //    server, and a type value that is not defined by the FastCGI
  //    specification.
  // 2) Value of FCGI_id: equal to 0, greater than zero but less than the
  //    maximum value, equal to the maximum value.
  // 3) Size of the content byte sequence:
  //    a) No content, i.e. begin_iter == end_iter.
  //    b) Nonzero but 1) much less than the maximum value of a FastCGI record
  //       body and 2) not a multiple of 8 (so that padding is necessary).
  //    c) Equal to the size of the maximum value that is less than the FastCGI
  //       record body size and a multiple of 8 ((2^16 - 1) - 7 = 65528).
  //    d) So large that a single call can likely not encode all of the content.
  //       A content byte sequence with a length of 2^25 bytes will be used.
  //       This value was derived from the assumption that the maximum number
  //       of struct iovec instances which can be handled by a call to writev
  //       is less than or equal to 1024. This is the current maximum on Linux.
  // 4) Content value: the extracted byte sequence must match the original 
  //    byte sequence.
  // 5) Iterator value.
  //
  // Test cases:
  // 1) begin_iter == end_iter, 
  //    type == fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT, FCGI_id == 0.
  // 2) std::distance(end_iter, begin_iter) == 3,
  //    type == fcgi_si::FCGIType::kFCGI_STDIN, FCGI_id == 1.
  // 3) std::distance(end_iter, begin_iter) == 25,
  //    type == fcgi_si::FCGIType::kFCGI_STDOUT, FCGI_id == 65535 ==
  //    std::numeric_limits<std::uint16_t>::max().
  // 4) std::distance(end_iter, begin_iter) == 8,
  //    type == static_cast<fcgi_si::FCGIType>(20), FCGI_id == 3.
  // 5) std::distance(end_iter, begin_iter) == 65528,
  //    type == fcgi_si::FCGIType::kFCGI_PARAMS, FCGI_id == 300.
  // 6) std::distance(end_iter, begin_iter) == 2^25,
  //    type == fcgi_si::FCGIType::kFCGI_STDOUT, FCGI_id == 3.
  //
  // Modules which testing depends on:
  // 1) fcgi_si_test::ExtractContent
  //
  // Other modules whose testing depends on this module: none.

  // BAZEL DEPENDENCY
  int temp_descriptor {};
  fcgi_si_test::CreateBazelTemporaryFile(&temp_descriptor);
  
  auto PartitionByteSequenceTester = [temp_descriptor](
    const std::string& message,
    bool expect_terminal_empty_record,
    const std::vector<std::uint8_t>& content_seq, 
    fcgi_si::FCGIType type,
    std::uint16_t FCGI_id
  )->void
  {
    // Clear the file.
    if(ftruncate(temp_descriptor, 0) < 0)
    {
      ADD_FAILURE() << "A call to ftruncate failed." << '\n' << message;
      return;
    }
    off_t lseek_return {lseek(temp_descriptor, 0, SEEK_SET)};
    if(lseek_return < 0)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }

    // Call PartitionByteSequence and write the encoded record sequence.
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
      std::size_t, std::vector<std::uint8_t>::const_iterator> pr {
      fcgi_si::PartitionByteSequence(content_seq.begin(), content_seq.end(), 
        type, FCGI_id)};

    ssize_t writev_return {};
    while(((writev_return = writev(temp_descriptor, std::get<1>(pr).data(),
      std::get<1>(pr).size())) == -1) && (errno == EINTR))
      continue;
    if((writev_return < 0) || (static_cast<std::size_t>(writev_return) < 
      std::get<2>(pr)))
    {
      ADD_FAILURE() << "A call to writev failed." << '\n' << message;
      return;
    }

    // Extract the content and validate.
    lseek_return = lseek(temp_descriptor, 0, SEEK_SET);
    if(lseek_return < 0)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }
    std::tuple<bool, bool, bool, bool, std::vector<uint8_t>> ecr
      {fcgi_si_test::ExtractContent(temp_descriptor, type, FCGI_id)};
    if(!std::get<0>(ecr))
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent encountered "
        "an error." << '\n' << message;
      return;
    }
    if(!std::get<1>(ecr))
    {      
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent determined that "
        "a header error was present or an incomplete record was present." << 
        '\n' << message << '\n' << "Length of the iovec list: " << 
        std::get<1>(pr).size() << '\n' << "Number to write: " << std::get<2>(pr);
      return;
    }
    if(( std::get<2>(ecr) && !expect_terminal_empty_record) || 
       (!std::get<2>(ecr) &&  expect_terminal_empty_record))
    {
      ADD_FAILURE() << "A terminal empty record mismatch was present." << 
        '\n' << message;
      return;
    }
    // std::get<3>(ecr) tests record alignment on 8-byte boundaries.
    // Such alignment is not specified by PartitionByteSequence.

    // This check ensures that PartitionByteSequence encodes some content
    // when content is given. The next check does not verify this property.
    if(content_seq.size() && (!std::get<4>(ecr).size()))
    {
      ADD_FAILURE() << "PartitionByteSequence caused nothing to be written " 
        "when content was present." << '\n' << message;
      return;
    }
    // Check for equality of the extracted byte sequence and the prefix of the
    // content sequence indicated by the information returned by
    // PartitionByteSequence.
    std::vector<std::uint8_t>::difference_type returned_length
      {std::distance(content_seq.begin(), std::get<3>(pr))};
    if((returned_length > content_seq.size())       ||
       (returned_length != std::get<4>(ecr).size()) ||
       (std::mismatch(content_seq.begin(), std::get<3>(pr), 
                      std::get<4>(ecr).begin())
        != std::make_pair(std::get<3>(pr), 
                          std::get<4>(ecr).begin() + returned_length)))
    {
      ADD_FAILURE() << "The extracted byte sequence did not match the prefix." 
        << '\n' << message;
      return;
    }

    return;
  };

  // Case 1: begin_iter == end_iter, 
  // type == fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT, FCGI_id == 0.
  {
    std::string message {"Case 1, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> empty {};
    PartitionByteSequenceTester(
      message,
      true,
      empty, 
      fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT,
      0
    );
  }

  // Case 2: std::distance(end_iter, begin_iter) == 3,
  // type == fcgi_si::FCGIType::kFCGI_STDIN, FCGI_id == 1.
  {
    std::string message {"Case 2, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {1,2,3};
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FCGIType::kFCGI_STDIN,
      1
    );
  }

  // Case 3: std::distance(end_iter, begin_iter) == 25,
  // type == fcgi_si::FCGIType::kFCGI_STDOUT, FCGI_id == 65535 ==
  // std::numeric_limits<std::uint16_t>::max(). 
  {
    std::string message {"Case 3, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {};
    for(int i {0}; i < 25; ++i)
      content.push_back(i);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FCGIType::kFCGI_STDOUT,
      std::numeric_limits<std::uint16_t>::max()
    );
  }

  // Case 4: std::distance(end_iter, begin_iter) == 8,
  // type == static_cast<fcgi_si::FCGIType>(20), FCGI_id == 3.
  {
    std::string message {"Case 4, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {};
    for(int i {0}; i < 8; ++i)
      content.push_back(i);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      static_cast<fcgi_si::FCGIType>(20),
      3
    );
  }

  // Case 5: std::distance(end_iter, begin_iter) == 65528,
  // type == fcgi_si::FCGIType::kFCGI_PARAMS, FCGI_id == 300.
  {
    std::string message {"Case 5, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {};
    for(int i {0}; i < 65528; ++i)
      content.push_back(i); // mod 256 overflow.
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FCGIType::kFCGI_PARAMS,
      300
    );
  }

  // Case 6: std::distance(end_iter, begin_iter) == 2^25,
  // type == fcgi_si::FCGIType::kFCGI_STDOUT, FCGI_id == 3.
  {
    std::string message {"Case 6, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content(1U << 25, 1U);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FCGIType::kFCGI_STDOUT,
      3
    );
  }
  close(temp_descriptor);
}

// fcgi_si content (FCGIServerInterface)

TEST(FCGIServerInterface, ConstructionExceptions)
{
  // Examined properties:
  // (Let "positive" mean an exception was thrown.)
  // Properties which should cause a throw during construction:
  // ("true positive" or "false negative" determination: EXPECT_THROW)
  // 1) Invalid socket properties:
  //    a) listening_descriptor does not refer to a socket.
  //    b) The socket type is not SOCK_STREAM.
  //    c) The socket is not listening.
  // 2) Invalid properties related to FCGI_WEB_SERVER_ADDRS.
  //    a) FCGI_WEB_SERVER_ADDRS is bound and non-empty, the domain of the
  //       socket is an internet domain, and no valid internet addresses are
  //       present after the value of FCGI_WEB_SERVER_ADDRS was processed as
  //       a comma-separated list of the appropriate internet addresses.
  // 3) Invalid value of max_connections: less than zero, zero.
  // 4) Invalid value of max_requests: less than zero, zero.
  // 5) Singleton violation: an interface is present and a call to construct
  //    another interface is made.
  // 
  // Properties which should not cause a throw:
  // ("false positive" or "true negative" determination: EXPECT_NO_THROW)
  // 1) Value of listening_descriptor: zero, non-zero.
  // 2) Maximum value of max_connections.
  // 3) Maximum value of max_requests.
  // 4) A non-default value for app_status_on_abort.
  // 5) An internet domain socket which either has FCGI_WEB_SERVER_ADDRS
  //    unbound or bound and empty.
  // 6) A Unix domain socket:
  //    a) Where FCGI_WEB_SERVER_ADDRS is unbound.
  //    b) Where FCGI_WEB_SERVER_ADDRS is bound to internet addresses.
  //
  // Test cases:
  // Throw expected:
  //  1) listening_descriptor refers to a file which is not a socket.
  //  2) listening_descriptor refers to a datagram socket (SOCK_DGRAM).
  //  3) listening_descriotor refers to a socket which not set to the listening
  //     state.
  //  4) The socket is of domain AF_INET and only IPv6 addresses are present.
  //  5) The socket is of domain AF_INET6 and only IPv4 addresses are present.
  //  6) The socket is of domain AF_INET and a combination of invalid IPv4
  //     addresses and IPv6 addresses are present. "Invalid" means malformed.
  //  7) The socket is of domain AF_INET and only a comma is present.
  //  8) max_connections == -1.
  //  9) max_connections == 0.
  // 10) max_requests == -1.
  // 11) max_requests == 0. 
  // 12) An interface already exists and another call to the constructor is
  //     made. The arguments to the second call are the same as the first.
  // Throw not expected:
  // 13) listening_descriptor == 0. The descriptor is a valid socket.
  //     FCGI_WEB_SERVER_ADDRS is unbound.
  // 14) listening_descriptor != 0. The descriptor is a valid socket.
  //     FCGI_WEB_SERVER_ADDRS is bound and empty.
  // 15) max_connections == std::numeric_limits<int>::max() &&
  //     max_requests    == std::numeric_limits<int>::max()
  //     Also, a non-default value is provided for app_status_on_abort.
  // 16) A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is unbound.
  // 17) A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is bound and has
  //     IPv4 address 127.0.0.1.
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module: none.

  // Ensure that FCGI_WEB_SERVER_ADDRS is bound and empty to establish a
  // consistent start state.
  if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
    FAIL() << "setenv failed" << '\n' << strerror(errno);

  // Case 1: listening_descriptor refers to a file which is not a socket.
  // Create a temporary regular file.
  {
    int temp_fd {};
    fcgi_si_test::CreateBazelTemporaryFile(&temp_fd);
    EXPECT_THROW(fcgi_si::FCGIServerInterface(temp_fd, 1, 1), std::exception);
    close(temp_fd);
  }

  // Case 2: listening_descriptor refers to a datagram socket (SOCK_DGRAM).
  {
    int socket_fd {socket(AF_INET, SOCK_DGRAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in case 2." << '\n' 
        << strerror(errno);
    }
    else
    {
      struct sockaddr_in sa {};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(0U); // Use an available ephemeral port.
      sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      if(bind(socket_fd, static_cast<struct sockaddr*>(static_cast<void*>(&sa)),
        sizeof(struct sockaddr_in)) < 0)
      { 
        int errno_value {errno};
        close(socket_fd);
        ADD_FAILURE() << "A call to bind failed in case 2." << '\n' 
          << strerror(errno_value);
      }
      else
      {
        EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1),
          std::exception);
        close(socket_fd);
      }
    }
  }
    
  // Case 3: listening_descriotor refers to a socket which not set to the
  // listening state.
  {
    int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in case 3." << '\n' 
        << strerror(errno);
    }
    else
    {
      struct sockaddr_in sa {};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(0U); // Use an available ephemeral port.
      sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      if(bind(socket_fd, static_cast<struct sockaddr*>(static_cast<void*>(&sa)),
        sizeof(struct sockaddr_in)) < 0)
      { 
        int errno_value {errno};
        close(socket_fd);
        ADD_FAILURE() << "A call to bind failed in case 3." << '\n' 
          << strerror(errno_value);
      }
      else
      {
        EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1),
          std::exception);
        close(socket_fd);
      }
    }
  }

  // Case 4: The socket is of domain AF_INET and only IPv6 addresses are
  // present.
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "::1", 1) < 0)
      ADD_FAILURE() << "setenv failed in case 4." << '\n' << strerror(errno);
    else
    {
      int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
      if(socket_fd < 0)
      {
        ADD_FAILURE() << "A call to socket failed in case 4." << '\n' 
          << strerror(errno);
      }
      else
      {
        if(listen(socket_fd, 5) < 0)
        {
          int errno_value {errno};
          close(socket_fd);
          ADD_FAILURE() << "A call to listen failed in case 4." << '\n'
            << strerror(errno_value);
        }
        else
        {
          EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1),
            std::exception);
          close(socket_fd);
        }
      }
    }
  }

  // Case 5: 





















}
