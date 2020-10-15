#include "test/include/fcgi_si_testing_utilities.h"

#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <tuple>
#include <vector>

#include "external/a_component_testing/gtest/include/a_component_testing_gtest_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_utilities.h"

namespace a_component {
namespace fcgi {
namespace test {
namespace test {

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
  // 10) Value of the returned number of headers.
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
  // 1) PopulateHeader
  //
  // Other modules whose testing depends on this module:
  // 1) EncodeNameValuePairs
  // 2) PartitionByteSequence

  // Create a temporary file for use during this test.
  // BAZEL DEPENDENCY
  int temp_fd {};
  testing::gtest::GTestFatalCreateBazelTemporaryFile(&temp_fd);

  // Case 1: Small file descriptor value, a single header with zero content
  // length and no padding.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;
    
    std::uint8_t local_header[FCGI_HEADER_LEN];
    PopulateHeader(local_header, FcgiType::kFCGI_DATA,
      1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(local_header),
      FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_TRUE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(1U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ(std::vector<std::uint8_t> {}, 
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 2: Small file descriptor value, single record with non-zero content
  // length, no padding, and no terminal empty record.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;
    
    // Populate an FCGI_BEGIN_REQUEST record.
    std::uint8_t record[2*FCGI_HEADER_LEN] = {};
    PopulateHeader(record,
      FcgiType::kFCGI_BEGIN_REQUEST, std::uint16_t(-1), 
      FCGI_HEADER_LEN, 0);
    // The second set of eight bytes (FCGI_HEADER_LEN) is zero except for
    // the low-order byte of the role.
    record[FCGI_HEADER_LEN + kBeginRequestRoleB0Index]
      = FCGI_RESPONDER;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      2*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
    if(write_return < 2*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_BEGIN_REQUEST, std::uint16_t(-1))};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ(1U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {0,1,0,0,0,0,0,0}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 3: As in 2, but with an unaligned record.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;
    
    // Populate an FCGI_BEGIN_REQUEST record.
    std::uint8_t record[FCGI_HEADER_LEN + 4] = {};
    PopulateHeader(record,
      FcgiType::kFCGI_PARAMS, 0, 4,
      0);
    // The second set of eight bytes (FCGI_HEADER_LEN) is zero except for
    // the low-order byte of the role.
    record[FCGI_HEADER_LEN]     = 1;
    record[FCGI_HEADER_LEN + 1] = 1;
    record[FCGI_HEADER_LEN + 2] = 'a';
    record[FCGI_HEADER_LEN + 3] = 'b';
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      FCGI_HEADER_LEN + 4)) == -1 && errno == EINTR)
    if(write_return < (FCGI_HEADER_LEN + 4))
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_PARAMS, 0)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ(1U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,1,'a','b'}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 4: As in 2, but with padding. (Regular discrete record.)
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;
    
    std::uint8_t record[2*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 10, 5, 3);
    record[FCGI_HEADER_LEN]     = 1;
    record[FCGI_HEADER_LEN + 1] = 2;
    record[FCGI_HEADER_LEN + 2] = 3;
    record[FCGI_HEADER_LEN + 3] = 4;
    record[FCGI_HEADER_LEN + 4] = 5;
    // Padding was uninitialized.
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      2*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 2*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 10)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ(1U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1, 2, 3, 4, 5}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 5: Small file descriptor value, a record with non-zero content
  // length, padding, and a terminal empty record. (A single-record, terminated
  // stream.)
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[3*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 10, 5, 3);
    record[FCGI_HEADER_LEN]     = 1;
    record[FCGI_HEADER_LEN + 1] = 2;
    record[FCGI_HEADER_LEN + 2] = 3;
    record[FCGI_HEADER_LEN + 3] = 4;
    record[FCGI_HEADER_LEN + 4] = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 10, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      3*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 3*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 10)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_TRUE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(2U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1, 2, 3, 4, 5}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to ::fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 6: Small file descriptor value, multiple records with non-zero
  // content lengths and padding as necessary to reach a multiple of eight. Not
  // terminated.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;
    
