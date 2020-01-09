#ifndef FCGI_APPLICATION_INTERFACE_DATA_TYPES_H_
#define FCGI_APPLICATION_INTERFACE_DATA_TYPES_H_

// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint8_t.
// C++ standard headers.
#include <map>
#include <mutex>
#include <utility>

namespace fcgi_si {

// Protocol Constants
  // General
constexpr int                        FCGI_LISTENSOCK_FILENO         {0};
constexpr int                        FCGI_HEADER_LEN                {8};
constexpr uint8_t                    FCGI_VERSION_1                 {1};
constexpr uint16_t                   FCGI_NULL_REQUEST_ID           {0};
  // Flags
constexpr uint8_t                    FCGI_KEEP_CONN                 {1};
  // Roles
constexpr uint16_t                   FCGI_RESPONDER                 {1};
constexpr uint16_t                   FCGI_AUTHORIZER                {2};
constexpr uint16_t                   FCGI_FILTER                    {3};
  // Protocol status values for FCGI_END_REQUEST records.
constexpr uint8_t                    FCGI_REQUEST_COMPLETE          {0};
constexpr uint8_t                    FCGI_CANT_MPX_CONN             {1};
constexpr uint8_t                    FCGI_OVERLOADED                {2};
constexpr uint8_t                    FCGI_UNKNOWN_ROLE              {3};
  // Default FCGI_GET_VALUES record variables.
const     std::string                FCGI_MAX_CONNS                 {"FCGI_MAX_CONNS"};
const     std::string                FCGI_MAX_REQS                  {"FCGI_MAX_REQS"};
const     std::string                FCGI_MPXS_CONNS                {"FCGI_MPXS_CONNS"};

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

class RequestIdentifier {
public:
  int descriptor();
  uint16_t FCGI_id();

  RequestIdentifier() = default;
  RequestIdentifier(int descriptor, uint16_t FCGI_id);
  RequestIdentifier(const RequestIdentifier& request_id) = default;
  RequestIdentifier(RequestIdentifier&& request_id) = default;

  RequestIdentifier& operator=(const RequestIdentifier& request_id) = default;
  RequestIdentifier& operator=(RequestIdentifier&& request_id) = default;
  operator bool();
  bool operator<(const RequestIdentifier& rhs);

  ~RequestIdentifier() = default;

private:
  std::pair<int, uint16_t> pair_;
};

enum class RequestStatus {kRequestPending, kRequestAssigned};

class RequestData {
public:
  // TODO Add a member function which is essentially a helper function
  // for FCGIRequest.

  using size = std::allocator_traits<std::allocator<uint8_t>>::size_type;

  bool get_abort() const;
  void set_abort();

  bool get_close_connection() const;
  uint16_t get_role() const;

  bool IsRequestComplete() const;

  fcgi_si::RequestStatus get_status() const;

  bool ProcessFCGI_PARAMS();

  bool get_PARAMS_completion() const;
  void CompletePARAMS();
  void AppendToPARAMS(const uint8_t* buffer_ptr, size count);

  bool get_STDIN_completion() const;
  void CompleteSTDIN();
  void AppendToSTDIN(const uint8_t* buffer_ptr, size count);

  bool get_DATA_completion() const;
  void CompleteDATA();
  void AppendToDATA(const uint8_t* buffer_ptr, size count);

  // No copy or move.
  RequestData() = delete;
  RequestData(uint16_t role, bool close_connection);
  RequestData(const RequestData&) = delete;
  RequestData(RequestData&&) = delete;

  RequestData& operator=(const RequestData&) = delete;
  RequestData& operator=(RequestData&&) = delete;

  ~RequestData() = default;

private:
  // Make FCGIRequest a friend class as its constructor will move data
  // from the representation of RequestData objects.
  friend class FCGIRequest;

  // Request data and completion status
  bool FCGI_PARAMS_complete_;
  bool FCGI_STDIN_complete_;
  bool FCGI_DATA_complete_;
  std::basic_string<uint8_t> FCGI_PARAMS_;
  std::basic_string<uint8_t> FCGI_STDIN_;
  std::basic_string<uint8_t> FCGI_DATA_;

  // Map to hold processed FCGI_PARAMS_ data.
  std::map<std::basic_string<uint8_t>, std::basic_string<unint8_t>>
    environment_map_;

  // Request metadata
  uint16_t role_;
  bool abort_;
  bool close_connection_;
  RequestStatus request_status_;
};

// Returns the length in bytes of a name or value when it is encoded
// using four bytes in the FastCGI name-value pair encoding. Names and
// values are variable length byte arrays.
//
// Parameters:
// content_ptr: points to the first byte of the byte sequence which
// determines the length of the corresponding name or value byte sequence.
//
// Requires:
// 1) The byte pointed to by content_ptr and the next three bytes constitute
//    a four-byte length as per the FastCGI name-value encoding.
//
// Effects:
// 1) The value returned is the length in bytes of the corresponding name or
//    value byte array.
uint32_t ExtractFourByteLength(const uint8_t* content_ptr) const;

void EncodeFourByteLength(uint32_t length, std::basic_string<uint8_t>* string_ptr);

// Extracts a collection of name-value pairs when they are encoded as a
// sequence of bytes in the FastCGI name-value pair encoding.
// Note: Checking if content_length is zero before calling allows for
// the detection of an empty collection of name-value pairs.
//
// Parameters:
// content_length: the total size of the sequence of bytes which constitutes
// the collection of name-value pairs.
// content_ptr: points to the first byte of the byte sequence.
//
// Requires:
// 1) The value of content_length is exactly equal to the number of bytes
//    which represent the collection of name-value parirs. This number does
//    not include the byte length of a FastCGI record header.
//
// Effects:
// 1) If a sequential application of the encoding rules to the encountered
//    length values gives a length which is equal to content_length, a vector
//    is returned of the name-value pairs extracted from content_length bytes.
//    The pairs are of type:
//    std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>.
// 2) If content_length was not long enough for the extracted sequence of
//    name-value pairs, an empty vector is returned.
std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
ProcessBinaryNameValuePairs(int content_length, const uint8_t* content_ptr);

} // namespace fcgi_si

#endif // FCGI_APPLICATION_INTERFACE_DATA_TYPES_H_
