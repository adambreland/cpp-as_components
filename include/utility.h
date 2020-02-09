#ifndef FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_

#include <cstdint>

#include "include/protocol_constants.h"

namespace fcgi_si {

void PopulateHeader(std::uint8_t* byte_ptr, fcgi_si::FCGIType type,
  std::uint16_t FCGI_id, std::uint16_t content_length,
  std::uint8_t padding_length);

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_
