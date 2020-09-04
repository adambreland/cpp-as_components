#ifndef FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
#define FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_

// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <sys/socket.h>

#include <cstdlib>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "external/id_manager/include/id_manager.h"

#include "fcgi_si.h"

namespace fcgi_si_test {

using ParamsMap = std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;


// ServerEvent and derived classes.

class ServerEvent
{
 public:
  virtual fcgi_si::RequestIdentifier RequestId() const = 0;
  virtual ~ServerEvent();
};

class ConnectionClosure : public ServerEvent
{
  public:
    inline fcgi_si::RequestIdentifier RequestId() const noexcept override
    {
      return connection_holder_;
    }

    inline ConnectionClosure() noexcept
    : connection_holder_ {-1, 0U}
    {}

    inline ConnectionClosure(int connection) noexcept
    : connection_holder_ {connection, 0U}
    {}

    ConnectionClosure(const ConnectionClosure&) = default;
    ConnectionClosure(ConnectionClosure&&) = default;

    ConnectionClosure& operator=(const ConnectionClosure&) = default;
    ConnectionClosure& operator=(ConnectionClosure&&) = default;

    ~ConnectionClosure() override = default;

  private:
    fcgi_si::RequestIdentifier connection_holder_;
};

class FcgiResponse : public ServerEvent
{
 public:
  inline std::int32_t AppStatus() const noexcept
  {
    return app_status_;
  }

  inline const std::vector<std::uint8_t>& FcgiStderr() const noexcept
  {
    return fcgi_stderr_;
  }

  inline const std::vector<std::uint8_t>& FcgiStdout() const noexcept
  {
    return fcgi_stdout_;
  }

  inline std::uint8_t ProtocolStatus() const noexcept
  {
    return protocol_status_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }

  inline FcgiResponse()
  : app_status_      {0},
    fcgi_stderr_     {},
    fcgi_stdout_     {},
    protocol_status_ {0U},
    request_id_      {-1, 0U}
  {}

  inline FcgiResponse(std::int32_t app_status, 
    const std::vector<std::uint8_t>& stderr, 
    const std::vector<std::uint8_t>& stdout,
    std::uint8_t protocol_status, 
    fcgi_si::RequestIdentifier request_id)
  : app_status_      {app_status},
    fcgi_stderr_     {stderr},
    fcgi_stdout_     {stdout},
    protocol_status_ {protocol_status},
    request_id_ {request_id}
  {}

  inline FcgiResponse(std::int32_t app_status, 
    std::vector<std::uint8_t>&& stderr, 
    std::vector<std::uint8_t>&& stdout,
    std::uint8_t protocol_status, 
    fcgi_si::RequestIdentifier request_id) noexcept
  : app_status_      {app_status},
    fcgi_stderr_     {std::move(stderr)},
    fcgi_stdout_     {std::move(stdout)},
    protocol_status_ {protocol_status},
    request_id_ {request_id}
  {}

  FcgiResponse(const FcgiResponse&) = default;
  FcgiResponse(FcgiResponse&&) = default;

  FcgiResponse& operator=(const FcgiResponse&) = default;
  FcgiResponse& operator=(FcgiResponse&&) = default;

  ~FcgiResponse() override = default;

 private:
  std::int32_t               app_status_;
  std::vector<std::uint8_t>  fcgi_stderr_;
  std::vector<std::uint8_t>  fcgi_stdout_;
  std::uint8_t               protocol_status_;
  fcgi_si::RequestIdentifier request_id_;
};

class InvalidRecord : public ServerEvent
{
 public:
  inline const std::vector<std::uint8_t>& Content() const noexcept
  {
    return content_;
  }

  inline std::uint8_t PaddingLength() const noexcept
  {
    return padding_length_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }

  inline fcgi_si::FcgiType Type() const noexcept
  {
    return type_;
  }
  
  inline std::uint8_t Version() const noexcept
  {
    return version_;
  }

  inline InvalidRecord()
  : version_        {0U},
    type_           {static_cast<fcgi_si::FcgiType>(0U)},
    request_id_     {-1, 0U},
    content_        {},
    padding_length_ {0U}
  {}

  inline InvalidRecord(std::uint8_t version, fcgi_si::FcgiType type,
    fcgi_si::RequestIdentifier request_id, 
    const std::vector<std::uint8_t>& content, std::uint8_t padding_length)
  : version_        {version},
    type_           {type},
    request_id_     {request_id},
    content_        {content},
    padding_length_ {padding_length}
  {}

  inline InvalidRecord(std::uint8_t version, fcgi_si::FcgiType type,
    fcgi_si::RequestIdentifier request_id, 
    std::vector<std::uint8_t>&& content, std::uint8_t padding_length) noexcept
  : version_        {version},
    type_           {type},
    request_id_     {request_id},
    content_        {std::move(content)},
    padding_length_ {padding_length}
  {}

