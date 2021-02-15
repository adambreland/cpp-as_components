// MIT License
//
// Copyright (c) 2021 Adam J. Breland
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "include/fcgi_utilities.h"

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

#include "external/as_components_testing/gtest/include/as_components_testing_gtest_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/fcgi_protocol_constants.h"
#include "test/include/fcgi_si_testing_utilities.h"

namespace as_components {
namespace fcgi {
namespace test {

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
  // 1) ExtractFourByteLength

  uint8_t header_array[4] = {};
  unsigned char char_header_array[4] = {};

  // Case 1: Random value: 2,128,547
  EncodeFourByteLength(2128547, header_array);
  EXPECT_EQ(128U, header_array[0]);
  EXPECT_EQ(32U, header_array[1]);
  EXPECT_EQ(122U, header_array[2]);
  EXPECT_EQ(163U, header_array[3]);

  // Case 2: Random value with back_insert_iterator.
  std::vector<uint8_t> byte_seq {};
  EncodeFourByteLength(2128547, std::back_inserter(byte_seq));
  EXPECT_EQ(128U, byte_seq[0]);
  EXPECT_EQ(32U, byte_seq[1]);
  EXPECT_EQ(122U, byte_seq[2]);
  EXPECT_EQ(163U, byte_seq[3]);

  // Case 3: Minimum value, 128.
  EncodeFourByteLength(128, header_array);
  EXPECT_EQ(128U, header_array[0]);
  EXPECT_EQ(0U, header_array[1]);
  EXPECT_EQ(0U, header_array[2]);
  EXPECT_EQ(128U, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    EncodeFourByteLength(128, char_header_array);
    EXPECT_EQ(128U, char_header_array[0]);
    EXPECT_EQ(0U, char_header_array[1]);
    EXPECT_EQ(0U, char_header_array[2]);
    EXPECT_EQ(128U, char_header_array[3]);
  }

  // Case 4: Requires two bytes.
  EncodeFourByteLength(256, header_array);
  EXPECT_EQ(128U, header_array[0]);
  EXPECT_EQ(0U, header_array[1]);
  EXPECT_EQ(1U, header_array[2]);
  EXPECT_EQ(0U, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    EncodeFourByteLength(256, char_header_array);
    EXPECT_EQ(128U, char_header_array[0]);
    EXPECT_EQ(0U, char_header_array[1]);
    EXPECT_EQ(1U, char_header_array[2]);
    EXPECT_EQ(0U, char_header_array[3]);
  }

  // Case 5: Requires three bytes.
  EncodeFourByteLength(1UL << 16, header_array);
  EXPECT_EQ(128U, header_array[0]);
  EXPECT_EQ(1U, header_array[1]);
  EXPECT_EQ(0U, header_array[2]);
  EXPECT_EQ(0U, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    EncodeFourByteLength(1UL << 16, char_header_array);
    EXPECT_EQ(128U, char_header_array[0]);
    EXPECT_EQ(1U, char_header_array[1]);
    EXPECT_EQ(0U, char_header_array[2]);
    EXPECT_EQ(0U, char_header_array[3]);
  }

  // Case 6: Maximum value less one.
  EncodeFourByteLength((1UL << 31) - 1 - 1, header_array);
  EXPECT_EQ(255U, header_array[0]);
  EXPECT_EQ(255U, header_array[1]);
  EXPECT_EQ(255U, header_array[2]);
  EXPECT_EQ(254U, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    EncodeFourByteLength((1UL << 31) - 1 - 1, char_header_array);
    EXPECT_EQ(255U, char_header_array[0]);
    EXPECT_EQ(255U, char_header_array[1]);
    EXPECT_EQ(255U, char_header_array[2]);
    EXPECT_EQ(254U, char_header_array[3]);
  }

  // Case 7: Maximum value
  EncodeFourByteLength((1UL << 31) - 1, header_array);
  EXPECT_EQ(255U, header_array[0]);
  EXPECT_EQ(255U, header_array[1]);
  EXPECT_EQ(255U, header_array[2]);
  EXPECT_EQ(255U, header_array[3]);

