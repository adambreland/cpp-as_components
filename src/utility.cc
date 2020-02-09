#include <cstdint>

#include <cstring>

#include "include/protocol_constants.h"
#include "include/utility.h"

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
