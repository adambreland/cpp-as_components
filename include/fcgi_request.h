#ifndef FCGI_APPLICATION_INTERFACE_FCGI_REQUEST_H_
#define FCGI_APPLICATION_INTERFACE_FCGI_REQUEST_H_

// Unix type declarations.
extern "C"
{
  #include <sys/types.h>   // For ssize_t.
}

// C standard library headers present in the C++ standard library.
#include <cstdlib>         // For uint8_t.
// C++ standard headers.
#include <map>


namespace fcgi_synchronous_interface {

class FCGIRequest {
public:
  const std::map<std::basic_string<uint8_t>, std::basic_string<uint8_t>>&
  get_environment_map() const;
  const std::basic_string<uint8_t>& get_STDIN() const;
  const std::basic_string<uint8_t>& get_DATA() const;

  bool get_abort() const;
  uint16_t get_role() const;

  void Write(std::basic_string<uint8_t> response) const;
  void WriteError(std::basic_string<uint8_t> error_message) const;

  ssize_t SendFile(int in_fd, off_t* offset_ptr, size_t count) const;
  ssize_t SendFile(std::string pathname) const;

  void Complete(int32_t app_status);

  // No copy or default construction.
  FCGIRequest() = delete;

  FCGIRequest(const FCGIRequest&) = delete;
  FCGIRequest(FCGIRequest&&);

  FCGIRequest& operator=(const FCGIRequest&) = delete;
  FCGIRequest& operator=(FCGIRequest&&);

  ~FCGIRequest() = default;

private:
  friend std::vector<FCGIRequest> AcceptRequests();

  // Constructor made private as only an FCGIApplicationInterface object
  // should create FCGIRequest objects.
  FCGIRequest(RequestIdentifier request_id);

  RequestIdentifier request_identifier_;

  std::map<std::basic_string<uint8_t>,
           std::basic_string<uint8_t>> environment_map_;
  std::basic_string<uint8_t> request_stdin_content_;
  std::basic_string<uint8_t> request_data_content_;

  // For inspection of the role requested by the client server.
  uint16_t role_;

  // A flag to inform the call to Complete() that the connection associated
  // with the request should be closed by the interface.
  bool close_connection_;

  // Forces the object to act as if it is null. Calls will return null
  // values (empty containers, false) or have no effect (e.g. a second
  // call to complete).
  bool completed_;
};

} // namespace fcgi_synchronous_interface

#endif // FCGI_APPLICATION_INTERFACE_FCGI_REQUEST_H_