  if(sizeof(unsigned char) == sizeof(uint8_t))
  {
    EncodeFourByteLength((1UL << 31) - 1, char_header_array);
    EXPECT_EQ(255U, char_header_array[0]);
    EXPECT_EQ(255U, char_header_array[1]);
    EXPECT_EQ(255U, char_header_array[2]);
    EXPECT_EQ(255U, char_header_array[3]);
  }

  // Case 8: 1
  EXPECT_THROW((EncodeFourByteLength(1, char_header_array)),
    std::invalid_argument);

  // Case 9: 0
  EXPECT_THROW((EncodeFourByteLength(0, char_header_array)),
    std::invalid_argument);

  // Case 10: -1
  EXPECT_THROW((EncodeFourByteLength(-1, char_header_array)),
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
  // 1) EncodeFourByteLength
  //
  // Other modules whose testing depends on this module: none.

  uint8_t seq[4] = {};
  uint32_t length {};

  // Case 1: Random value.
  EncodeFourByteLength(2128547, seq);
  length = ExtractFourByteLength(seq);
  EXPECT_EQ(2128547UL, length);

  // Case 2: Minimum length.
  EncodeFourByteLength(128, seq);
  length = ExtractFourByteLength(seq);
  EXPECT_EQ(128U, length);

  // Case 3: Requires two bytes.
  EncodeFourByteLength(256, seq);
  length = ExtractFourByteLength(seq);
  EXPECT_EQ(256U, length);

  // Case 4: Requires three bytes.
  EncodeFourByteLength(1UL << 16, seq);
  length = ExtractFourByteLength(seq);
  EXPECT_EQ(1UL << 16, length);

  // Case 5: Maximum value less one.
  EncodeFourByteLength((1UL << 31) - 1 - 1, seq);
  length = ExtractFourByteLength(seq);
  EXPECT_EQ((1UL << 31) - 1 - 1, length);

  // Case 6: Maximum value.
  EncodeFourByteLength((1UL << 31) - 1, seq);
  length = ExtractFourByteLength(seq);
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
  //  1) type:           FcgiType::kFCGI_BEGIN_REQUEST
  //     Fcgi_id:        0
  //     content_length: 0
  //     padding_length: 0
  //  2) type:           FcgiType::kFCGI_ABORT_REQUEST
  //     Fcgi_id:        1
  //     content_length: 1
  //     padding_length: 1
  //  3) type:           FcgiType::kFCGI_END_REQUEST
  //     Fcgi_id:        10
  //     content_length: 10
  //     padding_length: 10
  //  4) type:           FcgiType::kFCGI_PARAMS
  //     Fcgi_id:        2^16 - 1 (which is equal to uint16_t(-1))
  //     content_length: 2^16 - 1
  //     padding_length: 255 (which is equal to uint8_t(-1))
  //  5) type:           FcgiType::kFCGI_STDIN
  //     Fcgi_id:        1
  //     content_length: 1000
  //     padding_length: 0
  //  6) type:           FcgiType::kFCGI_STDOUT
  //     Fcgi_id:        1
  //     content_length: 250
  //     padding_length: 2
  //  7) type:           FcgiType::kFCGI_STDERR
  //     Fcgi_id:        1
  //     content_length: 2
  //     padding_length: 6
  //  8) type:           FcgiType::kFCGI_DATA
  //     Fcgi_id:        2^16 - 1
  //     content_length: 2^16 - 1
  //     padding_length: 7
  //  9) type:           FcgiType::kFCGI_GET_VALUES
  //     Fcgi_id:        0
  //     content_length: 100
  //     padding_length: 4
  // 10) type:           FcgiType::kFCGI_GET_VALUES_RESULT
  //     Fcgi_id:        0
  //     content_length: 100
  //     padding_length: 0
  // 11) type:           FcgiType::kFCGI_UNKNOWN_TYPE
  //     Fcgi_id:        1
  //     content_length: 8
  //     padding_length: 8
  //
  // Modules which testing depends on:
  //
  // Other modules whose testing depends on this module:
  // 1) ExtractContent

  std::vector<uint8_t> local_header(FCGI_HEADER_LEN);
  std::vector<uint8_t> expected_result(FCGI_HEADER_LEN);

  auto PopulateHeaderTester = [&local_header, &expected_result](
    std::string message,
    FcgiType type,
    uint16_t Fcgi_id,
    uint16_t content_length,
    uint8_t padding_length
  )->void
  {
    PopulateHeader(local_header.data(), type, Fcgi_id, content_length,
      padding_length);

    expected_result[0] = FCGI_VERSION_1;
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
    FcgiType type {FcgiType::kFCGI_BEGIN_REQUEST};
    uint16_t Fcgi_id {0};
    uint16_t content_length {0};
    uint8_t padding_length {0};

    std::string message {"Case 1, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 2
  {
    FcgiType type {FcgiType::kFCGI_ABORT_REQUEST};
    uint16_t Fcgi_id {1};
    uint16_t content_length {1};
    uint8_t padding_length {1};

    std::string message {"Case 2, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 3
  {
    FcgiType type {FcgiType::kFCGI_END_REQUEST};
    uint16_t Fcgi_id {10};
    uint16_t content_length {10};
    uint8_t padding_length {10};

    std::string message {"Case 3, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 4
  {
    FcgiType type {FcgiType::kFCGI_PARAMS};
    uint16_t Fcgi_id(-1);
    uint16_t content_length(-1);
    uint8_t padding_length(-1);

    std::string message {"Case 4, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 5
  {
    FcgiType type {FcgiType::kFCGI_STDIN};
    uint16_t Fcgi_id {1};
    uint16_t content_length {1000};
    uint8_t padding_length {0};

    std::string message {"Case 5, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 6
  {
    FcgiType type {FcgiType::kFCGI_STDOUT};
    uint16_t Fcgi_id {1};
    uint16_t content_length {250};
    uint8_t padding_length {2};

    std::string message {"Case 6, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 7
  {
    FcgiType type {FcgiType::kFCGI_STDERR};
    uint16_t Fcgi_id {1};
    uint16_t content_length {2};
    uint8_t padding_length {6};

    std::string message {"Case 7, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 8
  {
    FcgiType type {FcgiType::kFCGI_DATA};
    uint16_t Fcgi_id(-1);
    uint16_t content_length(-1);
    uint8_t padding_length {7};

    std::string message {"Case 8, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 9
  {
    FcgiType type {FcgiType::kFCGI_GET_VALUES};
    uint16_t Fcgi_id {0};
    uint16_t content_length {100};
    uint8_t padding_length {4};

    std::string message {"Case 9, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 10
  {
    FcgiType type {FcgiType::kFCGI_GET_VALUES_RESULT};
    uint16_t Fcgi_id {0};
    uint16_t content_length {100};
    uint8_t padding_length {0};

    std::string message {"Case 10, Line: "};
    message += std::to_string(__LINE__);
    PopulateHeaderTester(message, type, Fcgi_id, content_length, padding_length);
  }
  // Case 11
  {
    FcgiType type {FcgiType::kFCGI_UNKNOWN_TYPE};
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
  // 11) content_ptr == nullptr and content_length == 100.
  //
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module:
  // 1) EncodeNameValuePairs

  using NameValuePair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

  const std::string case_prefix {"Case "};
  const std::string case_suffix {", about line "};
  std::vector<std::uint8_t> encoded_name_string {'N','a','m','e'};
  std::vector<std::uint8_t> encoded_value_string {'V','a','l','u','e'};
  std::vector<uint8_t> four_name_vector(256, 'b');
  std::vector<uint8_t> four_value_vector(128, 'a');

  auto CaseNameGenerator = [&case_prefix, &case_suffix]
  (int case_number, int line)->std::string
  {
    std::string case_message {case_prefix + std::to_string(case_number)};
    case_message += case_suffix;
    case_message += std::to_string(line);
    case_message += ".";
    return case_message;
  };

  auto NonErrorCaseTester = []
  (
    const std::vector<NameValuePair>& nv_pair_list,
    std::string message
  )->void
  {
    std::vector<uint8_t> encoded_nv_pairs {};
    for(std::vector<NameValuePair>::const_iterator
      i {nv_pair_list.begin()};
      i != nv_pair_list.end();
      ++i)
    {
      // Encode the sizes.
      std::vector<std::uint8_t>::size_type sizes[2] = 
      {
        {i->first.size()},
        {i->second.size()}
      };
      for(int j {0}; j < 2; ++j)
      {
        if(sizes[j] > kNameValuePairSingleByteLength)
        {
          std::uint8_t buffer[4];
          EncodeFourByteLength(sizes[j], buffer);
          encoded_nv_pairs.insert(encoded_nv_pairs.end(), buffer, buffer + 4);
        }
        else
        {
          encoded_nv_pairs.push_back(sizes[j]);
        }
      }
      // Encode the names.
      encoded_nv_pairs.insert(encoded_nv_pairs.end(), 
        i->first.begin(), i->first.end());
      encoded_nv_pairs.insert(encoded_nv_pairs.end(), 
        i->second.begin(), i->second.end());
    }
    // encode_nv_pairs should now hold all of the encoded name-value pairs.
    std::uint8_t* data_ptr {(encoded_nv_pairs.size()) ?
      encoded_nv_pairs.data() : nullptr};
    std::vector<NameValuePair> result {ExtractBinaryNameValuePairs(
      data_ptr, encoded_nv_pairs.size())};
    EXPECT_EQ(nv_pair_list, result) << message;
  };

  // Case 1: Nothing to process.
  {
    NonErrorCaseTester({}, CaseNameGenerator(1, __LINE__));

    uint8_t test_value {0U};
    EXPECT_EQ(
      (std::vector<NameValuePair> {}),
      ExtractBinaryNameValuePairs(&test_value, 0U)
    ) << "Case 1, non-null pointer and content_length == 0U.";
  }

  // Case 2: Single name-value pair. (1 byte, 1 byte) for lengths. 
  // Empty name and value.
  {
    std::vector<NameValuePair> nv_pair_list {{{}, {}}};
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(2, __LINE__));
  }
  
  // Case 3: Single name-value pair. (1 byte, 1 byte) for lengths. Empty value.
  {
    std::vector<NameValuePair> nv_pair_list {{encoded_name_string, {}}};
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(3, __LINE__));
  }

  // Case 4: Single name-value pair. (1 byte, 1 byte) for lengths.
  {
    std::vector<NameValuePair> nv_pair_list
    {
      {encoded_name_string, encoded_value_string}
    };
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(4, __LINE__));
  }

  // Case 5: Single name-value pair, (1 byte, 4 bytes) for lengths.
  {
    std::vector<NameValuePair> nv_pair_list 
    {
      {encoded_name_string, four_value_vector}
    };
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(5, __LINE__));
  }
  
  // Case 6: Single name-value pair, (4 byte, 1 bytes) for lengths.
  {
    std::vector<NameValuePair> nv_pair_list
    {
      {four_name_vector, encoded_value_string}
    };
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(6, __LINE__));
  }
  
  // Case 7: Multiple name-value pairs with names and values that need one and
  // four byte lengths. Also includes an empty value.
  {
    std::vector<NameValuePair> nv_pair_list
    {
      {four_name_vector, four_value_vector},
      {encoded_name_string, encoded_value_string},
      {encoded_name_string, {}}
    };
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(7, __LINE__));
  }

  // Case 8: As above, but with the empty value in the middle.
  {
    std::vector<NameValuePair> nv_pair_list
    {
      {four_name_vector, four_value_vector},
      {encoded_name_string, {}},
      {encoded_name_string, encoded_value_string}
    };
    NonErrorCaseTester(nv_pair_list, CaseNameGenerator(8, __LINE__));
  }

  // Case 9: An incomplete encoding. A single name and value is present. Extra
  // information is added. ProcessBinaryNameValuePairs should return an
  // empty vector.
  {
    std::vector<std::uint8_t> encoded_nv_pair 
    {
      static_cast<std::uint8_t>(encoded_name_string.size()),
      static_cast<std::uint8_t>(encoded_value_string.size())
    };
    encoded_nv_pair.insert(encoded_nv_pair.end(),
      encoded_name_string.begin(), encoded_name_string.end());
    encoded_nv_pair.insert(encoded_nv_pair.end(),
      encoded_value_string.begin(), encoded_value_string.end());
    encoded_nv_pair.push_back(10U);
    // A terminal byte with length information was added above. encoded_nv_pair
    // is now invalid.
    EXPECT_EQ(ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
      encoded_nv_pair.size()), std::vector<NameValuePair> {}) << 
      CaseNameGenerator(9, __LINE__);
  }
  
  // Case 10: Too many bytes were specified for the last name, but the first 
  // name-value pair was correct. An empty vector should still be returned.
  {
    std::vector<std::uint8_t> encoded_nv_pair
    {
      static_cast<std::uint8_t>(encoded_name_string.size()),
      static_cast<std::uint8_t>(encoded_value_string.size())
    };
    encoded_nv_pair.insert(encoded_nv_pair.end(),
      encoded_name_string.begin(), encoded_name_string.end());
    encoded_nv_pair.insert(encoded_nv_pair.end(),
      encoded_value_string.begin(), encoded_value_string.end());
    encoded_nv_pair.push_back(100U);
    encoded_nv_pair.push_back(
      static_cast<std::uint8_t>(encoded_value_string.size()));
    encoded_nv_pair.insert(encoded_nv_pair.end(),
      encoded_name_string.begin(), encoded_name_string.end());
    EXPECT_EQ(ExtractBinaryNameValuePairs(encoded_nv_pair.data(),
      encoded_nv_pair.size()), std::vector<NameValuePair> {}) <<
      CaseNameGenerator(10, __LINE__);
  }

  // Case 11: content_ptr == nullptr, content_length == 100.
  EXPECT_THROW((ExtractBinaryNameValuePairs(nullptr, 100)),
    std::invalid_argument) << " Case 11: throw with null pointer and non-zero "
      "length.";
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
  // 9) The returned number of records.
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
  // 1) ExtractContent
  // 2) ProcessBinaryNameValuePairs
  //
  // Other modules whose testing depends on this module: none.

  using NameValuePair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

  int temp_fd {};
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalCreateBazelTemporaryFile(
    &temp_fd, __LINE__));

  auto EncodeNameValuePairTester = [temp_fd]
  (
    std::string message,
    const std::vector<NameValuePair>& pair_seq,
    FcgiType type,
    uint16_t fcgi_id,
    bool expect_error,
    std::vector<NameValuePair>::const_iterator error_iter
  )->void
  {
    if(!testing::gtest::GTestNonFatalPrepareTemporaryFile(temp_fd, __LINE__))
    {
      ADD_FAILURE() << "A temporary file could not be prepared." << '\n'
        << message;
      return;
    }
    std::vector<NameValuePair>::const_iterator end_iter {pair_seq.cend()};
    std::vector<NameValuePair>::const_iterator begin_iter {pair_seq.cbegin()};
    std::size_t offset {0U};
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t,
      std::vector<NameValuePair>::const_iterator> encode_return {};
    do
    {
      encode_return = {EncodeNameValuePairs(begin_iter, end_iter,
        type, fcgi_id, offset)};
      ssize_t write_return {0};
      while((write_return = writev(temp_fd, std::get<2>(encode_return).data(),
        std::get<2>(encode_return).size())) == -1 && errno == EINTR)
      continue;
      if((write_return == -1) ||
         (static_cast<std::size_t>(write_return) != std::get<1>(encode_return)))
      {
        ADD_FAILURE() << "A call to writev did not write all bytes requested."
          << '\n' << message;
        return;
      }
      begin_iter = std::get<6>(encode_return);
      offset = std::get<5>(encode_return);
    } while(std::get<0>(encode_return) && (begin_iter != end_iter));
    if(expect_error)
    {
      EXPECT_FALSE(std::get<0>(encode_return)) << message;
      EXPECT_EQ(error_iter, std::get<6>(encode_return)) << message;
    }
    off_t lseek_return {lseek(temp_fd, 0, SEEK_SET)};
    if(lseek_return == -1)
    {
      ADD_FAILURE() << "A call to lseek failed." << '\n' << message;
      return;
    }
    std::tuple<bool, bool, bool, bool, std::size_t, std::vector<std::uint8_t>>
    extract_content_result {ExtractContent(temp_fd, type,
      fcgi_id)};
    if(!std::get<0>(extract_content_result))
    {
      ADD_FAILURE() << "A call to ExtractContent encountered an "
        "unrecoverable read error." << '\n' << message;
      return;
    }
    if(!std::get<1>(extract_content_result))
    {
      ADD_FAILURE() << "A call to ExtractContent reported from "
        "std::get<1> that a header error or a partial section was encountered."
        << '\n' << message;
      return;
    }
    EXPECT_FALSE(std::get<2>(extract_content_result)) <<
      "A call to ExtractContent reported from "
      "std::get<2> that the record sequence was terminated."
        << '\n' << message;
    EXPECT_TRUE(std::get<3>(extract_content_result)) <<
      "A call to ExtractContent reported from "
      "std::get<3> that an unaligned record was present."
        << '\n' << message;
    std::vector<NameValuePair> extracted_pairs
      {ExtractBinaryNameValuePairs(
        std::get<5>(extract_content_result).data(),
        std::get<5>(extract_content_result).size())};
    if(!expect_error)
    {
      EXPECT_EQ(pair_seq, extracted_pairs) << message;
    }
    else
    {
      std::vector<NameValuePair> pair_seq_prefix {pair_seq.begin(),
        error_iter};
      EXPECT_EQ(pair_seq_prefix, extracted_pairs) << message;
    }
  };

  // Case 1: No name-value pairs, i.e. pair_iter == end.
  {
    std::string message {"Case 2, about line: "};
    message += std::to_string(__LINE__);
    std::vector<NameValuePair> pair_sequence {};
    EncodeNameValuePairTester(
      message,
      pair_sequence,
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
      pair_sequence.cend()
    );
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1000U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
      pair_sequence.cend()
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
      pair_sequence.cend()
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
      pair_sequence.cend()
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
      pair_sequence.cend()
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
      FcgiType::kFCGI_PARAMS,
      1U,
      false,
      pair_sequence.cend()
    );
  }

  // // Cases 15 and 16 cause the test process to be sporadically killed by the
  // // Linux Out of Memory Killer.
  // // Case 15: Multiple name-value pairs where one of the middle pairs has a
  // // name whose length exceeds the maximum size. (Invalid input.)
  // {
  //   std::string message {"Case 15, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<uint8_t> illegal_name((1UL << 31) + 10, 'd');
  //   std::vector<NameValuePair> pair_sequence {{{'a'}, {0}}, {{'b'}, {1}},
  //     {{'c'}, {2}}, {std::move(illegal_name), {3}}, {{'e'}, {4}},
  //     {{'f'}, {5}}, {{'g'}, {6}}};
  //   EncodeNameValuePairTester(
  //     message,
  //     pair_sequence,
  //     FcgiType::kFCGI_PARAMS,
  //     1U,
  //     true,
  //     pair_sequence.cbegin() + 3
  //   );
  // }

  // // Case 16: As in 15, but for value instead of name. (Invalid input.)
  // {
  //   std::string message {"Case 16, about line: "};
  //   message += std::to_string(__LINE__);
  //   std::vector<uint8_t> illegal_value((1UL << 31) + 10, 3U);
  //   std::vector<NameValuePair> pair_sequence {{{'a'}, {0}}, {{'b'}, {1}},
  //     {{'c'}, {2}}, {{'d'}, std::move(illegal_value)}, {{'e'}, {4}},
  //     {{'f'}, {5}}, {{'g'}, {6}}};
  //   EncodeNameValuePairTester(
  //     message,
  //     pair_sequence,
  //     FcgiType::kFCGI_PARAMS,
  //     1U,
  //     true,
  //     pair_sequence.cbegin() + 3
  //   );
  // }

  // Case 17: More than the current iovec limit of name-value pairs.
  {
    std::string message {"Case 17, about line: "};
    message += std::to_string(__LINE__);
    long local_iovec_max {iovec_MAX};
    if(local_iovec_max == -1)
    {
      // 1024 is the current Linux iovec limit.
      local_iovec_max = 1024;
    }
    local_iovec_max = std::min<long>(local_iovec_max,
      std::numeric_limits<int>::max());
    long pair_count {local_iovec_max};
    if(local_iovec_max <= (std::numeric_limits<long>::max() - 10))
    {
      pair_count += 10;
      NameValuePair to_copy {{'a'}, {1U}};
      std::vector<NameValuePair> pair_sequence(pair_count, to_copy);
      EncodeNameValuePairTester(
        message,
        pair_sequence,
        FcgiType::kFCGI_PARAMS,
        1U,
        false,
        pair_sequence.cend()
      );
    }
    else
    {
      ADD_FAILURE() << "A sufficiently long sequence of pairs could not be "
        "produced.";
    }
  }
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
  {
    EXPECT_THROW(
      (ToUnsignedCharacterVector(std::numeric_limits<int>::min())),
      std::invalid_argument
    );
  }
  // Case 2
  EXPECT_THROW(ToUnsignedCharacterVector(-200), std::invalid_argument);
  // Case 3
  EXPECT_THROW(ToUnsignedCharacterVector(-1), std::invalid_argument);
  // Case 4
  EXPECT_EQ(ToUnsignedCharacterVector(0),
    (std::vector<uint8_t> {'0'}));
  // Case 5
  EXPECT_EQ(ToUnsignedCharacterVector(1),
    (std::vector<uint8_t> {'1'}));
  // Case 6
  EXPECT_EQ(ToUnsignedCharacterVector(100),
    (std::vector<uint8_t> {'1','0','0'}));
  // Case 7
  if(test_extremes)
  {
    EXPECT_EQ(
      ToUnsignedCharacterVector(std::numeric_limits<int>::max()),
      (std::vector<uint8_t> {'2','1','4','7','4','8','3','6','4','7'})
    );
  }
}

TEST(Utility, PartitionByteSequence)
{
  // Testing explanation
  //    Tests call PartitionByteSequence, use writev to write to a temporary
  // file, and use ExtractContent to retrieve the content of
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
  //    type == FcgiType::kFCGI_GET_VALUES_RESULT, Fcgi_id == 0.
  // 2) std::distance(end_iter, begin_iter) == 3,
  //    type == FcgiType::kFCGI_STDIN, Fcgi_id == 1.
  // 3) std::distance(end_iter, begin_iter) == 25,
  //    type == FcgiType::kFCGI_STDOUT, Fcgi_id == 65535 ==
  //    std::numeric_limits<std::uint16_t>::max().
  // 4) std::distance(end_iter, begin_iter) == 8,
  //    type == static_cast<FcgiType>(20), Fcgi_id == 3.
  // 5) std::distance(end_iter, begin_iter) == 65528,
  //    type == FcgiType::kFCGI_PARAMS, Fcgi_id == 300.
  // 6) std::distance(end_iter, begin_iter) == 2^25,
  //    type == FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
  //
  // Modules which testing depends on:
  // 1) ExtractContent
  //
  // Other modules whose testing depends on this module: none.

  // BAZEL DEPENDENCY
  int temp_descriptor {};
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalCreateBazelTemporaryFile(
    &temp_descriptor, __LINE__));
  
  // Note that the function call termination behavior of fatal Google Test
  // assertions, where a subroutine is exited without terminating the calling
  // routine, is desired here. As such, propagation of fatal failures is
  // neither needed nor appropriate.
  auto PartitionByteSequenceTester = [temp_descriptor](
    const std::string& message,
    bool expect_terminal_empty_record,
    std::vector<std::uint8_t>& content_seq, 
    FcgiType type,
    std::uint16_t Fcgi_id
  )->void
  {
    // Clear the file.
    ASSERT_NE(ftruncate(temp_descriptor, 0), -1);
    off_t lseek_return {lseek(temp_descriptor, 0, SEEK_SET)};
    ASSERT_NE(lseek_return, -1);

    // Call PartitionByteSequence and write the encoded record sequence.
    // The loop continues until all of content_seq is encoded and written.
    std::vector<std::uint8_t>::iterator loop_iter {content_seq.begin()};
    do
    {
      std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
        std::size_t, std::vector<std::uint8_t>::iterator> pr
        {PartitionByteSequence(loop_iter, content_seq.end(), 
        type, Fcgi_id)};

      ssize_t writev_return {writev(temp_descriptor, std::get<1>(pr).data(),
        std::get<1>(pr).size())};
      ASSERT_FALSE((writev_return < 0) ||
                   (static_cast<std::size_t>(writev_return) < std::get<2>(pr)));
        
      loop_iter = std::get<3>(pr);
    } while(loop_iter != content_seq.cend());

    // Extract the content and validate.
    ASSERT_NE(lseek_return = lseek(temp_descriptor, 0, SEEK_SET), -1);
    std::tuple<bool, bool, bool, bool, std::size_t, std::vector<uint8_t>> ecr
      {ExtractContent(temp_descriptor, type, Fcgi_id)};
    ASSERT_TRUE(std::get<0>(ecr)) <<
      "A call to ExtractContent encountered an error." << '\n' 
        << message;
    ASSERT_TRUE(std::get<1>(ecr)) <<
      "A call to ExtractContent determined that "
      "a header error was present or an incomplete record was present."
        <<  '\n' << message;
    ASSERT_FALSE(( std::get<2>(ecr) && !expect_terminal_empty_record)   || 
                 (!std::get<2>(ecr) &&  expect_terminal_empty_record)) <<
      "A terminal empty record mismatch was present." << '\n' << message;

    // std::get<3>(ecr) tests record alignment on 8-byte boundaries.
    // Such alignment is not specified by PartitionByteSequence.

    // std::get<4>(ecr) examines the number of records. As even empty content
    // sequences should cause a header to be written, this value should always
    // be larger than zero.
    ASSERT_GT(std::get<4>(ecr), 0U);

    // Check for equality of the extracted byte sequence and content_seq.
    std::pair<std::vector<std::uint8_t>::iterator,
      std::vector<std::uint8_t>::iterator> expected_mismatch
      {content_seq.end(), std::get<5>(ecr).end()};
    ASSERT_TRUE((content_seq.size() == std::get<5>(ecr).size()) &&
      (std::mismatch(content_seq.begin(), content_seq.end(),
       std::get<5>(ecr).begin()) == expected_mismatch)) << 
       "The extracted byte sequence did not match the encoded argument." 
        << '\n' << message;
  };

  // Case 1: begin_iter == end_iter, 
  // type == FcgiType::kFCGI_GET_VALUES_RESULT, Fcgi_id == 0.
  {
    std::string message {"Case 1, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> empty {};
    PartitionByteSequenceTester(
      message,
      true,
      empty, 
      FcgiType::kFCGI_GET_VALUES_RESULT,
      0
    );
  }

  // Case 2: std::distance(end_iter, begin_iter) == 3,
  // type == FcgiType::kFCGI_STDIN, Fcgi_id == 1.
  {
    std::string message {"Case 2, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content {1,2,3};
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      FcgiType::kFCGI_STDIN,
      1
    );
  }

  // Case 3: std::distance(end_iter, begin_iter) == 25,
  // type == FcgiType::kFCGI_STDOUT, Fcgi_id == 65535 ==
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
      FcgiType::kFCGI_STDOUT,
      std::numeric_limits<std::uint16_t>::max()
    );
  }

  // Case 4: std::distance(end_iter, begin_iter) == 8,
  // type == static_cast<FcgiType>(20), Fcgi_id == 3.
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
      static_cast<FcgiType>(20),
      3
    );
  }

  // Case 5: std::distance(end_iter, begin_iter) == 65528,
  // type == FcgiType::kFCGI_PARAMS, Fcgi_id == 300.
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
      FcgiType::kFCGI_PARAMS,
      300
    );
  }

  // Case 6: std::distance(end_iter, begin_iter) == 2^25,
  // type == FcgiType::kFCGI_STDOUT, Fcgi_id == 3.
  {
    std::string message {"Case 6, about line: "};
    message += std::to_string(__LINE__);
    std::vector<std::uint8_t> content(1U << 25, 1U);
    PartitionByteSequenceTester(
      message,
      false,
      content, 
      FcgiType::kFCGI_STDOUT,
      3
    );
  }

  close(temp_descriptor);
}

} // namespace test
} // namespace fcgi
} // namespace as_components
