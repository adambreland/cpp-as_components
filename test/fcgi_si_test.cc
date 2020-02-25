#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "fcgi_si.h"

TEST(Utility, EncodeFourByteLength)
{
  // Testing explanation:
  // Examined properties:
  // 1) Byte length and value of the length argument.
  // 2) The type of the iterator used in the instantiation of the template.
  //
  // The following cases are tested:
  // 1) A random value within the acceptable values of uint32_t.
  // 2) A random value as above, but using a non-pointer iterator.
  // 3) Minimum value: 128.
  // 4) A value which requires two bytes to encode: 256.
  // 5) A value which requires three bytes to encode: 1UL << 16.
  // 6) One less than the maximum value.
  // 7) The maximum value.
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

  // Random value: 2,128,547
  fcgi_si::EncodeFourByteLength(2128547, header_array);
  EXPECT_EQ(128, header_array[0]);
  EXPECT_EQ(32, header_array[1]);
  EXPECT_EQ(122, header_array[2]);
  EXPECT_EQ(163, header_array[3]);

  // Random value with back_insert_iterator.
  std::vector<uint8_t> byte_seq {};
  fcgi_si::EncodeFourByteLength(2128547, std::back_inserter(byte_seq));
  EXPECT_EQ(128, byte_seq[0]);
  EXPECT_EQ(32, byte_seq[1]);
  EXPECT_EQ(122, byte_seq[2]);
  EXPECT_EQ(163, byte_seq[3]);

  // Minimum value, 128.
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

  // Requires two bytes.
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

  // Requires three bytes.
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

  // Maximum value less one.
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

  // Maximum value
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

  // Random value.
  fcgi_si::EncodeFourByteLength(2128547, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(2128547, length);

  // Minimum length.
  fcgi_si::EncodeFourByteLength(128, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(128, length);

  // Requires two bytes.
  fcgi_si::EncodeFourByteLength(256, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(256, length);

  // Requires three bytes.
  fcgi_si::EncodeFourByteLength(1UL << 16, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ(1UL << 16, length);

  // Maximum value less one.
  fcgi_si::EncodeFourByteLength((1UL << 31) - 1 - 1, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ((1UL << 31) - 1 - 1, length);

  // Maximum value.
  fcgi_si::EncodeFourByteLength((1UL << 31) - 1, seq);
  length = fcgi_si::ExtractFourByteLength(seq);
  EXPECT_EQ((1UL << 31) - 1, length);
}

TEST(Utility, ExtractContent)
{
  // Testing explanation
  // Examined properties:
  // 1) Content byte sequence value.
  // 2) Value of the file descriptor.
  // 3) Record type: discrete or stream.
  // 4) For stream types, presence and absence of a terminal record with a
  //    content length of zero.
  // 5) Presence or absence of padding.
  // 6) Presence or absence of an unrecoverable read error (such as a bad
  //    file descriptor).
  // 7) Presence or absence of a header error. Two errors categories: type
  //    and FastCGI request identifier.
  // 8) Presence or absence of an incomplete section. Three sections produce
  //    three error categories.
  //
  // Test cases:
  //  1) File descriptor equal to zero, empty file.
  //  2) Small file descriptor value, single header with a zero content length
  //     and no padding. (Equivalent to an empty record stream termination.)
  //  3) Small file descriptor value, single record with non-zero content
  //     length, no padding, and no terminal empty record.
  //  4) As in 3, but with padding. (Regular discrete record.)
  //  5) Small file descriptor value, a record with non-zero content length,
  //     padding, and a terminal empty record. (A single-record, terminated
  //     stream.)
  //  6) Small file descriptor value, multiple records with non-zero content
  //     lengths and padding as necessary to reach a multiple of eight. Not
  //     terminated.
  //  7) As in 6, but terminated. (A typical, multi-record stream sequence.)
  //  8) A bad file descriptor as an unrecoverable read error.
  //  9) As in 7, but with a header type error in the middle.
  // 10) As in 7, but with a header FastCGI request identifier error in the
  //     middle.
  // 11) A header with a non-zero content length and non-zero padding but
  //     no more data. (An incomplete record.) A small file descriptor value.
  // 12) A small file descriptor value and a sequence of records with non-zero
  //     content lengths and with padding. The sequence ends with a header with
  //     a non-zero content length and padding but no more additional data.
  // 13) A small file descriptor value and a sequence of records with non-zero
  //     content lengths and with padding. The sequence ends with a header that
  //     is not complete.
  // 14) As in 12, but with a final record for which the content has a length
  //     that is less than the content length given in the final header. No
  //     additional data is present.
  // 15) As in 12, but with a final record whose padding has a length that is
  //     less than the padding length given in the final header. No additional
  //     data is present.
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module:
  // 1) fcgi_si::EncodeNameValuePairs

  // Case 1: File descriptor equal to zero, empty file.
  bool case_single_iteration {true};
  while(case_single_iteration)
  {
    // Enforce a single iteration.
    case_single_iteration = false;
    // Duplicate stdin so that it can be restored later.
    int saved_stdin {dup(STDIN_FILENO)};
    if(saved_stdin == -1)
    {
      ADD_FAILURE() << "A call to dup failed.";
      break;
    }
    if(close(STDIN_FILENO) == -1)
    {
      ADD_FAILURE() << "A call to close failed.";
      int restore {dup2(saved_stdin, STDIN_FILENO)};
      close(saved_stdin);
      if(restore == -1)
        FAIL() << "A call to dup2 failed. stdin could not be restored.";
      break;
    }
    // Create a temporary file with no data.
    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      int restore {dup2(saved_stdin, STDIN_FILENO)};
      close(saved_stdin);
      if(restore == -1)
        FAIL() << "A call to dup2 failed. stdin could not be restored.";
      break;
    }

    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, 1)};
    close(temp_fd);
    int restore {dup2(saved_stdin, STDIN_FILENO)};
    close(saved_stdin);
    if(restore == -1)
      FAIL() << "A call to dup2 failed. stdin could not be restored.";

    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_EQ(std::vector<uint8_t> {}, std::get<3>(extract_content_result)) <<
        "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 2: Small file descriptor value, a single header with zero content
  // length and no padding.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
    uint8_t local_header[fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(local_header, fcgi_si::FCGIType::kFCGI_DATA,
      1, 0, 0);
    ssize_t write_return {write(temp_fd, (void*)local_header,
      fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result));
      EXPECT_TRUE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {}), std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 3: Small file descriptor value, single record with non-zero content
  // length, no padding, and no terminal empty record.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
    // Populate an FCGI_BEGIN_REQUEST record.
    uint8_t record[2*fcgi_si::FCGI_HEADER_LEN] = {};
    fcgi_si::PopulateHeader(record,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, uint16_t(-1), fcgi_si::FCGI_HEADER_LEN,
      0);
    // The second set of eight bytes (FCGI_HEADER_LEN) is zero except for
    // the low-order byte of the role.
    record[fcgi_si::FCGI_HEADER_LEN + fcgi_si::kBeginRequestRoleB0Index]
      = fcgi_si::FCGI_RESPONDER;
    ssize_t write_return {write(temp_fd, (void*)record,
      2*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 2*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, uint16_t(-1))};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {0,1,0,0,0,0,0,0}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 4: As in 3, but with padding. (Regular discrete record.)
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
    uint8_t record[2*fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_DATA, 10, 5, 3);
    record[fcgi_si::FCGI_HEADER_LEN]     = 1;
    record[fcgi_si::FCGI_HEADER_LEN + 1] = 2;
    record[fcgi_si::FCGI_HEADER_LEN + 2] = 3;
    record[fcgi_si::FCGI_HEADER_LEN + 3] = 4;
    record[fcgi_si::FCGI_HEADER_LEN + 4] = 5;
    // Padding was uninitialized.
    ssize_t write_return {write(temp_fd, (void*)record,
      2*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 2*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 10)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1, 2, 3, 4, 5}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
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

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      3*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 3*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 10)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result));
      EXPECT_TRUE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1, 2, 3, 4, 5}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
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

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      6*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 6*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 7: As in 6, but terminated. (A typical, multi-record stream sequence.)
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result));
      EXPECT_TRUE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 8: A bad file descriptor as an unrecoverable read error.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;
    int file_descriptor_limit {dup(0)};
    if(file_descriptor_limit == -1)
    {
      ADD_FAILURE() << "A call to dup failed.";
      break;
    }
    auto result {fcgi_si::ExtractContent(file_descriptor_limit + 1,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, 1)};
    EXPECT_FALSE(std::get<0>(result));
  }

  // Case 9: As in 7, but with a header type error in the middle.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 10: As in 7, but with a header FastCGI request identifier error in the
  // middle.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
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

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
    uint8_t record[fcgi_si::FCGI_HEADER_LEN];
    fcgi_si::PopulateHeader(record, fcgi_si::FCGIType::kFCGI_PARAMS, 1, 50, 6);
    ssize_t write_return {write(temp_fd, (void*)record,
      fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_PARAMS, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
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

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN)};
    if(write_return == -1 || write_return < 7*fcgi_si::FCGI_HEADER_LEN)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
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

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    if(write_return == -1 || write_return < (6*fcgi_si::FCGI_HEADER_LEN + 3))
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 14:  As in 12, but with a final record for which the content has a
  // length that is less than the content length given in the final header. No
  // additional data is present.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN + 1)};
    if(write_return == -1 || write_return < (7*fcgi_si::FCGI_HEADER_LEN + 1))
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15, 16}),
        std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }

  // Case 15: As in 12, but with a final record whose padding has a length that
  // is less than the padding length given in the final header. No additional
  // data is present.
  case_single_iteration = true;
  while(case_single_iteration)
  {
    case_single_iteration = false;

    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
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
    ssize_t write_return {write(temp_fd, (void*)record,
      7*fcgi_si::FCGI_HEADER_LEN + 5)};
    if(write_return == -1 || write_return < (7*fcgi_si::FCGI_HEADER_LEN + 5))
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to write failed or returned a short-count.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_DATA, 1)};
    close(temp_fd);
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result));
      EXPECT_FALSE(std::get<2>(extract_content_result));
      EXPECT_EQ((std::vector<uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20}), std::get<3>(extract_content_result));
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si::ExtractContent "
        "encountered a read error.";
      break;
    }
  }
}