    std::uint8_t record[6*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      6*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 6*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result))  <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result))  <<
        "Record alignment flag.";
      EXPECT_EQ(3U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 7: As in 5, but terminated. (A typical, multi-record stream sequence.)
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[7*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    PopulateHeader(record + 6*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      7*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_TRUE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_TRUE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_TRUE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(4U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 8: A bad file descriptor as an unrecoverable read error.
  do
  {
    // A file descriptor which is not allocated is generated by calling
    // dup on the temporary file and adding 1000. It is assumed that no
    // file descriptor will be allocated with this value.
    int file_descriptor_limit {dup(temp_fd)};
    if(file_descriptor_limit == -1)
    {
      ADD_FAILURE() << "A call to dup failed.";
      break;
    }
    auto result {ExtractContent(file_descriptor_limit + 1000,
      FcgiType::kFCGI_BEGIN_REQUEST, 1)};
    EXPECT_FALSE(std::get<0>(result));
    close(file_descriptor_limit);
  } while(false);

  // Case 9: As in 6, but with a header type error in the middle.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[7*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_PARAMS, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    PopulateHeader(record + 6*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      7*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(2U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 10: As in 6, but with a header FastCGI request identifier error in the
  // middle.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[7*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 2, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    PopulateHeader(record + 6*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 0, 0);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      7*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(2U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 11: A header with a non-zero content length and non-zero padding, but
  // no more data. (An incomplete record.) A small file descriptor value.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_PARAMS, 1, 50, 6);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_PARAMS, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(1U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 12: A small file descriptor value and a sequence of records with
  // non-zero content lengths and with padding. The sequence ends with a header
  // with a non-zero content length and padding but no more data.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[7*FCGI_HEADER_LEN];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    PopulateHeader(record + 6*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 38, 2);
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      7*FCGI_HEADER_LEN)) == -1 && errno == EINTR)
      continue;
    if(write_return < 7*FCGI_HEADER_LEN)
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(4U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 13: A small file descriptor value and a sequence of records with
  // non-zero content lengths and with padding. The sequence ends with a header
  // that is not complete.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[6*FCGI_HEADER_LEN + 3];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    // Add values for the incomplete header.
    record[6*FCGI_HEADER_LEN + kHeaderVersionIndex]     =
      FCGI_VERSION_1;
    record[6*FCGI_HEADER_LEN + kHeaderTypeIndex]        =
      static_cast<std::uint8_t>(FcgiType::kFCGI_DATA);
    record[6*FCGI_HEADER_LEN + kHeaderRequestIDB1Index] =
      0;
    ssize_t write_return {write(temp_fd, static_cast<void*>(record),
      6*FCGI_HEADER_LEN + 3)};
    if(write_return < (6*FCGI_HEADER_LEN + 3))
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(3U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 14:  As in 11, but with a final record for which the content has a
  // length that is less than the content length given in the final header. No
  // additional data is present.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[7*FCGI_HEADER_LEN + 1];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    PopulateHeader(record + 6*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 50, 6);
    record[7*FCGI_HEADER_LEN]     = 16;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      7*FCGI_HEADER_LEN + 1)) == -1 && errno == EINTR)
      continue;
    if(write_return < (7*FCGI_HEADER_LEN + 1))
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(4U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}),
        std::get<5>(extract_content_result)) << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);

  // Case 15: As in 11, but with a final record whose padding has a length that
  // is less than the padding length given in the final header. No additional
  // data is present.
  do
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd))
      break;

    std::uint8_t record[7*FCGI_HEADER_LEN + 5];
    PopulateHeader(record, FcgiType::kFCGI_DATA, 1, 5, 3);
    record[FCGI_HEADER_LEN]       = 1;
    record[FCGI_HEADER_LEN + 1]   = 2;
    record[FCGI_HEADER_LEN + 2]   = 3;
    record[FCGI_HEADER_LEN + 3]   = 4;
    record[FCGI_HEADER_LEN + 4]   = 5;
    // Padding was uninitialized.
    PopulateHeader(record + 2*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[3*FCGI_HEADER_LEN]     = 6;
    record[3*FCGI_HEADER_LEN + 1] = 7;
    record[3*FCGI_HEADER_LEN + 2] = 8;
    record[3*FCGI_HEADER_LEN + 3] = 9;
    record[3*FCGI_HEADER_LEN + 4] = 10;
    // Padding was uninitialized.
    PopulateHeader(record + 4*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[5*FCGI_HEADER_LEN]     = 11;
    record[5*FCGI_HEADER_LEN + 1] = 12;
    record[5*FCGI_HEADER_LEN + 2] = 13;
    record[5*FCGI_HEADER_LEN + 3] = 14;
    record[5*FCGI_HEADER_LEN + 4] = 15;
    PopulateHeader(record + 6*FCGI_HEADER_LEN,
      FcgiType::kFCGI_DATA, 1, 5, 3);
    record[7*FCGI_HEADER_LEN]     = 16;
    record[7*FCGI_HEADER_LEN + 1] = 17;
    record[7*FCGI_HEADER_LEN + 2] = 18;
    record[7*FCGI_HEADER_LEN + 3] = 19;
    record[7*FCGI_HEADER_LEN + 4] = 20;
    ssize_t write_return {0};
    while((write_return = write(temp_fd, static_cast<void*>(record),
      7*FCGI_HEADER_LEN + 5)) == -1 && errno == EINTR)
      continue;
    if(write_return < (7*FCGI_HEADER_LEN + 5))
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
    auto extract_content_result {ExtractContent(temp_fd,
      FcgiType::kFCGI_DATA, 1)};
    if(std::get<0>(extract_content_result))
    {
      EXPECT_FALSE(std::get<1>(extract_content_result)) <<
        "Header and section errors.";
      EXPECT_FALSE(std::get<2>(extract_content_result)) <<
        "Sequence termination flag.";
      EXPECT_FALSE(std::get<3>(extract_content_result)) <<
        "Record alignment flag.";
      EXPECT_EQ(4U, std::get<4>(extract_content_result)) <<
        "Incorrect number of records.";
      EXPECT_EQ((std::vector<std::uint8_t> {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20}), std::get<5>(extract_content_result))
        << "Content byte sequence.";
    }
    else
    {
      ADD_FAILURE() << "A call to fcgi_si_test::ExtractContent "
        "encountered a read error.";
      break;
    }
  } while(false);
  close(temp_fd);
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
