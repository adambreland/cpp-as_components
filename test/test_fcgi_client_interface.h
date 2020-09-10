#ifndef FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
#define FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_

// #include <arpa/inet.h>
// #include <netinet/in.h>
// #include <sys/socket.h>

#include <cstdlib>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include "external/id_manager/include/id_manager.h"

#include "fcgi_si.h"

namespace fcgi_si_test {

using ParamsMap = std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;

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

struct ManagementRequestData
{
  fcgi_si::FcgiType         type {};
  ParamsMap                 params_map {};
  std::vector<std::uint8_t> data {};
};

               ////// ServerEvent and derived classes. //////

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

  inline const FcgiRequest& Request() const noexcept
  {
    return request_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }

  // The absence of an FcgiRequest instance makes default construction
  // problematic due to the inclusion of a constant reference to a ParamsMap
  // instance.
  FcgiResponse() = delete;

  inline FcgiResponse(std::int32_t app_status, 
    const std::vector<std::uint8_t>& stderr, 
    const std::vector<std::uint8_t>& stdout,
    std::uint8_t protocol_status,
    const FcgiRequest& request, 
    fcgi_si::RequestIdentifier request_id)
  : app_status_      {app_status},
    fcgi_stderr_     {stderr},
    fcgi_stdout_     {stdout},
    protocol_status_ {protocol_status},
    request_         {request},
    request_id_      {request_id}
  {}

  inline FcgiResponse(std::int32_t app_status, 
    std::vector<std::uint8_t>&& stderr, 
    std::vector<std::uint8_t>&& stdout,
    std::uint8_t protocol_status,
    FcgiRequest&& request,
    fcgi_si::RequestIdentifier request_id) noexcept
  : app_status_      {app_status},
    fcgi_stderr_     {std::move(stderr)},
    fcgi_stdout_     {std::move(stdout)},
    protocol_status_ {protocol_status},
    request_         {std::move(request)},
    request_id_      {request_id}
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
  FcgiRequest                request_;
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
  inline const ParamsMap& RequestMap() const noexcept
  {
    return request_params_map_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }
 
  inline const ParamsMap& ResponseMap() const noexcept
  {
    return response_params_map_;
  }

  inline GetValuesResult()
  : request_id_ {-1, 0U},
    request_params_map_ {},
    response_params_map_ {}
  {}

  inline GetValuesResult(fcgi_si::RequestIdentifier request_id, 
    const ParamsMap& request, const ParamsMap& response)
  : request_id_ {request_id},
    request_params_map_ {request},
    response_params_map_ {response}
  {}

  inline GetValuesResult(fcgi_si::RequestIdentifier request_id, 
    ParamsMap&& request, ParamsMap&& response)
  : request_id_ {request_id},
    request_params_map_ {std::move(request)},
    response_params_map_ {std::move(response)}
  {}

  GetValuesResult(const GetValuesResult&) = default;
  GetValuesResult(GetValuesResult&&) = default;
  
  GetValuesResult& operator=(const GetValuesResult&) = default;
  GetValuesResult& operator=(GetValuesResult&&) = default;

  ~GetValuesResult() override = default;

 private:
  fcgi_si::RequestIdentifier request_id_;
  ParamsMap                  request_params_map_;
  ParamsMap                  response_params_map_;
};

class UnknownType : public ServerEvent
{
 public:
  inline const ManagementRequestData& Request() const noexcept
  {
    return request_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept override
  {
    return request_id_;
  }

  inline std::uint8_t Type() const noexcept
  {
    return unknown_type_;
  }

  inline UnknownType()
  : request_id_         {-1, 0U},
    unknown_type_       {0U},
    request_ {static_cast<fcgi_si::FcgiType>(0U), {}, {}}
  {}

  inline UnknownType(fcgi_si::RequestIdentifier request_id,
    std::uint8_t type, const ManagementRequestData& request)
  : request_id_         {request_id},
    unknown_type_       {type},
    request_ {request}
  {}

  inline UnknownType(fcgi_si::RequestIdentifier request_id,
    std::uint8_t type, ManagementRequestData&& request)
  : request_id_         {request_id},
    unknown_type_       {type},
    request_ {std::move(request)}
  {}

  UnknownType(const UnknownType&) = default;
  UnknownType(UnknownType&&) = default;
  
  UnknownType& operator=(const UnknownType&) = default;
  UnknownType& operator=(UnknownType&&) = default;

  ~UnknownType() override = default;

 private:
  fcgi_si::RequestIdentifier request_id_;
  std::uint8_t               unknown_type_;
  ManagementRequestData      request_;
};

class OtherManagementResponse : public ServerEvent
{
 public:
  inline const std::vector<std::uint8_t>& Content() const noexcept
  {
    return content_;
  }

  inline const ManagementRequestData& Request() const noexcept
  {
    return request_;
  }

  inline fcgi_si::RequestIdentifier RequestId() const noexcept
  {
    return request_id_;
  }

  inline OtherManagementResponse()
  : request_id_ {-1, 0U},
    content_    {},
    request_ {}
  {}