TEST(Utility, ProcessBinaryNameValuePairs)
{
  // Testing explanation
  // Examined properties:
  // 1) Number of name-value pairs. In particular, one pair or more than one.
  // 2) Number of bytes required to encode the name or value. From the
  //    encoding format, one byte or four bytes.
  // 3) Presence or absence of data. I.e. an empty name or value.
  // 4) Improperly encoded data.
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
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module:
  // 1) fcgi_si::EncodeNameValuePairs

  using NameValuePair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

  // Nothing to process.
  EXPECT_EQ(std::vector<NameValuePair> {},
    fcgi_si::ProcessBinaryNameValuePairs(0, nullptr));

  uint8_t test_value {0};
  EXPECT_EQ(std::vector<NameValuePair> {},
    fcgi_si::ProcessBinaryNameValuePairs(0, &test_value));
  EXPECT_EQ(0, test_value);

  // Single name-value pair. (1 byte, 1 byte) for lengths. Empty name and value.
  const char* empty_name_ptr {""};
  const char* empty_value_ptr {""};
  NameValuePair empty_empty_nv_pair {{empty_name_ptr, empty_name_ptr},
    {empty_value_ptr, empty_value_ptr}};
  std::vector<uint8_t> encoded_nv_pair {};
  encoded_nv_pair.push_back(0);
  encoded_nv_pair.push_back(0);
  std::vector<NameValuePair> result {fcgi_si::ProcessBinaryNameValuePairs(
    encoded_nv_pair.size(), encoded_nv_pair.data())};
  EXPECT_EQ(result[0], empty_empty_nv_pair);

  // Single name-value pair. (1 byte, 1 byte) for lengths. Empty value.
  encoded_nv_pair.clear();
  const char* name_ptr {"Name"};
  NameValuePair name_empty_nv_pair {{name_ptr, name_ptr + 4},
    {empty_value_ptr, empty_value_ptr}};
  encoded_nv_pair.push_back(4);
  encoded_nv_pair.push_back(0);
  for(auto c : name_empty_nv_pair.first)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result[0], name_empty_nv_pair);

  // Single name-value pair. (1 byte, 1 byte) for lengths.
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
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result[0], one_one_nv_pair);

  // Single name-value pair, (1 byte, 4 bytes) for lengths.
  std::vector<uint8_t> four_value_vector(128, 'a');
  NameValuePair one_four_nv_pair {{name_ptr, name_ptr + 4}, four_value_vector};
  encoded_nv_pair.clear();
  encoded_nv_pair.push_back(4);
  fcgi_si::EncodeFourByteLength(128, std::back_inserter(encoded_nv_pair));
  for(auto c : one_four_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : one_four_nv_pair.second)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result[0], one_four_nv_pair);

  // Single name-value pair, (4 byte, 1 bytes) for lengths.
  std::vector<uint8_t> four_name_vector(256, 'b');
  NameValuePair four_one_nv_pair {four_name_vector, {value_ptr, value_ptr + 5}};
  encoded_nv_pair.clear();
  fcgi_si::EncodeFourByteLength(256, std::back_inserter(encoded_nv_pair));
  encoded_nv_pair.push_back(5);
  for(auto c : four_one_nv_pair.first)
    encoded_nv_pair.push_back(c);
  for(auto c : four_one_nv_pair.second)
    encoded_nv_pair.push_back(c);
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result[0], four_one_nv_pair);

  // Multiple name-value pairs with names and values that need one and four
  // byte lengths. Also includes an empty value.
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
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result, pairs);

  // As above, but with the empty value in the middle.
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
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result, pairs);

  // An incomplete encoding. A single name and value is present. Extra
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
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result, std::vector<NameValuePair> {});

  // Too many bytes were specified for the last name, but the first name-
  // value pair was correct. An empty vector should still be returned.
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
  result = fcgi_si::ProcessBinaryNameValuePairs(encoded_nv_pair.size(),
    encoded_nv_pair.data());
  EXPECT_EQ(result, std::vector<NameValuePair> {});
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
  // 2) Specific values for name and value.
  //    a) The presence of empty names and values.
  //    b) The presence of duplicate names. Duplicates should be present to
  //       ensure that the implementation can handle accidental duplicate names.
  //    c) Names and values which have a length large enough to require
  //       four bytes to be encoded in the FastCGI name-value format.
  // 3) The need for padding. Records should be present which will likely
  //    require padding if the suggested 8-byte alignment condition is met.
  // 4) Number of records. Sequences which require more than one full record
  //    should be present.
  // 5) Presence of a name or value whose length exceeds the maximum size
  //    allowed by the FastCGI protocol.
  // 6) Large and small FCGI_id values. In particular, values greater than 255.
  // 7) A large number of sequence elements. In particular, larger than
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
  //  9) As in 6, but padding is required.
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
  // 16) As in 14, but for value instead of name. (Invalid input.)
  // 17) More than 1024 name-value pairs. Encoding should cause the returned
  //     offset value to be zero.
  // 18) More than 1024 name-value pairs. Encoding should cauuse the returned
  //     offset value to be non-zero.
  //
  // Modules which testing depends on:
  // 1) fcgi_si::ExtractContent
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
  bool case_single_iteration {true};
  while(case_single_iteration)
  {
    case_single_iteration = false;

    std::vector<NameValuePair> nv_pair_no_pad {{{'n','a','m','e'}, {'v','l'}}};
    auto encoded_result = fcgi_si::EncodeNameValuePairs(nv_pair_no_pad.begin(),
      nv_pair_no_pad.end(), fcgi_si::FCGIType::kFCGI_PARAMS, 1, 0);
    if(!std::get<0>(encoded_result))
    {
      ADD_FAILURE() << "EncodeNameValuePairs encountered an error reported by "
        "std::get<0>.";
      break;
    }
    size_t total_to_write {std::get<1>(encoded_result)};
    if(total_to_write != 16)
    {
      ADD_FAILURE() << "fcgi_si::EncodeNameValuePairs indicated an incorrect "
        "number of bytes to write as reported by std::get<1>. Value: "
        << total_to_write;
      break;
    }
    // std::get<2>(encoded_result) is used in a call to writev.
    // std::get<3>(encoded_result) is implciitly used in a call to writev.
    if(std::get<4>(encoded_result) != 0)
    {
      ADD_FAILURE() << "EncodeNameValuePairs returned a non-zero offset as "
        "reported by std::get<4>.";
      break;
    }
    if(std::get<5>(encoded_result) != nv_pair_no_pad.end())
    {
      ADD_FAILURE() << "EncodeNameValuePairs did not process all pairs as "
        "reported by std::get<5>.";
      break;
    }
    int temp_fd {open(".", O_RDWR | O_TMPFILE)};
    if(temp_fd == -1)
    {
      ADD_FAILURE() << "A call to open failed to make a temporary file.";
      break;
    }
    ssize_t write_return {0};
    while((write_return = writev(temp_fd, std::get<2>(encoded_result).data(),
      std::get<2>(encoded_result).size())) == -1 && errno == EINTR)
      continue;
    if(write_return != total_to_write)
    {
      ADD_FAILURE() << "A call to writev did not write all bytes requested.";
      break;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to lseek failed.";
      break;
    }
    auto extract_content_result {fcgi_si::ExtractContent(temp_fd,
      fcgi_si::FCGIType::kFCGI_PARAMS, 1)};
    if(!std::get<0>(extract_content_result))
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to fcgi_si::ExtractContent encountered an "
        "unrecoverable read error.";
      break;
    }
    if(!std::get<1>(extract_content_result)
      || std::get<2>(extract_content_result))
    {
      close(temp_fd);
      ADD_FAILURE() << "A call to fcgi_si::ExtractContent returned an "
      "unexpected truth value as reported by std::get<1> and std::get<2>.";
      break;
    }
    std::vector<NameValuePair> nv_pair_result {
      fcgi_si::ProcessBinaryNameValuePairs(std::get<3>(
      extract_content_result).size(), std::get<3>(
      extract_content_result).data())};
    EXPECT_EQ(nv_pair_no_pad, nv_pair_result);
  }



}



// TEST(Utility, uint32_tToUnsignedCharacterVector)
// {
//   //
// }
