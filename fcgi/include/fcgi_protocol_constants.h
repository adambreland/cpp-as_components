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

#ifndef AS_COMPONENTS_FCGI_INCLUDE_FCGI_PROTOCOL_CONSTANTS_H_
#define AS_COMPONENTS_FCGI_INCLUDE_FCGI_PROTOCOL_CONSTANTS_H_

#include <cstdint>
#include <vector>

namespace as_components {
namespace fcgi {

// Protocol Constants
extern const int                       FCGI_LISTENSOCK_FILENO;
extern const std::int_fast32_t         FCGI_HEADER_LEN;
extern const std::uint8_t              FCGI_VERSION_1;
extern const std::uint16_t             FCGI_NULL_REQUEST_ID;
  // Flags
extern const std::uint8_t              FCGI_KEEP_CONN;
  // Roles
extern const std::uint16_t             FCGI_RESPONDER;
extern const std::uint16_t             FCGI_AUTHORIZER;
extern const std::uint16_t             FCGI_FILTER;
  // Protocol status values for FCGI_END_REQUEST records.
extern const std::uint8_t              FCGI_REQUEST_COMPLETE;
extern const std::uint8_t              FCGI_CANT_MPX_CONN;
extern const std::uint8_t              FCGI_OVERLOADED;
extern const std::uint8_t              FCGI_UNKNOWN_ROLE;
  // Default FCGI_GET_VALUES record variables.
  // (As character byte sequences encoded in ASCII/UTF-8.)
extern const std::vector<std::uint8_t> FCGI_MAX_CONNS;
extern const std::vector<std::uint8_t> FCGI_MAX_REQS;
extern const std::vector<std::uint8_t> FCGI_MPXS_CONNS;
  // Header byte position definitions
extern const std::int_fast32_t         kHeaderVersionIndex;
extern const std::int_fast32_t         kHeaderTypeIndex;
extern const std::int_fast32_t         kHeaderRequestIDB1Index;
extern const std::int_fast32_t         kHeaderRequestIDB0Index;
extern const std::int_fast32_t         kHeaderContentLengthB1Index;
extern const std::int_fast32_t         kHeaderContentLengthB0Index;
extern const std::int_fast32_t         kHeaderPaddingLengthIndex;
extern const std::int_fast32_t         kHeaderReservedByteIndex;
  // FCGI_BEGIN_REQUEST byte position definitions
extern const std::int_fast32_t         kBeginRequestRoleB1Index;
extern const std::int_fast32_t         kBeginRequestRoleB0Index;
extern const std::int_fast32_t         kBeginRequestFlagsIndex;
  // FCGI_END_REQUEST byte position definitions
extern const std::int_fast32_t         kEndRequestAppStatusB3Index;
extern const std::int_fast32_t         kEndRequestAppStatusB2Index;
extern const std::int_fast32_t         kEndRequestAppStatusB1Index;
extern const std::int_fast32_t         kEndRequestAppStatusB0Index;
extern const std::int_fast32_t         kEndRequestProtocolStatusIndex;
  // Maximum lengths for some record fields.
extern const std::int_fast32_t         kNameValuePairSingleByteLength;
extern const std::int_fast32_t         kNameValuePairFourByteLength;
extern const std::int_fast32_t         kMaxRecordContentByteLength;

// Enum class for header type values.
enum class FcgiType : std::uint8_t
{
  kFCGI_BEGIN_REQUEST     = 1U,       // From client.
  kFCGI_ABORT_REQUEST     = 2U,       // From client.
  kFCGI_END_REQUEST       = 3U,
  kFCGI_PARAMS            = 4U,       // From client.
  kFCGI_STDIN             = 5U,       // From client.
  kFCGI_STDOUT            = 6U,
  kFCGI_STDERR            = 7U,
  kFCGI_DATA              = 8U,       // From client.
  kFCGI_GET_VALUES        = 9U,       // From client.
  kFCGI_GET_VALUES_RESULT = 10U,
  kFCGI_UNKNOWN_TYPE      = 11U
};

} // namespace fcgi
} // namespace as_components

#endif // AS_COMPONENTS_FCGI_INCLUDE_FCGI_PROTOCOL_CONSTANTS_H_
