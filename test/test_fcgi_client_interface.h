#ifndef FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
#define FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_

#include <sys/select.h>
#include <netinet/in.h>
#include <sys/uio.h>

#include <cstdlib>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

#include "external/id_manager/include/id_manager_template.h"

#include "include/protocol_constants.h"
#include "include/request_identifier.h"

namespace fcgi_si_test {

using ParamsMap = std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;

// This is a reference type which contains the metadata of a FastCGI request
// and references to the data of the request.
struct FcgiRequest
{
  // Request metadata.
  std::uint16_t       role;
  bool                keep_conn;

  // Pointers to request data.
  const ParamsMap*    params_map_ptr;
  const std::uint8_t* stdin_begin;
  const std::uint8_t* stdin_end;
  const std::uint8_t* data_begin;
  const std::uint8_t* data_end;
};

struct ManagementRequestData
{
  fcgi_si::FcgiType         type {0U};
  ParamsMap                 params_map {};
  std::vector<std::uint8_t> data {};
};

               ////// ServerEvent and derived classes. //////

class ServerEvent
{
 public:
  virtual fcgi_si::RequestIdentifier RequestId() const = 0;
  virtual ~ServerEvent() = default;
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

  inline FcgiResponse() = default;

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
    const FcgiRequest& request,
    fcgi_si::RequestIdentifier request_id) noexcept
  : app_status_      {app_status},
    fcgi_stderr_     {std::move(stderr)},
    fcgi_stdout_     {std::move(stdout)},
    protocol_status_ {protocol_status},
    request_         {request},
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

  inline InvalidRecord() noexcept
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

  inline GetValuesResult() noexcept
  : corrupt_response_ {false},
    request_id_ {-1, 0U},
    request_params_map_ {},
    response_params_map_ {}
  {}

  inline GetValuesResult(bool corruption, fcgi_si::RequestIdentifier request_id, 
    const ParamsMap& request, const ParamsMap& response)
  : corrupt_response_ {corruption},
    request_id_ {request_id},
    request_params_map_ {request},
    response_params_map_ {response}
  {}

  inline GetValuesResult(bool corruption, fcgi_si::RequestIdentifier request_id, 
    ParamsMap&& request, ParamsMap&& response) noexcept
  : corrupt_response_ {corruption},
    request_id_ {request_id},
    request_params_map_ {std::move(request)},
    response_params_map_ {std::move(response)}
  {}

  GetValuesResult(const GetValuesResult&) = default;
  GetValuesResult(GetValuesResult&&) = default;
  
  GetValuesResult& operator=(const GetValuesResult&) = default;
  GetValuesResult& operator=(GetValuesResult&&) = default;

  ~GetValuesResult() override = default;

 private:
  bool                       corrupt_response_;
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

