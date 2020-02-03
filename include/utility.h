#ifndef FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_

#include <cstdint>

#include "include/protocol_constants.h"

namespace fcgi_si {

template<typename ByteIter>
void PopulateHeader(ByteIter byte_iter, fcgi_si::FCGIType type,
  uint16_t FCGI_id, uint16_t content_length, uint8_t padding_length)
{
  *byte_iter++ = fcgi_si::FCGI_VERSION_1;
  *byte_iter++ = static_cast<uint8_t>(type);
  *byte_iter++ = static_cast<uint8_t>(FCGI_id >> 8);
  *byte_iter++ = static_cast<uint8_t>(FCGI_id);
  *byte_iter++ = static_cast<uint8_t>(content_length >> 8);
  *byte_iter++ = static_cast<uint8_t>(content_length);
  *byte_iter++ = padding_length;
  *byte_iter++ = 0;
}

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_