  InvalidRecord(const InvalidRecord&) = default;
  InvalidRecord(InvalidRecord&&) = default;

  InvalidRecord& operator=(const InvalidRecord&) = default;
  InvalidRecord& operator=(InvalidRecord&&) = default;

  ~InvalidRecord() override = default;

 private:
  std::uint8_t               version_;
  fcgi_si::FcgiType          type_;
  fcgi_si::RequestIdentifier request_id_;
  std::vector<std::uint8_t>  content_;
  std::uint8_t               padding_length_;
};

class GetValuesResult : public ServerEvent
{
 public:
  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }

  inline const ParamsMap& Result() const noexcept
  {
    return params_map_;
  }
 
  inline GetValuesResult()
  : request_id_ {-1, 0U},
    params_map_ {}
  {}

  inline GetValuesResult(fcgi_si::RequestIdentifier request_id, 
    const ParamsMap& pm)
  : request_id_ {request_id},
    params_map_ {pm}
  {}

  inline GetValuesResult(fcgi_si::RequestIdentifier request_id, ParamsMap&& pm)
  : request_id_ {request_id},
    params_map_ {std::move(pm)}
  {}

  GetValuesResult(const GetValuesResult&) = default;
  GetValuesResult(GetValuesResult&&) = default;
  
  GetValuesResult& operator=(const GetValuesResult&) = default;
  GetValuesResult& operator=(GetValuesResult&&) = default;

  ~GetValuesResult() override = default;

 private:
  fcgi_si::RequestIdentifier request_id_;
  ParamsMap                  params_map_;
};

class UnknownType : public ServerEvent
{
 public:
  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }

  inline std::uint8_t Type() const noexcept
  {
    return unknown_type_;
  }

  inline UnknownType() noexcept
  : request_id_   {-1, 0U},
    unknown_type_ {0U}
  {}

  inline UnknownType(fcgi_si::RequestIdentifier request_id,
    std::uint8_t type) noexcept
  : request_id_   {request_id},
    unknown_type_ {type}
  {}

  UnknownType(const UnknownType&) = default;
  UnknownType(UnknownType&&) = default;
  
  UnknownType& operator=(const UnknownType&) = default;
  UnknownType& operator=(UnknownType&&) = default;

  ~UnknownType() override = default;

 private:
  fcgi_si::RequestIdentifier request_id_;
  std::uint8_t               unknown_type_;
};

class OtherManagementResponse : public ServerEvent
{
 public:
  inline const std::vector<std::uint8_t>& Content() const noexcept
  {
    return content_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept
  {
    return request_id_;
  }

  inline OtherManagementResponse()
  : request_id_ {-1, 0U},
    content_    {}
  {}

  inline OtherManagementResponse(fcgi_si::RequestIdentifier request_id, 
    const std::vector<std::uint8_t>& content)
  : request_id_ {request_id},
    content_    {content}
  {}

  inline OtherManagementResponse(fcgi_si::RequestIdentifier request_id, 
    std::vector<std::uint8_t>&& content) noexcept
  : request_id_ {request_id},
    content_    {std::move(content)}
  {}

  OtherManagementResponse(const OtherManagementResponse&) = default;
  OtherManagementResponse(OtherManagementResponse&&) = default;
  
  OtherManagementResponse& operator=(const OtherManagementResponse&) = default;
  OtherManagementResponse& operator=(OtherManagementResponse&&) = default;

  ~OtherManagementResponse() override = default;

 private:
  fcgi_si::RequestIdentifier request_id_;
  std::vector<std::uint8_t>  content_;
};


// The interface class.

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

class TestFcgiClientInterface
{
 public:
  // 1) Detect the address domain, create a socket of the detected domain, and
  //    attempt to connect to the specified address and port pair.
  // 2) Return the file descriptor of the socket on success and -1 on failure.
  int Connect(const char* address, std::uint16_t port);

  bool SendGetValuesRequest(int connection, const ParamsMap& params_map);
  bool SendBinaryManagementRequest(int connection, const std::uint8_t* byte_ptr,
    std::size_t length);

  fcgi_si::RequestIdentifier SendRequest(int connection, 
    const FcgiRequest& request);
  bool SendAbortRequest(fcgi_si::RequestIdentifier);
  bool ReleaseId(fcgi_si::RequestIdentifier id);
  bool ReleaseId(int connection);

  bool CloseConnection(int connection);

  std::vector<std::unique_ptr<ServerEvent>> ReceiveResponses();

 private:
  std::map<int, a_component::IdManager> id_manager_map_;
};

} // fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
