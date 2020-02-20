#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "include/protocol_constants.h"
#include "include/utility.h"

std::tuple<bool, bool, bool, std::vector<uint8_t>>
fcgi_si::ExtractContent(int fd, FCGIType type, uint16_t id)
{
  constexpr uint16_t buffer_size {1 << 10};
  uint8_t byte_buffer[buffer_size];

  uint32_t local_offset {0};
  ssize_t number_bytes_read {0};
  uint8_t local_header[FCGI_HEADER_LEN];
  int header_bytes_read {0};
  std::vector<uint8_t> content_bytes {};
  uint16_t FCGI_id {};
  uint16_t content_length {0};
  uint16_t content_bytes_read {0};
  uint8_t padding_length {0};
  uint8_t padding_bytes_read {0};
  bool read_error {false}
  bool error_found {false};
  bool sequence_terminated {false};
  int state {0};

  while(number_bytes_read = read(fd, byte_buffer, buffer_size))
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
      switch(state) {
        case 0 : {
          if(header_bytes_read < FCGI_HEADER_LEN)
          {
            // Safe narrowing as this can never exceed FCGI_HEADER_LEN.
            int header_bytes_to_copy(std::min<ssize_t>(FCGI_HEADER_LEN
              - header_bytes_read, number_bytes_read - local_offset));
            std::memcpy((void*)(local_header + header_bytes_read),
              (void*)(byte_buffer + local_offset), header_bytes_to_copy);
            local_offset += header_bytes_to_copy;
            header_bytes_read += header_bytes_to_copy;
            if(local_offset == number_bytes_read)
              break;
          }
          // The header is complete and there are some bytes left to process.

          // Extract header information.
          (FCGI_id = local_header[kHeaderRequestIDB1Index]) <<= 8; // One byte.
          FCGI_id += local_header[kHeaderRequestIDB0Index];
          (content_length = local_header[kHeaderContentLengthB1Index]) <<= 8;
          content_length += local_header[kHeaderContentLengthB0Index];
          padding_length = local_header[kHeaderPaddingLengthIndex];
          // Verify header information.
          if(static_cast<FCGIType>(local_header[kHeaderTypeIndex]) != type
             || FCGI_id != id)
          {
            error_found = true;
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
            if(local_offset == number_bytes_read)
              break;
          }
          // Set or reset state.
          content_bytes_read = 0;
          state++;
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
            if(local_offset == number_bytes_read)
              break;
          }
          padding_bytes_read = 0;
          state = 0;
        }
      }
      if(error_found || sequence_terminated)
        break;
    }
    if(error_found || sequence_terminated)
      break;
  }
  return std::make_tuple(!read_error, !error_found, sequence_terminated,
    content_bytes);
}

void fcgi_si::PopulateHeader(std::uint8_t* byte_ptr, fcgi_si::FCGIType type,
  std::uint16_t FCGI_id, std::uint16_t content_length,
  std::uint8_t padding_length)
{
  std::uint8_t header_array[fcgi_si::FCGI_HEADER_LEN];
  header_array[0] = fcgi_si::FCGI_VERSION_1;
  header_array[1] = static_cast<uint8_t>(type);
  header_array[2] = static_cast<uint8_t>(FCGI_id >> 8);
  header_array[3] = static_cast<uint8_t>(FCGI_id);
  header_array[4] = static_cast<uint8_t>(content_length >> 8);
  header_array[5] = static_cast<uint8_t>(content_length);
  header_array[6] = padding_length;
  header_array[7] = 0;

  std::memcpy((void*)byte_ptr, (void*)header_array,
    fcgi_si::FCGI_HEADER_LEN);
}

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
