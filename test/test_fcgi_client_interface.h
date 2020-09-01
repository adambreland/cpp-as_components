#ifndef FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
#define FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_

// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <sys/socket.h>

#include <cstdlib>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "fcgi_si.h"

namespace fcgi_si_test {

class ServerEvent
{
 public:
  virtual fcgi_si::RequestIdentifier RequestId() = 0;
  virtual ~ServerEvent();
};

class LocalFcgiResponse : public ServerEvent
{
 public:

 private:
  fcgi_si::RequestIdentifier request_id;
  bool                       complete_response;
  std::vector<std::uint8_t>  fcgi_stdout;
  std::vector<std::uint8_t>  fcgi_stderr;
  std::int32_t               app_status;
  std::uint8_t               protocol_status;
};

class InvalidRecord : public ServerEvent
{

};

class GetValuesResult : public ServerEvent
{

};

class UnknownType : public ServerEvent
{

};

class OtherManagementResponse : public ServerEvent
{

};

class TestFcgiClientInterface
{
 public:
  using ParamsMap = std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;

  // 1) Detect the address domain, create a socket of the detected domain, and
  //    attempt to connect to the specified address and port pair.
  // 2) Return the file descriptor of the socket on success and -1 on failure.
  int ConnectToServer(const char* address, std::uint16_t port);

  bool SendGetValuesRequest(int connection, const ParamsMap& params_map);
  bool SendBinaryManagementRequest(int connection, const std::uint8_t* byte_ptr,
    std::size_t length);

  struct FcgiRequest
  {
    std::uint16_t       role;
    bool                keep_conn;
    const ParamsMap&    params_map;
    const std::uint8_t* fcgi_stdin_ptr;
    std::size_t         stdin_length;
    const std::uint8_t* fcgi_data_ptr;
    std::size_t         data_length;
  };

  fcgi_si::RequestIdentifier SendRequest(int connection, 
    const FcgiRequest& request);
  bool SendAbortRequest(fcgi_si::RequestIdentifier);

  bool CloseConnection(int connection);

  std::vector<std::unique_ptr<ServerEvent>> ReceiveResponses();
};

} // fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
