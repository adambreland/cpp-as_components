#include "include/utility.h"

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/protocol_constants.h"
#include "test/fcgi_si_testing_utilities.h"

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
  // 2) Fcgi_id value (0, 1, larger than 1 but less than the maximum,
  //    the maximum value).
  // 3) content_length value (0, 1, larger than 1 but less than the maximum,
  //    the maximum value).
  // 4) padding_length value (0, 1, larger than 1 but less than the maximum,
  //    the maximum value).
  //
  // Test cases:
  //  1) type:           fcgi_si::FcgiType::kFCGI_BEGIN_REQUEST
  //     Fcgi_id:        0
  //     content_length: 0
  //     padding_length: 0
  //  2) type:           fcgi_si::FcgiType::kFCGI_ABORT_REQUEST
  //     Fcgi_id:        1
  //     content_length: 1
  //     padding_length: 1
  //  3) type:           fcgi_si::FcgiType::kFCGI_END_REQUEST
  //     Fcgi_id:        10
  //     content_length: 10
  //     padding_length: 10
  //  4) type:           fcgi_si::FcgiType::kFCGI_PARAMS
  //     Fcgi_id:        2^16 - 1 (which is equal to uint16_t(-1))
  //     content_length: 2^16 - 1
  //     padding_length: 255 (which is equal to uint8_t(-1))
  //  5) type:           fcgi_si::FcgiType::kFCGI_STDIN
  //     Fcgi_id:        1
  //     content_length: 1000
  //     padding_length: 0
  //  6) type:           fcgi_si::FcgiType::kFCGI_STDOUT
  //     Fcgi_id:        1
  //     content_length: 250
  //     padding_length: 2
  //  7) type:           fcgi_si::FcgiType::kFCGI_STDERR
  //     Fcgi_id:        1
  //     content_length: 2
  //     padding_length: 6
  //  8) type:           fcgi_si::FcgiType::kFCGI_DATA
  //     Fcgi_id:        2^16 - 1
  //     content_length: 2^16 - 1
  //     padding_length: 7
  //  9) type:           fcgi_si::FcgiType::kFCGI_GET_VALUES
  //     Fcgi_id:        0
  //     content_length: 100
  //     padding_length: 4
  // 10) type:           fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT
  //     Fcgi_id:        0
  //     content_length: 100
  //     padding_length: 0
  // 11) type:           fcgi_si::FcgiType::kFCGI_UNKNOWN_TYPE
  //     Fcgi_id:        1
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
    fcgi_si::FcgiType type,
    uint16_t Fcgi_id,
    uint16_t content_length,
    uint8_t padding_length
  )->void
  {
    fcgi_si::PopulateHeader(local_header.data(), type, Fcgi_id, content_length,
      padding_length);

    expected_result[0] = fcgi_si::FCGI_VERSION_1;
    expected_result[1] = static_cast<uint8_t>(type);
    expected_result[2] = (Fcgi_id >> 8);
    expected_result[3] = Fcgi_id;        // implicit truncation.
    expected_result[4] = (content_length >> 8);
    expected_result[5] = content_length; // implicit truncation.
    expected_result[6] = padding_length;
    expected_result[7] = 0;

    EXPECT_EQ(local_header, expected_result) << message;
  };

  // Case 1
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_BEGIN_REQUEST};
    uint16_t Fcgi_id {0};
    uint16_t content_length {0};
    uint8_t padding_length {0};

    std::string message {"Case 1, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 2
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_ABORT_REQUEST};
    uint16_t Fcgi_id {1};
    uint16_t content_length {1};
    uint8_t padding_length {1};

    std::string message {"Case 2, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 3
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_END_REQUEST};
    uint16_t Fcgi_id {10};
    uint16_t content_length {10};
    uint8_t padding_length {10};

    std::string message {"Case 3, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 4
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_PARAMS};
    uint16_t Fcgi_id(-1);
    uint16_t content_length(-1);
    uint8_t padding_length(-1);

    std::string message {"Case 4, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 5
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_STDIN};
    uint16_t Fcgi_id {1};
    uint16_t content_length {1000};
    uint8_t padding_length {0};

    std::string message {"Case 5, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 6
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_STDOUT};
    uint16_t Fcgi_id {1};
    uint16_t content_length {250};
    uint8_t padding_length {2};

    std::string message {"Case 6, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 7
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_STDERR};
    uint16_t Fcgi_id {1};
    uint16_t content_length {2};
    uint8_t padding_length {6};

    std::string message {"Case 7, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 8
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_DATA};
    uint16_t Fcgi_id(-1);
    uint16_t content_length(-1);
    uint8_t padding_length {7};

    std::string message {"Case 8, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 9
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_GET_VALUES};
    uint16_t Fcgi_id {0};
    uint16_t content_length {100};
    uint8_t padding_length {4};

    std::string message {"Case 9, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 10
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT};
    uint16_t Fcgi_id {0};
    uint16_t content_length {100};
    uint8_t padding_length {0};

    std::string message {"Case 10, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 11
  {
    fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_UNKNOWN_TYPE};
    uint16_t Fcgi_id {1};
    uint16_t content_length {8};
    uint8_t padding_length {8};

    std::string message {"Case 11, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
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
  // 7) Large and small Fcgi_id values. In particular, values greater than 255.
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
  //  4) As in 3, but with a Fcgi_id larger than 255.
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
    fcgi_si::FcgiType type,
    uint16_t Fcgi_id,
    std::size_t offset_argument,
    bool expect_processing_error,
    int returned_offset_test_value,
    std::vector<NameValuePair>::const_iterator expected_pair_it
  )->void
  {
    auto encoded_result = fcgi_si::EncodeNameValuePairs(pair_sequence.begin(),
      pair_sequence.end(), type, Fcgi_id, offset_argument);
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
      fcgi_si::FcgiType::kFCGI_PARAMS, Fcgi_id)};
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
      fcgi_si::FcgiType::kFCGI_PARAMS, 1, 0)};
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
      1,
      0,
      false,
      0,
      pair_sequence.cend()
    );
  }

  // Case 4: As in 3, but with a Fcgi_id larger than 255.
  {
    std::string message {"Case 4, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {{{'n','a','m','e'},
      {'v','a','l','u','e'}}};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
      fcgi_si::FcgiType::kFCGI_PARAMS,
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
  //   fcgi_si::FcgiType type {fcgi_si::FcgiType::kFCGI_PARAMS};
  //   uint16_t Fcgi_id {1};
  //   std::size_t offset_argument {0};
  //   bool expect_processing_error {true};
  //   int returned_offset_test_value {0};
  //   std::vector<NameValuePair>::const_iterator expected_pair_it
  //     {pair_sequence.cbegin() + 3};
  //   while(true)
  //   {
  //     auto encoded_result = fcgi_si::EncodeNameValuePairs(pair_sequence.begin(),
  //       pair_sequence.end(), type, Fcgi_id, offset_argument);
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
  //       fcgi_si::FcgiType::kFCGI_PARAMS, Fcgi_id)};
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
    do
    {
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
          many_pairs.end(), fcgi_si::FcgiType::kFCGI_PARAMS, 1, offset);
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
        fcgi_si::FcgiType::kFCGI_PARAMS, 1)};
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
    } while(false);
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
  // 2) Value of Fcgi_id: equal to 0, greater than zero but less than the
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
  //    type == fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT, Fcgi_id == 0.
  // 2) std::distance(end_iter, begin_iter) == 3,
  //    type == fcgi_si::FcgiType::kFCGI_STDIN, Fcgi_id == 1.
  // 3) std::distance(end_iter, begin_iter) == 25,
  //    type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 65535 ==
  //    std::numeric_limits<std::uint16_t>::max().
  // 4) std::distance(end_iter, begin_iter) == 8,
  //    type == static_cast<fcgi_si::FcgiType>(20), Fcgi_id == 3.
  // 5) std::distance(end_iter, begin_iter) == 65528,
  //    type == fcgi_si::FcgiType::kFCGI_PARAMS, Fcgi_id == 300.
  // 6) std::distance(end_iter, begin_iter) == 2^25,
  //    type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
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
    std::vector<std::uint8_t>& content_seq, 
    fcgi_si::FcgiType type,
    std::uint16_t Fcgi_id
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
    std::vector<std::uint8_t>::iterator begin_iter {content_seq.begin()};
    std::vector<std::uint8_t>::difference_type returned_length {0};
    do
    {
      std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
        std::size_t, std::vector<std::uint8_t>::iterator> pr
        {fcgi_si::PartitionByteSequence(begin_iter, content_seq.end(), 
        type, Fcgi_id)};

      ssize_t writev_return {writev(temp_descriptor, std::get<1>(pr).data(),
        std::get<1>(pr).size())};
      if((writev_return < 0) || (static_cast<std::size_t>(writev_return) < 
        std::get<2>(pr)))
      {
        ADD_FAILURE() << "A call to writev failed." << '\n' << message;
        return;
      }      
      begin_iter = std::get<3>(pr);
    } while(begin_iter != content_seq.cend());

    // Extract the content and validate.
    lseek_return = lseek(temp_descriptor, 0, SEEK_SET);
    if(lseek_return < 0)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }
    std::tuple<bool, bool, bool, bool, std::vector<uint8_t>> ecr
      {fcgi_si_test::ExtractContent(temp_descriptor, type, Fcgi_id)};
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
        '\n' << message;
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
    // // Check for equality of the extracted byte sequence and the prefix of the
    // // content sequence indicated by the information returned by
    // // PartitionByteSequence.
    // if((returned_length > content_seq.size())       ||
    //    (returned_length != std::get<4>(ecr).size()) ||
    //    (std::mismatch(content_seq.begin(), std::get<3>(pr), 
    //                   std::get<4>(ecr).begin())
    //     != std::make_pair(std::get<3>(pr), 
    //                       std::get<4>(ecr).begin() + returned_length)))
    // {
    //   ADD_FAILURE() << "The extracted byte sequence did not match the prefix." 
    //     << '\n' << message;
    //   return;
    // }

    return;
  };

  // // Case 1: begin_iter == end_iter, 
  // // type == fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT, Fcgi_id == 0.
  // {
  //   std::string message {"Case 1, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<std::uint8_t> empty {};
  //   PartitionByteSequenceTester(
  //     message,
  //     true,
  //     empty, 
  //     fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT,
  //     0
  //   );
  // }

  // // Case 2: std::distance(end_iter, begin_iter) == 3,
  // // type == fcgi_si::FcgiType::kFCGI_STDIN, Fcgi_id == 1.
  // {
  //   std::string message {"Case 2, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<std::uint8_t> content {1,2,3};
  //   PartitionByteSequenceTester(
  //     message,
  //     false,
  //     content, 
  //     fcgi_si::FcgiType::kFCGI_STDIN,
  //     1
  //   );
  // }

  // // Case 3: std::distance(end_iter, begin_iter) == 25,
  // // type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 65535 ==
  // // std::numeric_limits<std::uint16_t>::max(). 
  // {
  //   std::string message {"Case 3, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<std::uint8_t> content {};
  //   for(int i {0}; i < 25; ++i)
  //     content.push_back(i);
  //   PartitionByteSequenceTester(
  //     message,
  //     false,
  //     content, 
  //     fcgi_si::FcgiType::kFCGI_STDOUT,
  //     std::numeric_limits<std::uint16_t>::max()
  //   );
  // }

  // // Case 4: std::distance(end_iter, begin_iter) == 8,
  // // type == static_cast<fcgi_si::FcgiType>(20), Fcgi_id == 3.
  // {
  //   std::string message {"Case 4, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<std::uint8_t> content {};
  //   for(int i {0}; i < 8; ++i)
  //     content.push_back(i);
  //   PartitionByteSequenceTester(
  //     message,
  //     false,
  //     content, 
  //     static_cast<fcgi_si::FcgiType>(20),
  //     3
  //   );
  // }

  // // Case 5: std::distance(end_iter, begin_iter) == 65528,
  // // type == fcgi_si::FcgiType::kFCGI_PARAMS, Fcgi_id == 300.
  // {
  //   std::string message {"Case 5, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<std::uint8_t> content {};
  //   for(int i {0}; i < 65528; ++i)
  //     content.push_back(i); // mod 256 overflow.
  //   PartitionByteSequenceTester(
  //     message,
  //     false,
  //     content, 
  //     fcgi_si::FcgiType::kFCGI_PARAMS,
  //     300
  //   );
  // }

  // Case 6: std::distance(end_iter, begin_iter) == 2^25,
  // type == fcgi_si::FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
  {
    std::string message {"Case 6, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content(1U << 25, 1U);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      fcgi_si::FcgiType::kFCGI_STDOUT,
      3
    );
  }
  close(temp_descriptor);
}