  inline UnknownType() noexcept
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
    std::uint8_t type, ManagementRequestData&& request) noexcept
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
  //          The port value must be in NETWORK byte order.
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
  int Connect(const char* address, in_port_t network_port);

  inline int ConnectionCount() const noexcept
  {
    return number_connected_;
  }

  std::size_t ManagementRequestCount(int connection) const;

  inline std::size_t ReadyEventCount() const noexcept
  {
    return micro_event_queue_.size();
  }

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

  std::unique_ptr<ServerEvent> RetrieveServerEvent();

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
  // Names are taken from params_map. Regardless of the values of the
  // name-value pairs of params_map, the name-value pair values are encoded as
  // if they were empty. A copy of params_map with empty values is made and
  // managed by *this.
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
  // Names are taken from params_map. Regardless of the values of the
  // name-value pairs of params_map, the name-value pair values are encoded as
  // if they were empty. The values of params_map are cleared. params_map is
  // then moved to storage managed by *this.
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

  TestFcgiClientInterface();

  TestFcgiClientInterface(const TestFcgiClientInterface&)            = delete;
  TestFcgiClientInterface(TestFcgiClientInterface&&)                 = delete;

  TestFcgiClientInterface& operator=(const TestFcgiClientInterface&) = delete;
  TestFcgiClientInterface& operator=(TestFcgiClientInterface&&)      = delete;

  ~TestFcgiClientInterface();

 private:
  struct RecordState
  {
    inline RecordState() noexcept
    : invalidated            {false},
      fcgi_id                {0U},
      type                   {static_cast<fcgi_si::FcgiType>(0U)},
      header                 {},
      header_bytes_received  {0U},
      content_bytes_expected {0U},
      content_bytes_received {0U},
      padding_bytes_expected {0U},
      padding_bytes_received {0U},
      local_buffer           {}
    {}

    RecordState(const RecordState&)            = default;
    RecordState(RecordState&&)                 = default;

    RecordState& operator=(const RecordState&) = default;
    RecordState& operator=(RecordState&&)      = default;

    ~RecordState()                             = default;

    bool                      invalidated;
    std::uint16_t             fcgi_id;
    fcgi_si::FcgiType         type;
    std::uint8_t              header[fcgi_si::FCGI_HEADER_LEN];
    std::uint8_t              header_bytes_received;
    std::uint16_t             content_bytes_expected;
    std::uint16_t             content_bytes_received;
    std::uint8_t              padding_bytes_expected;
    std::uint8_t              padding_bytes_received;
    std::vector<std::uint8_t> local_buffer;
  };

  static_assert(std::is_nothrow_default_constructible<TestFcgiClientInterface::RecordState>::value);
  static_assert(std::is_nothrow_move_constructible<TestFcgiClientInterface::RecordState>::value);
  static_assert(std::is_nothrow_move_assignable<TestFcgiClientInterface::RecordState>::value);

  struct ConnectionState
  {
    // Move construction and move assignment may throw.
    bool                                  connected;
    a_component::IdManager<std::uint16_t> id_manager;
    RecordState                           record_state;
    std::list<ManagementRequestData>      management_queue;
  };

  // a_component::IdManager<std::uint16_t>
  static_assert(std::is_nothrow_default_constructible<a_component::IdManager<std::uint16_t>>::value);
  static_assert(std::is_nothrow_move_constructible<a_component::IdManager<std::uint16_t>>::value);
  static_assert(std::is_nothrow_move_assignable<a_component::IdManager<std::uint16_t>>::value);
  // ConnectionState
  static_assert(std::is_nothrow_default_constructible<TestFcgiClientInterface::ConnectionState>::value);
  static_assert(std::is_nothrow_move_constructible<TestFcgiClientInterface::ConnectionState>::value);
  static_assert(std::is_nothrow_move_assignable<TestFcgiClientInterface::ConnectionState>::value);

  struct RequestData
  {
    inline RequestData() noexcept
    : request          {},
      fcgi_stdout      {},
      stdout_completed {false},
      fcgi_stderr      {},
      stderr_completed {false}
    {}

    inline RequestData(
      FcgiRequest new_request,
      std::vector<std::uint8_t>&& stdout_content,
      bool stdout_status,
      std::vector<std::uint8_t>&& stderr_content,
      bool stderr_status
    ) noexcept
    : request {new_request},
      fcgi_stdout {std::move(stdout_content)},
      stdout_completed {stdout_status},
      fcgi_stderr {std::move(stderr_content)},
      stderr_completed {stderr_status}
    {}

    RequestData(const RequestData&)            = default;
    RequestData(RequestData&&)                 = default;

    RequestData& operator=(const RequestData&) = default;
    RequestData& operator=(RequestData&&)      = default;

    ~RequestData()                             = default;

    FcgiRequest               request;
    std::vector<std::uint8_t> fcgi_stdout;
    bool                      stdout_completed;
    std::vector<std::uint8_t> fcgi_stderr;
    bool                      stderr_completed;
  };

  static_assert(std::is_nothrow_default_constructible<TestFcgiClientInterface::RequestData>::value);
  static_assert(std::is_nothrow_move_constructible<TestFcgiClientInterface::RequestData>::value);
  static_assert(std::is_nothrow_move_assignable<TestFcgiClientInterface::RequestData>::value);

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
  std::map<int, ConnectionState>::iterator ConnectedCheck(int connection);

  // A helper function which is intended to only be used within
  // RetrieveServerEvent.
  //
  // Preconditions: 
  // 1) remaining_ready_ > 0
  //
  // Exceptions:
  // 1)
  // 2)
  //
  // Effects:
  //
  void ExamineSelectReturn();

  // Performs recovery after a write to a connection failed.
  //
  // Parameters:
  // connection_iter:      An iterator to the connection_map_ entry of the
  //                       connection which was written to.
  // error_code:           The value of errno which was set by the I/O system
  //                       call which failed.
  // nothing_written:      True if nothing was written over the entire
  //                       sequence of writes which make up the write
  //                       transaction on the connection.
  // pop_management_queue: True if the most recently added management queue
  //                       entry should be removed and discarded.
  // system_error_message: The message which will be included in a
  //                       std::system_error instance if an exception is
  //                       thrown.
  //
  // Preconditions:
  // 1) connection_iter is not null and points to a valid entry of connection
  //    map.
  // 2) pop_management_queue is true if and only if the failed write
  //    transaction added an entry to the end of queue which should be removed.
  //
  // Exceptions: See below.
  //
  // Effects:
  // 1) May return, throw a std::system_error instance, or terminate the
  //    program.
  //    a) If the call returned or threw, then connection_iter->first was
  //       closed by a call to CloseConnection if a partial write occurred or
  //       error_code == EPIPE. If pop_management_queue == true, then the
  //       item which was most recently added to the management queue of
  //       connection_iter->first was removed.
  //    b) A throw occurs if error_code != EPIPE.
  //    c) Termination occurs if an invariant of the interface could not be
  //       maintained.
  //    d) The function returns otherwise.
  void FailedWrite(
    std::map<int, ConnectionState>::iterator connection_iter,
    int                                      error_code,
    bool                                     nothing_written,
    bool                                     pop_management_queue,
    const char*                              system_error_message
  );

  // ProcessCompleteRecord is intended to only be used within the
  // implementation of ExamineSelectReturn (which is in turn only intended to
  // be used within the implementation of RetrieveServerEvent).
  //
  // Parameters:
  // connection_iter: The iterator to the connection_map_ entry whose
  //                  RecordState instance is complete.
  // pending_iter:    An iterator to pending_request_map_. See the
  //                  preconditions section for the preconditions on
  //                  pending_iter.
  //
  // Preconditions:
  // 1) connection_iter does not refer to connection_map_.end().
  // 2)    If the type of the record is FCGI_END_REQUEST, FCGI_STDOUT, or
  //    FCGI_STDERR, and the record was not invalidated, then pending_iter
  //    refers to the appropriate entry in pending_request_map_.
  //       Note that pending_iter_ may be equal to pending_request_map_.end()
  //    if the above condition does not hold.
  //
  // Exceptions
  // 1) A call may throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) If the record was invalidated or the type of the record was one of
  //    FCGI_END_REQUEST, FCGI_GET_VALUES_RESULT, or FCGI_UNKNOWN_TYPE, then an
  //    instance of the appropriate type derived from ServerEvent was
  //    constructed and added to the end of micro_event_queue_.
  // 2) If the type of the record was one of FCGI_GET_VALUES_RESULT or
  //    FCGI_UNKNOWN_TYPE (i.e. the record was a management record) and the
  //    record was valid, then the management queue referred to by
  //    connection_iter was popped.
  // 3) 
  // 4) If the type of the record was FCGI_END_REQUEST and the record was valid,
  //    then the entry for the request was removed from pending_request_map_
  //    and the RequestIdentifier of the request was added to
  //    completed_request_set_.
  // 5)    If the type of the record was either FCGI_STDOUT or FCGI_STDERR, the
  //    record was valid, and the content length of the record was zero, then
  //    the appropriate stream was completed in the RequestData instance
  //    referred to by pending_iter_.
  //       Note that stream data is appended to the appropriate stream buffer
  //    of the pending_request_ entry for the request when it is received.
  // 6) The RecordState instance of the ConnectionState instance referred to
  //    by connection_iter_ was reinitialized.
  // 7) A valid iterator to pending_request_map_ was returned.
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator
  ProcessCompleteRecord(
    std::map<int, ConnectionState>::iterator connection_iter,
    std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_iter);

  bool SendBinaryManagementRequestHelper(
    std::map<int, ConnectionState>::iterator connection_iter,
    fcgi_si::FcgiType type, ManagementRequestData&& queue_item);

  bool SendGetValuesRequestHelper(
    std::map<int, ConnectionState>::iterator connection_iter,
    ManagementRequestData&& queue_item);
  
  bool SendManagementRequestHelper(
    std::map<int, ConnectionState>::iterator connection_iter,
    struct iovec iovec_array[], int iovec_count, std::size_t number_to_write);

  // UpdateOnHeaderCompletion is intended to only be used within the
  // implementation of ExamineSelectReturn (which is in turn only intended to
  // be used within the implementation of RetrieveServerEvent).
  //
  // Parameters:
  // 1) connection_iter: The iterator of connection_map_ which refers to the
  //                     entry whose header was just completed.
  // 2) pending_iter:    An iterator of pending_request_map_. This iterator is
  //                     used to potentially save a search in
  //                     pending_request_map_.
  //
  // Preconditions:
  // 1) connection_iter does not refer to the end of connection_map_, is
  //    valid, and refers to the correct entry.
  // 2) pending_iter is valid. It may be equal to pending_request_map_.end().
  //
  // Exceptions
  // 1) A call may throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  //
  // Effects:
  // 1) The record was inspected for validity relative to the requirements of
  //    the FastCGI protocol and the state of the ConnectionState instance
  //    referred to by connection_iter. A record was invalidated if:
  //    a) The FastCGI protocol version of the record was not 1.
  //    b) FCGI_END_REQUEST:
  //       1) No corresponding request existed in pending_request_map_.
  //       2) A corresponding request existed, but at least one of the
  //          FCGI_STDERR and FCGI_STDOUT streams was not complete.
  //       3) The content length of the record was not 8 bytes.
  //    c) FCGI_STDERR:
  //       1) No corresponding request existed in pending_request_map_.
  //       2) A corresponding request existed, but the FCGI_STDERR stream was
  //          complete.
  //    d) FCGI_STDOUT: as FCGI_STDERR, mutatis mutandis.
  //    e) FCGI_GET_VALUES_RESULT:
  //       1) The FastCGI identifier of the record was not zero.
  //       2) No management requests were active for the connection.
  //       3) The first request in the management request queue was not of type
  //          FCGI_GET_VALUES.
  //    f) FCGI_UNKNOWN_TYPE:
  //       1) The FastCGI identifier of the record was not zero.
  //       2) No management requests were active for the connection.
  //       3) The first request in the management request queue was of type
  //          FCGI_GET_VALUES (as every FastCGI server must recognize
  //          FCGI_GET_VALUES requests).
  //       4) The content length of the record was not 8 bytes.
  //    g) The record was not of one the above types.
  // 3) The information contained in the header was used to update the
  //    following fields of the RecordState instance referred to by
  //    connection_iter: type, fcgi_id, content_bytes_expected, and
  //    padding_bytes_expected.
  // 4) A valid iterator of pending_request_map_ was returned.
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator
  UpdateOnHeaderCompletion(
    std::map<int, ConnectionState>::iterator connection_iter,
    std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_iter);

  std::set<fcgi_si::RequestIdentifier>              completed_request_set_;
  std::map<int, ConnectionState>                    connection_map_;
  std::map<fcgi_si::RequestIdentifier, RequestData> pending_request_map_;
  std::list<std::unique_ptr<ServerEvent>>           micro_event_queue_;
  int                                               number_connected_;
  // I/O multiplexing tracking state
  int                                               remaining_ready_;
  std::map<int, ConnectionState>::iterator          next_connection_;
  fd_set                                            select_set_;

  static constexpr const char* write_or_select_ {"write or select"};
};

} // namespace fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
