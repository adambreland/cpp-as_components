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
#include <utility>
#include <vector>

#include "external/id_manager/include/id_manager_template.h"

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
  // Attempts to close the socket descriptor connection when it refers to a
  // socket opened by the TestFcgiClientInterface instance.
  //
  // Parameters:
  // connection: The socket file descriptor of a connection.
  //
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) In the event of a throw, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) Internal state was updated to reflect connection closure, the
  //       file descriptor was closed, and the system reported an error
  //       during file descriptor closure. In this case, the exception is of
  //       type std::system_error and contains the error code reported by the
  //       system.
  //
  // Effects:
  // 1) If connection was not a connected socket descriptor which was opened by
  //    the TestFcgiClientInterface instance, then false was returned.
  // 2) Otherwise, true was returned.
  //    a) Requests on connection for which responses had been received
  //       in-full and which were not released by the user remain active.
  //    b) Pending requests were released: the fcgi_si::RequestIdentifier
  //       instances which were generated for them no longer refer to requests.
  bool CloseConnection(int connection);

  // Attempts to connect to an IPv4, IPv6, or UNIX domain stream socket as
  // determined by the format of address. For UNIX domain addresses, port is
  // disregarded and the current working directory is used to interpret
  // relative file paths. If connection succeeded, the file descriptor of the
  // local connected socket is returned. If connection failed, -1 is returned
  // and errno is set appropriately.
  //
  // Parameters:
  // address: A pointer to a null-terminated string. This string is interpreted
  //          as either an IPv4 address or an IPv6 address. If neither format
  //          applies, a UNIX domain address is assumed. A character string
  //          length limit including the terminating null byte of 92 bytes is
  //          enforced.
  // port:    The port to be used with an IPv4 or IPv6 address.
  //
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) If connection failed, -1 was returned and errno was set accordingly.
  //    Note that failure occurs if the arguments were considered invalid.
  // 2) If connection succeeded:
  //    a) The file descriptor of the local connected socket was returned.
  //    c) The file description associated with the returned file descriptor
  //       is non-blocking.
  //    b) If the descriptor had previously been used and had requests which
  //       were not released by a call to ReleaseId, those requests continue
  //       to be active.
  // 3) EINTR was ignored during the invocation.
  int Connect(const char* address, std::uint16_t port);

  std::unique_ptr<ServerEvent> RetrieveServerEvent();

  // Attempts to release the FastCGI request identifier of id when id refers
  // to a request which is completed and unreleased.
  //
  // Parameters:
  // id: When id refers to a completed but unreleased request, id will have
  //     been returned by a call to SendRequest and will be the
  //     RequestIdentifier value that is returned by a call to RequestId on
  //     a ServerEvent instance which was returned by a call to
  //     RetrieveServerEvent.
  //
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) If false was returned, then id did not refer to a completed but
  //    unreleased request.
  // 2) If true was returned, then the FastCGI request identifier of id was
  //    released.
  bool ReleaseId(fcgi_si::RequestIdentifier id);

  // Attempts to release all FastCGI request identifiers on connection which
  // are associated with completed but unreleased requests.
  //
  // Parameters:
  // int: The descriptor of a socket connection.
  //
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) If false was returned, then connection neither referred to a connected
  //    socket descriptor which was opened by the interface nor referred to
  //    a disconnected socket descriptor which was associated with completed
  //    but unreleased requests.
  // 2) If true was returned, then all completed but unreleased requests which
  //    were associated with connection were released.
  bool ReleaseId(int connection);

  // Attempts to send a FastCGI request abort record for id.Fcgi_id() on
  // id.descriptor() when id refers to a pending FastCGI request.
  //
  // Parameters:
  // id: For a pending request, id is the value returned by a successful call
  //     to SendRequest.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) If a throw occurs, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) An error occurred when writing to the connection, and the connection
  //       was corrupted as a result. The connection was closed by a call
  //       to CloseConnection. A ConnectionClosure instance was added to the
  //       micro server event queue.
  //
  // Termination:
  // 1) If an error or exception would cause SendAbortRequest to return or
  //    throw when an invariant of TestFcgiClientInterface is potentially
  //    violated, the program is terminated.
  //
  // Effects:
  // 1) If false was returned, then either id did not refer to a pending
  //    request, or it was discovered that the server closed id.descriptor().
  //    When connection closure was discovered, a ConnectionClosure instance
  //    was added to the micro server event queue.
  // 2) If true was returned, then a FastCGI request abort was sent for id.
  bool SendAbortRequest(fcgi_si::RequestIdentifier id);

  // Attempts to send a management request with content given by
  // [byte_ptr, byte_ptr + length) and management request type given by type
  // to connection.
  //
  // Parameters:
  // connection: The descriptor of a socket connection.
  // type:       The type of the management request. A type defined by the
  //             FastCGI protocol is not required.
  // byte_ptr:   A pointer to the first byte in the byte sequence which will
  //             form the content of the management request.
  // length:     The length of the content byte sequence.
  // 
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) If a throw occurs, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) An error occurred when writing to the connection, and the connection
  //       was corrupted as a result. The connection was closed by a call
  //       to CloseConnection. A ConnectionClosure instance was added to the
  //       micro server event queue.
  //
  // Termination:
  // 1) If an error or exception would cause SendAbortRequest to return or
  //    throw when an invariant of TestFcgiClientInterface is potentially
  //    violated, the program is terminated.
  //
  // Effects:
  // 1) If false was returned, then one of the following occurred:
  //    a) connection was not a connected socket descriptor which was opened by
  //       the interface.
  //    b) Length was larger than fcgi_si::kMaxRecordContentByteLength.
  //    c) It was discovered that the server closed the connection. When
  //       connection closure was discovered, a ConnectionClosure instance was
  //       added to the micro server event queue.
  // 2) If true was returned:
  //    a) The management request was sent.
  //    b) A ManagementRequestData instance with a copy of the byte sequence 
  //       given by [byte_ptr, byte_ptr + length) was enqueued to the
  //       management request queue of connection.
  bool SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, const std::uint8_t* byte_ptr, std::size_t length);
  
  // Attempts to send a management request with content given by data and
  // management request type given by type to connection.
  //
  // Parameters:
  // connection: The descriptor of a socket connection.
  // type:       The type of the management request. A type defined by the
  //             FastCGI protocol is not required.
  // data:       The data to be moved into an internal queue and sent to
  //             the server over connection.
  // 
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) If a throw occurs, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) An error occurred when writing to the connection, and the connection
  //       was corrupted as a result. The connection was closed by a call
  //       to CloseConnection. A ConnectionClosure instance was added to the
  //       micro server event queue.
  //
  // Termination:
  // 1) If an error or exception would cause SendAbortRequest to return or
  //    throw when an invariant of TestFcgiClientInterface is potentially
  //    violated, the program is terminated.
  //
  // Effects:
  // 1) If false was returned, then one of the following occurred:
  //    a) connection was not a connected socket descriptor which was opened by
  //       the interface.
  //    b) data.size() was larger than fcgi_si::kMaxRecordContentByteLength.
  //    c) It was discovered that the server closed the connection. When
  //       connection closure was discovered, a ConnectionClosure instance was
  //       added to the micro server event queue.
  // 2) If true was returned:
  //    a) The management request was sent.
  //    b) A ManagementRequestData instance with the byte sequence of data was
  //       enqueued to the management request queue of connection.
  bool SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, std::vector<std::uint8_t>&& data);

  // Attempts to send an FCGI_GET_VALUES management request on connection.
  // Names are taken from params_map. Values of the encoded name-value pairs
  // are empty.
  //
  // Parameters:
  // connection: The descriptor of a socket connection.
  // params_map: A map of names to be encoded in the FastCGI name-value pair
  //             format and sent to connection. Values of the map are ignored.
  // 
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) If a throw occurs, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) An error occurred when writing to the connection, and the connection
  //       was corrupted as a result. The connection was closed by a call
  //       to CloseConnection. A ConnectionClosure instance was added to the
  //       micro server event queue.
  //
  // Termination:
  // 1) If an error or exception would cause SendAbortRequest to return or
  //    throw when an invariant of TestFcgiClientInterface is potentially
  //    violated, the program is terminated.
  //
  // Effects:
  // 1) If false was returned, then one of the following occured:
  //    a) connection was not a connected socket descriptor which was opened by
  //       the interface.
  //    b) The names of params_map and empty values required more than one
  //       FastCGI record when they were encoded with a call to
  //       fcgi_si::EncodeNameValuePairs.
  //    c) It was discovered that the server closed the connection. When
  //       connection closure was discovered, a ConnectionClosure instance was
  //       added to the micro server event queue.
  // 2) If true was returned:
  //    a) The management request was sent.
  //    b) A ManagementRequestData instance with a ParamsMap instance with the
  //       names of params_map and empty values was enqueued to the management
  //       request queue of connection.
  bool SendGetValuesRequest(int connection, const ParamsMap& params_map);

  // Attempts to send an FCGI_GET_VALUES management request on connection.
  // Names are taken from params_map. Values of the encoded name-value pairs
  // are empty.
  //
  // Parameters:
  // connection: The descriptor of a socket connection.
  // params_map: A map of names to be encoded in the FastCGI name-value pair
  //             format and sent to connection. Values of the map are ignored.
  // 
  // Preconditions: none
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) If a throw occurs, one of the following holds:
  //    a) The strong exception guarantee.
  //    b) An error occurred when writing to the connection, and the connection
  //       was corrupted as a result. The connection was closed by a call
  //       to CloseConnection. A ConnectionClosure instance was added to the
  //       micro server event queue.
  //
  // Termination:
  // 1) If an error or exception would cause SendAbortRequest to return or
  //    throw when an invariant of TestFcgiClientInterface is potentially
  //    violated, the program is terminated.
  //
  // Effects:
  // 1) If false was returned, then one of the following occured:
  //    a) connection was not a connected socket descriptor which was opened by
  //       the interface.
  //    b) The names of params_map and empty values required more than one
  //       FastCGI record when they were encoded with a call to
  //       fcgi_si::EncodeNameValuePairs.
  //    c) It was discovered that the server closed the connection. When
  //       connection closure was discovered, a ConnectionClosure instance was
  //       added to the micro server event queue.
  // 2) If true was returned:
  //    a) The management request was sent.
  //    b) The values of params_map were cleared. params_map was then moved to
  //       a ManagementRequestData instance. This instance was then enqueued
  //       to the management request queue of connection.
  bool SendGetValuesRequest(int connection, ParamsMap&& params_map);

  //
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
    bool                                  connected;
    a_component::IdManager<std::uint16_t> id_manager;
    RecordState                           record_state;
    std::list<ManagementRequestData>      management_queue;
  };

  struct RequestData
  {
    FcgiRequest               request;
    std::vector<std::uint8_t> fcgi_stdout {};
    std::vector<std::uint8_t> fcgi_stderr {};
  };

  // Preconditions: none.
  //
  // Exceptions
  // 1) A call may throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) Returns a null pointer if connection is not associated with an entry
  //    in connection_map_ or if connection is not connected.
  // 2) Returns a pointer to the ConnectionState instance of the entry of
  //    connection_map_ which is associated with connection otherwise.
  std::pair<const int, ConnectionState>* ConnectedCheck(int connection);

  bool SendBinaryManagementRequestHelper(
    std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr,
    fcgi_si::FcgiType type, ManagementRequestData&& queue_item);

  bool SendGetValuesRequestHelper(
    std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr,
    ManagementRequestData&& queue_item);
  
  bool SendManagementRequestHelper(
    std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr,
    struct iovec iovec_array[], int iovec_count, std::size_t number_to_write);

  std::set<fcgi_si::RequestIdentifier>              completed_request_set_;
  std::map<int, ConnectionState>                    connection_map_;
  std::map<fcgi_si::RequestIdentifier, RequestData> pending_request_map_;
  std::list<std::unique_ptr<ServerEvent>>           micro_event_queue_;
};

} // namespace fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
