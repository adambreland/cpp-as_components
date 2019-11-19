
extern "C"
{
  #include <sys/types.h>   // For ssize_t.
}

#include <map>
#include <cstdlib>         // For uint8_t.

#ifndef FCGI_SYNCRHONOUS_INTERFACE_DATA_TYPES_H_
#define FCGI_SYNCRHONOUS_INTERFACE_DATA_TYPES_H_
#include "fcgi_synchronous_interface_data_types.h"
#endif

namespace fcgi_synchronous_interface {

class FCGIRequest {
public:
  friend std::vector<FCGIRequest> AcceptRequests();

  const std::map<std::basic_string<uint8_t>, std::basic_string<uint8_t>>&
  get_environment_map() const;

  void Write(std::basic_string<uint8_t> response) const;
  void WriteError(std::basic_string<uint8_t> error_message) const;

  ssize_t SendFile(int in_fd, off_t* offset_ptr, size_t count) const;
  ssize_t SendFile(std::string pathname) const;

  void AbortRespond(int status) const;

  void Complete();

  // No copy or default construction.
  FCGIRequest() = delete;

  FCGIRequest(const FCGIRequest&) = delete;
  FCGIRequest(FCGIRequest&&);

  FCGIRequest& operator=(const FCGIRequest&) = delete;
  FCGIRequest& operator=(FCGIRequest&&);

  ~FCGIRequest() = default;

private:
  // Constructors made private as only an FCGIApplicationInterface object
  // should create FCGIRequest objects.

  // The boolean argument indicates that an abort request should be
  // constructed. Use true, though false results in identical behavior.
  FCGIRequest(RequestIdentifier request_id, bool abort_placeholder);

  FCGIRequest(RequestIdentifier request_id);

  RequestIdentifier request_identifier_;

  std::map<std::basic_string<uint8_t>,
           std::basic_string<uint8_t>> environment_map_;
  std::basic_string<uint8_t> request_stdin_content_;
  std::basic_string<uint8_t> request_data_content_;

  // For inspection of the role requested by the client server.
  uint16_t role_;

  // A flag to indicate that a request which was already assigned should be
  // aborted by the application.
  bool abort_;

  // A flag to inform the call to Complete() that the connection associated
  // with the request should be closed by the interface.
  bool close_connection_;

  // Allows preventing calls to Complete() after the first from having
  // an effect.
  bool completed_;
};

} // namespace fcgi_synchronous_interface