  inline OtherManagementResponse(fcgi_si::RequestIdentifier request_id, 
    const std::vector<std::uint8_t>& content, 
    const ManagementRequestData& request)
  : request_id_ {request_id},
    content_    {content},
    request_    {request}
  {}

  inline OtherManagementResponse(fcgi_si::RequestIdentifier request_id, 
    std::vector<std::uint8_t>&& content,
    ManagementRequestData&& request)
  : request_id_ {request_id},
    content_    {std::move(content)},
    request_    {std::move(request)}
  {}

  OtherManagementResponse(const OtherManagementResponse&) = default;
  OtherManagementResponse(OtherManagementResponse&&) = default;
  
  OtherManagementResponse& operator=(const OtherManagementResponse&) = default;
  OtherManagementResponse& operator=(OtherManagementResponse&&) = default;

  ~OtherManagementResponse() override = default;

 private:
  fcgi_si::RequestIdentifier request_id_;
  std::vector<std::uint8_t>  content_;
  ManagementRequestData      request_;
};


                    ////// The interface class. //////

class TestFcgiClientInterface
{
 public:
  // Closes the socket descriptor connection when it refers to a socket
  // opened by the TestFcgiClientInterface instance.
  //
  // Parameters:
  // connection: A connection socket file descriptor.
  //
  // Preconditions: none
  //
  // Exceptions:
  // 1) May through exceptions derived from std::exception.
  // 2) In the event of a throw, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) Internal state was updated to reflect connection closure, the
  //       file descriptor was closed, and the system reported an error
  //       during file descriptor closure. In this case, the exception is of
  //       type std::system_error and contains the error code reported by the
  //       system.
  //
  // Effects:
  // 1) If connection is not a connected socket descriptor which was opened by
  //    the TestFcgiClientInterface instance, then false was returned.
  // 2) Otherwise, true was returned.
  //    a) Requests on connection for which responses had been received
  //       in-full and which were not released remain active.
  //    b) Pending requests were released. The fcgi_si::RequestIdentifier
  //       instances which were generated for them no longer refer to requests.
  bool CloseConnection(int connection);

  // Connects to an IPv4, IPv6, or UNIX domain stream socket as determined
  // by the format of address. For UNIX domain addresses, port is disregarded
  // and the current working directory is used to interpret relative
  // file paths. If connection succeeded, the file descriptor of the local
  // connected socket is returned. If connection failed, -1 is returned and
  // errno is set accordingly.
  //
  // Parameters:
  // address: A pointer to a null-terminated string. This string is interpreted
  //          as either an IPv4 address or an IPv6 address. If neither format
  //          applies, a UNIX domain address is assumed. A character string
  //          length limit including the terminating null byte of 92 bytes is
  //          enforced.
  // port:    The port to be used with the IPv4 or IPv6 address of address.
  //
  // Preconditions: none
  //
  // Exceptions:
  // 1) May through exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) If connection failed, -1 was returned and errno was set accordingly.
  // 2) If connection succeeded:
  //    a) The file descriptor of the local connected socket was returned.
  //    b) If the descriptor had previously been used and had requests which
  //       were not released by a call to ReleaseId, those requests continue
  //       to be active.
  int Connect(const char* address, std::uint16_t port);

  std::vector<std::unique_ptr<ServerEvent>> ReceiveResponses();

  bool ReleaseId(fcgi_si::RequestIdentifier id);
  bool ReleaseId(int connection);

  bool SendAbortRequest(fcgi_si::RequestIdentifier);

  bool SendBinaryManagementRequest(int connection, fcgi_si::FcgiType type,
    const std::uint8_t* byte_ptr, std::size_t length);
  bool SendBinaryManagementRequest(int connection, fcgi_si::FcgiType type,
    std::vector<std::uint8_t>&& data);

  bool SendGetValuesRequest(int connection, const ParamsMap& params_map);
  bool SendGetValuesRequest(int connection, ParamsMap&& params_map);

  fcgi_si::RequestIdentifier SendRequest(int connection, 
    const FcgiRequest& request);
  fcgi_si::RequestIdentifier SendRequest(int connection,
    FcgiRequest&& request);

 private:
  struct RecordState
  {
    std::uint8_t              header[fcgi_si::FCGI_HEADER_LEN] = {};
    std::uint8_t              header_bytes_received {0U};
    std::uint16_t             content_bytes_expected {0U};
    std::uint16_t             content_bytes_received {0U};
    std::uint8_t              padding_bytes_expected {0U};
    std::uint8_t              padding_bytes_received {0U};
    std::vector<std::uint8_t> local_buffer {};
  };

  struct ConnectionState
  {
    bool                   connected;
    a_component::IdManager id_manager;
    RecordState            record_state;
    std::queue<ManagementRequestData, std::list<ManagementRequestData>> 
                           management_queue;
  };

  struct RequestData
  {
    FcgiRequest               request;
    std::vector<std::uint8_t> fcgi_stdout {};
    std::vector<std::uint8_t> fcgi_stderr {};
  };

  std::map<int, ConnectionState>                    connection_map_;
  std::map<fcgi_si::RequestIdentifier, RequestData> request_map_;
};

} // namespace fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
