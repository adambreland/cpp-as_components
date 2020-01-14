#ifndef FCGI_APPLICATION_INTERFACE_FCGI_REQUEST_H_
#define FCGI_APPLICATION_INTERFACE_FCGI_REQUEST_H_

// Unix type declarations.
#include <sys/types.h>     // For ssize_t.

// C standard library headers in the C++ standard library.
#include <cstdint>         // For uint8_t.
// C++ standard headers.
#include <map>
#include <mutex>
#include <vector>

namespace fcgi_si {

// Forward declaration to break cyclic dependency between FCGIRequest
// and FCGIApplicationInterface includes.
class FCGIApplicationInterface;

class FCGIRequest {
public:
  const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
  get_environment_map() const;
  const std::vector<uint8_t>& get_STDIN() const;
  const std::vector<uint8_t>& get_DATA() const;

  bool get_abort() const;
  uint16_t get_role() const;
  bool get_completion_status() const;

  bool Write(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter);
  bool WriteError(std::vector<uint8_t> error_message);

  ssize_t SendFile(int in_fd, off_t* offset_ptr, size_t count);
  ssize_t SendFile(std::string pathname);

  void Complete(int32_t app_status);

  // No copy or default construction.
  FCGIRequest() = delete;

  FCGIRequest(const FCGIRequest&) = delete;
  FCGIRequest(FCGIRequest&&);

  FCGIRequest& operator=(const FCGIRequest&) = delete;
  FCGIRequest& operator=(FCGIRequest&&);

  ~FCGIRequest() = default;

private:
  friend class fcgi_si::FCGIApplicationInterface;

  // Constructor made private as only an FCGIApplicationInterface object
  // should create FCGIRequest objects through calls to AcceptRequests().
  FCGIRequest(fcgi_si::RequestIdentifier request_id,
    fcgi_si::FCGIApplicationInterface* interface_ptr,
    fcgi_si::RequestData* request_data_ptr,
    std::mutex* write_mutex_ptr, std::mutex* interface_state_mutex_ptr);

  void SetComplete();

  void WriteHelper(const uint8_t* message_ptr, uint16_t count)

  fcgi_si::FCGIApplicationInterface* interface_ptr_;
  fcgi_si::RequestIdentifier request_identifier_;

  std::map<std::vector<uint8_t>, std::vector<uint8_t>> environment_map_;
  std::vector<uint8_t> request_stdin_content_;
  std::vector<uint8_t> request_data_content_;

  // For inspection of the role requested by the client server.
  uint16_t role_;

  // A flag to inform the call to Complete() that the connection associated
  // with the request should be closed by the interface.
  bool close_connection_;

  // A local abort value that is populated when an abort is detected by
  // the discovery of a closed connection or by inspection with get_abort().
  bool was_aborted_;

  // Forces the object to act as if it is null. Calls will return null
  // values (empty containers, false) or have no effect (e.g. a second
  // call to complete).
  bool completed_;

  // Synchronization
  std::mutex* write_mutex_ptr_;
  std::mutex* interface_state_mutex_ptr_;
};

} // namespace fcgi_si

#endif // FCGI_APPLICATION_INTERFACE_FCGI_REQUEST_H_
