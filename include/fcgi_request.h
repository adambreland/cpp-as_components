#ifndef FCGI_SERVER_INTERFACE_FCGI_REQUEST_H_
#define FCGI_SERVER_INTERFACE_FCGI_REQUEST_H_

#include <sys/types.h>     // For ssize_t.

#include <cstdint>         // For uint8_t.

#include <map>
#include <mutex>
#include <vector>

#include "include/protocol_constants.h"
#include "include/request_identifier.h"
#include "include/request_data.h"

namespace fcgi_si {

// Forward declaration to break cyclic dependency between FCGIRequest
// and FCGIServerInterface includes.
class FCGIServerInterface;

class FCGIRequest {
public:
  const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
  get_environment_map() const;
  const std::vector<uint8_t>& get_STDIN() const;
  const std::vector<uint8_t>& get_DATA() const;

  bool get_abort();
  uint16_t get_role() const;
  bool get_completion_status() const;

  bool Write(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter);
  bool WriteError(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter);

  void Complete(int32_t app_status);

  // No copy or default construction.
  FCGIRequest() = delete;

  FCGIRequest(const FCGIRequest&) = delete;
  FCGIRequest(FCGIRequest&&);

  FCGIRequest& operator=(const FCGIRequest&) = delete;
  FCGIRequest& operator=(FCGIRequest&&);

  ~FCGIRequest() = default;

private:
  friend class fcgi_si::FCGIServerInterface;

  // Constructor made private as only an FCGIServerInterface object
  // should create FCGIRequest objects through calls to AcceptRequests().
  FCGIRequest(fcgi_si::RequestIdentifier request_id,
    fcgi_si::FCGIServerInterface* interface_ptr,
    fcgi_si::RequestData* request_data_ptr,
    std::mutex* write_mutex_ptr, std::mutex* interface_state_mutex_ptr);

  void SetComplete();

  void PartitionByteSequence(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter, fcgi_si::FCGIType type);

  void WriteHelper(const uint8_t* message_ptr, uint16_t message_length,
    std::unique_lock<std::mutex>* lock_ptr, bool acquire_lock, bool release_lock);

  fcgi_si::FCGIServerInterface* interface_ptr_;
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

#endif // FCGI_SERVER_INTERFACE_FCGI_REQUEST_H_
