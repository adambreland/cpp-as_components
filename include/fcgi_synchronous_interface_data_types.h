
#include <map>
#include <mutex>
#include <utility>
#include <cstdlib>         // For uint8_t.

namespace fcgi_synchronous_interface {

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
const     std::basic_string<uint8_t> FCGI_MAX_CONNS                 {"FCGI_MAX_CONNS"};
const     std::basic_string<uint8_t> FCGI_MAX_REQS                  {"FCGI_MAX_REQS"};
const     std::basic_string<uint8_t> FCGI_MPXS_CONNS                {"FCGI_MPXS_CONNS"};

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
constexpr uint8_t                    kNameValuePairSingleByteLength {(1 << 7)  - 1};
constexpr uint32_t                   kNameValuePairFourByteLength   {(1 << 31) - 1};
constexpr uint16_t                   kMaxRecordContentByteLength    {(1 << 16) - 1};

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
  bool operator<(const RequestIdentifier& lhs, const RequestIdentifier& rhs);

  ~RequestIdentifier() = default;

private:
  std::pair<int, uint16_t> pair_;
};

enum class RequestStatus {kRequestPending, kRequestAssigned};

class RequestData {
public:
  friend fcgi_synchronous_interface::
         FCGIRequest(fcgi_synchronous_interface::RequestIdentifier);

  using size = std::allocator_traits<std::allocator<uint8_t>>::size_type;

  bool get_abort() const;
  void set_abort();

  bool get_close_connection() const;
  uint16_t get_role() const;

  bool IsRequestComplete() const;

  fcgi_synchronous_interface::RequestStatus get_status() const;

  bool get_PARAMS_completion() const;
  void CompletePARAMS();
  void AppendToPARAMS(const uint8_t* buffer_ptr, size count)

  bool get_STDIN_completion() const;
  void CompleteSTDIN();
  void AppenToSTDIN(const uint8_t* buffer_ptr, size count);

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

  ~RequestData = default;

private:
  // request data and completion status
  bool FCGI_PARAMS_complete_;
  bool FCGI_STDIN_complete_;
  bool FCGI_DATA_complete_;
  std::basic_string<uint8_t> FCGI_PARAMS_;
  std::basic_string<uint8_t> FCGI_STDIN_;
  std::basic_string<uint8_t> FCGI_DATA_;

  // request metadata
  uint16_t role_;
  bool abort_;
  bool close_connection_;
  RequestStatus request_status_;
};

} // namespace fcgi_synchronous_interface
