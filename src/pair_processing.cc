#include <cstdint>
#include <utility>
#include <vector>

#include "include/pair_processing.h"
#include "include/protocol_constants.h"

std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
fcgi_si::
ProcessBinaryNameValuePairs(uint32_t content_length, const uint8_t* content_ptr)
{
  uint32_t bytes_processed {0};
  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
  result {};
  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
  error_result {};

  while(bytes_processed < content_length)
  {
    uint32_t name_length;
    uint32_t value_length;
    bool name_length_bit = *content_ptr >> 7;
    bool value_length_bit;

    // Extract name length.
    if(name_length_bit)
    {
      if((bytes_processed + 3) > content_length)
        return error_result; // Not enough information to continue.
      name_length = fcgi_si::ExtractFourByteLength(content_ptr);
      bytes_processed += 4;
      content_ptr += 4;
    }
    else
    {
      name_length = *content_ptr;
      bytes_processed += 1;
      content_ptr += 1;
    }

    // Extract value length.
    if((bytes_processed + 1) > content_length)
      return error_result;
    value_length_bit = *content_ptr >> 7;
    if(value_length_bit)
    {
      if((bytes_processed + 3) > content_length)
        return error_result; // Not enough information to continue.
      value_length = fcgi_si::ExtractFourByteLength(content_ptr);
      bytes_processed += 4;
      content_ptr += 4;
    }
    else
    {
      value_length = *content_ptr;
      bytes_processed += 1;
      content_ptr += 1;
    }

    // Extract name and value as byte strings.
    if((bytes_processed + name_length + value_length) > content_length)
      return error_result; // Not enough information to continue.
    std::vector<uint8_t> name {content_ptr, content_ptr + name_length};
    content_ptr += name_length;
    std::vector<uint8_t> value {content_ptr, content_ptr + value_length};
    content_ptr += value_length;
    bytes_processed += (name_length + value_length);
    result.emplace_back(std::move(name), std::move(value));
  } // End while (no more pairs to process).

  return result;
}

std::vector<uint8_t> fcgi_si::uint32_tToUnsignedCharacterVector(uint32_t c)
{
  // This implementation allows the absolute size of char to be larger than
  // one byte. It is assumed that only ASCII digits are present in c_string.
  std::string c_string {std::to_string(c)};
  std::vector<uint8_t> c_vector {c_string.begin(), c_string.end()};
  return c_vector;
}
