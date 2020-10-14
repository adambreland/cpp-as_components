#ifndef FCGI_SERVER_INTERFACE_INCLUDE_PROTOCOL_CONSTANTS_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_PROTOCOL_CONSTANTS_H_

#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>

#include <cstdint>         // For uint8_t.
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace fcgi_si {

// Protocol Constants
constexpr int                          FCGI_LISTENSOCK_FILENO         {0};
constexpr std::int_fast32_t            FCGI_HEADER_LEN                {8};
constexpr std::uint8_t                 FCGI_VERSION_1                 {1U};
constexpr std::uint16_t                FCGI_NULL_REQUEST_ID           {0U};
  // Flags
constexpr std::uint8_t                 FCGI_KEEP_CONN                 {1U};
  // Roles
constexpr std::uint16_t                FCGI_RESPONDER                 {1U};
constexpr std::uint16_t                FCGI_AUTHORIZER                {2U};
constexpr std::uint16_t                FCGI_FILTER                    {3U};
  // Protocol status values for FCGI_END_REQUEST records.
constexpr std::uint8_t                 FCGI_REQUEST_COMPLETE          {0U};
constexpr std::uint8_t                 FCGI_CANT_MPX_CONN             {1U};
constexpr std::uint8_t                 FCGI_OVERLOADED                {2U};
constexpr std::uint8_t                 FCGI_UNKNOWN_ROLE              {3U};
  // Default FCGI_GET_VALUES record variables.
  // (As character byte sequences encoded in ASCII/UTF-8.)
const     std::vector<std::uint8_t>   FCGI_MAX_CONNS
  {'F', 'C', 'G', 'I', '_', 'M', 'A', 'X', '_', 'C', 'O', 'N', 'N', 'S'};
const     std::vector<std::uint8_t>   FCGI_MAX_REQS
  {'F', 'C', 'G', 'I', '_', 'M', 'A', 'X', '_', 'R', 'E', 'Q', 'S'};
const     std::vector<std::uint8_t>   FCGI_MPXS_CONNS
  {'F', 'C', 'G', 'I', '_', 'M', 'P', 'X', 'S', '_', 'C', 'O', 'N', 'N', 'S'};
  // Header byte position definitions
constexpr std::int_fast32_t            kHeaderVersionIndex            {0};
constexpr std::int_fast32_t            kHeaderTypeIndex               {1};
constexpr std::int_fast32_t            kHeaderRequestIDB1Index        {2};
constexpr std::int_fast32_t            kHeaderRequestIDB0Index        {3};
constexpr std::int_fast32_t            kHeaderContentLengthB1Index    {4};
constexpr std::int_fast32_t            kHeaderContentLengthB0Index    {5};
constexpr std::int_fast32_t            kHeaderPaddingLengthIndex      {6};
constexpr std::int_fast32_t            kHeaderReservedByteIndex       {7};
  // FCGI_BEGIN_REQUEST byte position definitions
constexpr std::int_fast32_t            kBeginRequestRoleB1Index       {0};
constexpr std::int_fast32_t            kBeginRequestRoleB0Index       {1};
constexpr std::int_fast32_t            kBeginRequestFlagsIndex        {2};
  // FCGI_END_REQUEST byte position definitions
constexpr std::int_fast32_t            kEndRequestAppStatusB3Index    {0};
constexpr std::int_fast32_t            kEndRequestAppStatusB2Index    {1};
constexpr std::int_fast32_t            kEndRequestAppStatusB1Index    {2};
constexpr std::int_fast32_t            kEndRequestAppStatusB0Index    {3};
constexpr std::int_fast32_t            kEndRequestProtocolStatusIndex {4};
  // Maximum lengths for some record fields.
constexpr std::int_fast32_t            kNameValuePairSingleByteLength {(1LU << 7)  - 1};
constexpr std::int_fast32_t            kNameValuePairFourByteLength   {(1LU << 31) - 1};
      // Use LU suffix to avoid overflow - a 32-bit signed integer cannot hold
      // 1 << 31.
constexpr std::int_fast32_t            kMaxRecordContentByteLength    {(1LU << 16) - 1};

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

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_PROTOCOL_CONSTANTS_H_
