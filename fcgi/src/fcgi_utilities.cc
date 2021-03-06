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

#include "fcgi/include/fcgi_utilities.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "fcgi/include/fcgi_protocol_constants.h"

namespace as_components {
namespace fcgi {

std::vector<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>>
ExtractBinaryNameValuePairs(const std::uint8_t* content_ptr, 
  std::size_t content_length)
{
  using byte_seq_pair = std::pair<std::vector<std::uint8_t>, 
    std::vector<std::uint8_t>>;

  if((content_length != 0U) && (content_ptr == nullptr))
  {
    throw std::invalid_argument {"A null pointer was passed with "
    "content_length != 0U."};
  }

  std::size_t bytes_processed {0U};
  std::vector<byte_seq_pair> result {};
  std::vector<byte_seq_pair> error_result {};
  std::vector<std::uint8_t> name_and_value_array[2] = {{}, {}};

  while(bytes_processed < content_length)
  {
    // Extract the name and value lengths while checking that the given
    // content is not exceeded.
    int_fast32_t name_value_lengths[2] = {};
    for(int i {0}; i < 2; ++i)
    {
      // Checks if a byte is present to continue.
      if((bytes_processed + 1) > content_length)
        return error_result;
      bool four_byte_bit {bool(*content_ptr & 0x80)}; // Check the leading bit.
      if(four_byte_bit)
      {
        // Check that enough bytes were given.
        if((bytes_processed + 4U) > content_length)
          return error_result;
        name_value_lengths[i] = ExtractFourByteLength(content_ptr);
        bytes_processed += 4U;
        content_ptr += 4;
      }
      else
      {
        name_value_lengths[i] = *content_ptr;
        bytes_processed += 1U;
        content_ptr += 1;
      }
    }

    // Check that the given content will not be exceeded when the name and
    // value byte sequences are extracted.
    std::size_t length_with_nv {
      bytes_processed                                 + 
      static_cast<std::size_t>(name_value_lengths[0]) +
      static_cast<std::size_t>(name_value_lengths[1])
    };
    if(length_with_nv > content_length)
      return error_result;
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

void PopulateBeginRequestRecord(std::uint8_t* byte_ptr, std::uint16_t fcgi_id,
  std::uint16_t role, bool keep_conn) noexcept
{
  PopulateHeader(byte_ptr, FcgiType::kFCGI_BEGIN_REQUEST, fcgi_id,
    FCGI_HEADER_LEN, 0U);
  byte_ptr += FCGI_HEADER_LEN;
  *byte_ptr = (role >> 8U);
  ++byte_ptr;
  *byte_ptr = role; // Truncate role.
  ++byte_ptr;
  *byte_ptr = (keep_conn) ? 1U : 0U;
}

void PopulateHeader(std::uint8_t* byte_ptr, FcgiType type,
  std::uint16_t Fcgi_id, std::uint16_t content_length,
  std::uint8_t padding_length) noexcept
{
  std::uint8_t header_array[FCGI_HEADER_LEN];
  header_array[0] = FCGI_VERSION_1;
  header_array[1] = static_cast<uint8_t>(type);
  header_array[2] = static_cast<uint8_t>(Fcgi_id >> 8);
  header_array[3] = static_cast<uint8_t>(Fcgi_id);
  header_array[4] = static_cast<uint8_t>(content_length >> 8);
  header_array[5] = static_cast<uint8_t>(content_length);
  header_array[6] = padding_length;
  header_array[7] = 0;

  std::memcpy(static_cast<void*>(byte_ptr), static_cast<void*>(header_array),
    FCGI_HEADER_LEN);
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

namespace partition_byte_sequence_internal {

std::size_t InitializeMaxForIovec()
{
  long lm {iovec_MAX};
  std::size_t i_max {(lm > 0) ?
    static_cast<std::size_t>(lm) :
    1024U
  };
  i_max = std::min<std::size_t>(i_max, std::numeric_limits<int>::max());
  std::size_t i_inter {(i_max - 1)/2};
  if(i_inter >
      (std::numeric_limits<std::size_t>::max()/max_aligned_content_length))
  {
    return std::numeric_limits<std::size_t>::max();
  }
  else
  {
    return i_inter*max_aligned_content_length;
  }
}

std::size_t NeededIovec(std::size_t m) 
{
  return
    (2U*CeilingOfQuotient(m,
      static_cast<std::size_t>(max_aligned_content_length))) +
    static_cast<std::size_t>((m % 8U) > 0U);
}

ssize_t NeededSsize_t(std::size_t m)
{
  return
    static_cast<ssize_t>(
      m +
      (8U*CeilingOfQuotient(m,
        static_cast<std::size_t>(max_aligned_content_length))) +
      ((8U - (m % 8U)) % 8U)
    );
}

std::size_t NeededLocalData(std::size_t m)
{
  return
    8U*(1U + CeilingOfQuotient(m, static_cast<std::size_t>(
      max_aligned_content_length)));
}

} // namespace partition_byte_sequence_internal

} // namspace fcgi
} // namespace as_components
