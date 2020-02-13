#ifndef FCGI_SERVER_INTERFACE_INCLUDE_PROTOCOL_CONSTANTS_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_PROTOCOL_CONSTANTS_H_

#include <cstdint>         // For uint8_t.

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace fcgi_si {

// Protocol Constants
  // General
constexpr int                          FCGI_LISTENSOCK_FILENO         {0};
constexpr int                          FCGI_HEADER_LEN                {8};
constexpr uint8_t                      FCGI_VERSION_1                 {1};
constexpr uint16_t                     FCGI_NULL_REQUEST_ID           {0};
  // Flags
constexpr uint8_t                      FCGI_KEEP_CONN                 {1};
  // Roles
constexpr uint16_t                     FCGI_RESPONDER                 {1};
constexpr uint16_t                     FCGI_AUTHORIZER                {2};
constexpr uint16_t                     FCGI_FILTER                    {3};
  // Protocol status values for FCGI_END_REQUEST records.
constexpr uint8_t                      FCGI_REQUEST_COMPLETE          {0};
constexpr uint8_t                      FCGI_CANT_MPX_CONN             {1};
constexpr uint8_t                      FCGI_OVERLOADED                {2};
constexpr uint8_t                      FCGI_UNKNOWN_ROLE              {3};
  // Default FCGI_GET_VALUES record variables.
  // (As character byte sequences encoded in ASCII/UTF-8.)
const     std::vector<uint8_t>   FCGI_MAX_CONNS
  {'F', 'C', 'G', 'I', '_', 'M', 'A', 'X', '_', 'C', 'O', 'N', 'N', 'S'};
const     std::vector<uint8_t>   FCGI_MAX_REQS
  {'F', 'C', 'G', 'I', '_', 'M', 'A', 'X', '_', 'R', 'E', 'Q', 'S'};
const     std::vector<uint8_t>   FCGI_MPXS_CONNS
  {'F', 'C', 'G', 'I', '_', 'M', 'P', 'X', 'S', '_', 'C', 'O', 'N', 'N', 'S'};

// Implementation Constants (determined by current protocol features)
  // Header byte position definitions
constexpr uint8_t                    kHeaderVersionIndex            {0};
constexpr uint8_t                    kHeaderTypeIndex               {1};
constexpr uint8_t                    kHeaderRequestIDB1Index        {2};
constexpr uint8_t                    kHeaderRequestIDB0Index        {3};
constexpr uint8_t                    kHeaderContentLengthB1Index    {4};
constexpr uint8_t                    kHeaderContentLengthB0Index    {5};
constexpr uint8_t                    kHeaderPaddingLengthIndex      {6};
constexpr uint8_t                    kHeaderReservedByteIndex       {7};
  // FCGI_BEGIN_REQUEST byte position definitions
constexpr uint8_t                    kBeginRequestRoleB1Index       {0};
constexpr uint8_t                    kBeginRequestRoleB0Index       {1};
constexpr uint8_t                    kBeginRequestFlagsIndex        {2};
  // Maximum lengths for some record fields.
constexpr uint8_t                    kNameValuePairSingleByteLength {(1LU << 7)  - 1};
constexpr uint32_t                   kNameValuePairFourByteLength   {(1LU << 31) - 1};
      // Use LU suffix to avoid overflow - a 32-bit signed integer cannot hold
      // 1 << 31.
constexpr uint16_t                   kMaxRecordContentByteLength(-1);
      // Largest possible value for the type per implicit conversions.

// Enum class for header type values.
enum class FCGIType : uint8_t
{
  kFCGI_BEGIN_REQUEST     = 1,       // From client.
  kFCGI_ABORT_REQUEST     = 2,       // From client.
  kFCGI_END_REQUEST       = 3,
  kFCGI_PARAMS            = 4,       // From client.
  kFCGI_STDIN             = 5,       // From client.
  kFCGI_STDOUT            = 6,
  kFCGI_STDERR            = 7,
  kFCGI_DATA              = 8,       // From client.
  kFCGI_GET_VALUES        = 9,       // From client.
  kFCGI_GET_VALUES_RESULT = 10,
  kFCGI_UNKNOWN_TYPE      = 11
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_PROTOCOL_CONSTANTS_H_
