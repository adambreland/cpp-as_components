#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "include/protocol_constants.h"
#include "include/utility.h"

namespace fcgi_si {

void PopulateHeader(std::uint8_t* byte_ptr, FCGIType type,
  std::uint16_t FCGI_id, std::uint16_t content_length,
  std::uint8_t padding_length) noexcept
{
  std::uint8_t header_array[FCGI_HEADER_LEN];
  header_array[0] = FCGI_VERSION_1;
  header_array[1] = static_cast<uint8_t>(type);
  header_array[2] = static_cast<uint8_t>(FCGI_id >> 8);
  header_array[3] = static_cast<uint8_t>(FCGI_id);
  header_array[4] = static_cast<uint8_t>(content_length >> 8);
  header_array[5] = static_cast<uint8_t>(content_length);
  header_array[6] = padding_length;
  header_array[7] = 0;

  std::memcpy(static_cast<void*>(byte_ptr), static_cast<void*>(header_array),
    FCGI_HEADER_LEN);
}

std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
ProcessBinaryNameValuePairs(const uint8_t* content_ptr, 
  int_fast32_t content_length)
{
  using byte_seq_pair = std::pair<std::vector<std::uint8_t>, 
    std::vector<std::uint8_t>>;

  const char* error_message_ptr {"The allowed content length was not large "
    "enough given the encoded name and value lengths."};
  std::invalid_argument length_error {error_message_ptr};

  if(content_length > 0 && content_ptr == nullptr)
    throw std::invalid_argument {"A nullptr was passed along with a non-zero "
    "content length."};

  int_fast32_t bytes_processed {0};
  std::vector<byte_seq_pair> result {};
  std::vector<uint8_t> name_and_value_array[2] = {{}, {}};

  while(bytes_processed < content_length)
  {
    // Extract the name and value lengths while checking that the given
    // content is not exceeded.
    int_fast32_t name_value_lengths[2] = {};
    for(int i {0}; i < 2; ++i)
    {
      // Checks if a byte is present to continue.
      if((bytes_processed + 1) > content_length)
        throw length_error;
      bool four_byte_bit {*content_ptr & 0x80}; // Check the leading bit.
      if(four_byte_bit)
      {
        // Check that enough bytes were given.
        if((bytes_processed + 4) > content_length)
          throw length_error;
        name_value_lengths[i] = ExtractFourByteLength(content_ptr);
        bytes_processed += 4;
        content_ptr += 4;
      }
      else
      {
        name_value_lengths[i] = *content_ptr;
        bytes_processed += 1;
        content_ptr += 1;
      }
    }

    // Check that the given content will not be exceeded when the name and
    // value byte sequences are extracted.
    int_fast32_t length_with_nv {bytes_processed + name_value_lengths[0] 
      + name_value_lengths[1]};
    if(length_with_nv > content_length)
      throw length_error;
    // Extract name and value as byte sequences.
    const uint8_t* past_end {content_ptr};
    for(int i {0}; i < 2; ++i)
    {
      past_end += name_value_lengths[i];
      name_and_value_array[i].insert(name_and_value_array[i].end(), content_ptr, 
        past_end);
      content_ptr = past_end;
    }
    result.emplace_back(std::move(name_and_value_array[0]), 
      std::move(name_and_value_array[1]));
    bytes_processed = length_with_nv;

    for(int i {0}; i < 2; ++i)
    {
      name_and_value_array[i].clear();
    }
  }

  return result;
}

std::vector<uint8_t> ToUnsignedCharacterVector(int c)
{
  if(c < 0)
    throw std::invalid_argument {"A negative value was given."};

  // This implementation allows the absolute size of char to be larger than
  // one byte. It is assumed that only ASCII digits are present in c_string.
  std::string c_string {std::to_string(c)};
  std::vector<uint8_t> c_vector {c_string.begin(), c_string.end()};
  return c_vector;
}

} // namspace fcgi_si
