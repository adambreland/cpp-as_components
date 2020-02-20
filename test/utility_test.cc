#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
  // Examined properties:
  //
  // Test cases:
  //
  // Modules which testing depends on:
  // 1) fcgi_si::ProcessBinaryNameValuePairs
  //
  // Other modules whose testing depends on this module: none.
  //
  // Steps most cases:
  // 1) Create a sequence of std::pairs with containerized names and values.
  // 2) Call EncodeNameValuePairs on the sequence.
  // 3) Write the name-value pairs encoded in the FastCGI name-value pair
  //    format as a sequence of FastCGI records by making a call to writev
  //    using the information returned by the call to EncodeNameValuePairs.
  // 4) Process the written FastCGI records.
  //    a) Check that each header is correct.
  //    b) Copy the content to generate a contiguous content byte sequence.
  // 5) Call ProcessBinaryNameValuePairs on the contiguous sequence. Check
  //    that the returned vector of pairs is equal to the source sequence.


  //
  // int fd {open(".", O_RDWR | O_TMPFILE | O_EXCL)};
  // if(fd == -1)
  //   ; // TODO Add code to nicely handle error using reporting from Googletest.
  //
  // // Call to EncodeNameValuePairs here after construction name-value pairs.
  //
  // if(rr == -1)
  //   ; // TODO As above.



}



TEST(Utility, uint32_tToUnsignedCharacterVector)
{
  //
}
