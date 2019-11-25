
#include <map>
#include <mutex>
#include <utility>
#include <cstdlib>         // For uint8_t.

namespace fcgi_synchronous_interface {

// Protocol Constants
constexpr int                        FCGI_LISTENSOCK_FILENO {0};
constexpr int                        FCGI_HEADER_LEN        {8};
constexpr uint8_t                    FCGI_VERSION_1         {1};
constexpr uint16_t                   FCGI_NULL_REQUEST_ID   {0};
constexpr uint8_t                    FCGI_KEEP_CONN         {1};
constexpr uint16_t                   FCGI_RESPONDER         {1};
constexpr uint16_t                   FCGI_AUTHORIZER        {2};
constexpr uint16_t                   FCGI_FILTER            {3};
constexpr uint8_t                    FCGI_REQUEST_COMPLETE  {0};
constexpr uint8_t                    FCGI_CANT_MPX_CONN     {1};
constexpr uint8_t                    FCGI_OVERLOADED        {2};
constexpr uint8_t                    FCGI_UNKNOWN_ROLE      {3};
constexpr std::basic_string<uint8_t> FCGI_MAX_CONNS         {"FCGI_MAX_CONNS"};
constexpr std::basic_string<uint8_t> FCGI_MAX_REQS          {"FCGI_MAX_REQS"};
constexpr std::basic_string<uint8_t> FCGI_MPXS_CONNS        {"FCGI_MPXS_CONNS"};

// Implementation Constants
constexpr uint8_t                    kNameValuePairSingleByteLength {std::exp2(7) - 1};
constexpr uint32_t                   kNameValuePairFourByteLength {std::exp2(31) - 1};
constexpr uint16_t                   kMaxRecordContentByteLength {std::exp2(16) - 1};

// Enum class for header type values.
// Record types as defined by FastCGI.
enum class FCGIType : uint8_t
{
  kFCGI_BEGIN_REQUEST = 1,              // From client.
  kFCGI_ABORT_REQUEST = 2,              // From client.
  kFCGI_END_REQUEST = 3,
  kFCGI_PARAMS = 4,                     // From client.
  kFCGI_STDIN = 5,                      // From client.
  kFCGI_STDOUT = 6,
  kFCGI_STDERR = 7,
  kFCGI_DATA = 8,                       // From client.
  kFCGI_GET_VALUES = 9,                 // From client.
  kFCGI_GET_VALUES_RESULT = 10,
  kFCGI_UNKNOWN_TYPE = 11,
};

class RequestIdentifier {
public:
  int descriptor();
  int FCGI_id();

  RequestIdentifier() = default;
  RequestIdentifier(int descriptor, int FCGI_id);
  RequestIdentifier(const RequestIdentifier& request_id) = default;
  RequestIdentifier(RequestIdentifier&& request_id) = default;

  RequestIdentifier& operator=(const RequestIdentifier& request_id) = default;
  RequestIdentifier& operator=(RequestIdentifier&& request_id) = default;
  operator bool();
  bool operator<(const RequestIdentifier& lhs, const RequestIdentifier& rhs);

  ~RequestIdentifier() = default;

private:
  std::pair<int, int> pair_;
};

enum class RequestStatus {kRequestPending, kRequestAssigned};

class RequestData {
public:
  friend fcgi_synchronous_interface::
         FCGIRequest(fcgi_synchronous_interface::RequestIdentifier);

  using size = std::allocator_traits<std::allocator<uint8_t>>::size_type;

  bool get_abort() const;
  void set_abort();

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
