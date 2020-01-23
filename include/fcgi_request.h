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
  bool AbortStatus();

  void Complete(int32_t app_status);

  inline bool get_completion_status() const
  {
    return completed_;
  }

  inline const std::vector<uint8_t>& get_DATA() const
  {
    return request_data_content_;
  }

  inline const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
  get_environment_map() const
  {
    return environment_map_;
  }

  inline uint16_t get_role() const
  {
    return role_;
  }

  inline const std::vector<uint8_t>& get_STDIN() const
  {
    return request_stdin_content_;
  }

  inline bool Write(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter)
  {
    return PartitionByteSequence(ref, begin_iter, end_iter,
      fcgi_si::FCGIType::kFCGI_STDOUT);
  }

  inline bool WriteError(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter)
  {
    return PartitionByteSequence(ref, begin_iter, end_iter,
      fcgi_si::FCGIType::kFCGI_STDERR);
  }

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

  bool PartitionByteSequence(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter, fcgi_si::FCGIType type);

  void CompleteAfterDiscoveredClosedConnection();

  bool WriteHelper(int fd, struct iovec* iovec_ptr, int iovec_count,
    std::size_t number_to_write, bool interface_mutex_held);

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
  // the discovery of a closed connection or by inspection with AbortStatus.
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
