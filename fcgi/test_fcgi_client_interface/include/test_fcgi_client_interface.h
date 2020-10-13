#ifndef A_COMPONENT_TEST_FCGI_CLIENT_INTERFACE_INCLUDE_TEST_FCGI_CLIENT_INTERFACE_H_
#define A_COMPONENT_TEST_FCGI_CLIENT_INTERFACE_INCLUDE_TEST_FCGI_CLIENT_INTERFACE_H_

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

#include "external/fcgi_si/include/protocol_constants.h"
#include "external/fcgi_si/include/request_identifier.h"
#include "external/id_manager/include/id_manager_template.h"

namespace a_component {
namespace fcgi {

using ParamsMap = std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;

//    This is a reference type which contains the metadata of a FastCGI request
// and references to the data of the request.
// 1) TestFcgiClientInterface::SendRequest accepts an FcgiRequest instance
//    in its parameter list.
// 2) FcgiResponse, a subtype of ServerEvent, uses FcgiRequest in its interface.
//
//    Note that a user of FcgiRequest must ensure that the contained pointers
// remain valid while TestFcgiClientInterface may use their values.
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

// A ManagementRequestData instance is used to store the information of a
// FastCGI management request.
// 1) An FCGI_GET_VAULES request is represented as a ManagementRequestData
//    instance with the appropriate type, a ParamsMap instance which stores
//    the name-value pairs of the request, and an empty data field.
// 2) All other management requests are treated as non-defined binary requests.
//    A ManagementRequestData instance is passed to SendBinaryManagementRequest
//    in this case. The type field is populated as desired, the params_map
//    field is empty, and data holds the FastCGI record content of the request.
struct ManagementRequestData
{
  fcgi_si::FcgiType         type {static_cast<fcgi_si::FcgiType>(0U)};
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
    // 1) For default-constructed instances, RequestId returns the
    //    RequestIdentifier given by (-1, 0).
    // 2) For other, non-moved-from instances, RequestId returns the
    //    RequestIdentifier given by (connection, 0) where connection is a
    //    local socket descriptor of a socket which was discovered to have been
    //    closed by its peer.
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

// A class which:
// 1) Stores the response to a FastCGI application request.
// 2) Allows access to the information of the request as represented by a
//    FcgiRequest instance and the RequestIdentifier of the request.
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

class GetValuesResult : public ServerEvent
{
 public:
  // 1) Returns true if a FastCGI name-value pair encoding error was detected
  //    in the response for the FCGI_GET_VALUES request described
  //    by RequestMap.
  //    a) If true is returned, then ResponseMap returns an empty map. The
  //       received erroneous data was discarded.
  // 2) Returns false if no FastCGI name-value pair encoding error was detected.
  //    IsCorrupt returns false for default-constructed instances.
  //    a) For non-default constructed instances, ResponseMap returns the map
  //       of name-value pairs which was encoded in the FCGI_GET_VALUES_RESULT
  //       record sent in response to the request described by RequestMap.
  inline bool IsCorrupt() const noexcept
  {
    return corrupt_response_;
  }

  inline const ParamsMap& RequestMap() const noexcept
  {
    return request_params_map_;
  }

  // 1) For a default constructed instance, RequestId returns the
  //    RequestIdentifier given by (-1, 0).
  // 2) For other, non-moved-from instances, RequestId returns the
  //    RequestIdentifier given by (connection, 0) where connection is the
  //    local socket descriptor of the socket over which the the
  //    FCGI_GET_VALUES request and its response were sent.
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

//    This class represents a FastCGI record which was deemed invalid. All of
// the information of the record except for the value of the reserved header
// byte and the values of padding bytes, if any, may be inspected.
//    A record is deemed invalid if:
// 1) The value of the version byte of the record header is not equal to one.
// 2) A type-based record property was not met. These properties may depend
//    on the request history of the client interface.
//    FCGI_END_REQUEST:
//    a) The content length is not equal to its specified length, eight bytes.
//    b) The record concerns a request which does not exist.
//    c) The record would imply termination of a response before the streams of
//       the response are complete.
//    FCGI_STDOUT:
//    a) The record concerns a request which does not exist.
//    b) The record concerns a request whose response has a completed
//       FCGI_STDOUT stream.
//    FCGI_STDERR:
//    a) As for FCGI_STDOUT, mutatis mutandis.
//    FCGI_GET_VALUES_RESULT:
//    a) The FastCGI identifier of the record is not zero.
//    b) No management requests exist for the connection over which the record
//       was sent.
//    c) The management request at the beginning of the management request
//       queue does is not an FCGI_GET_VALUES request.
//    FCGI_UNKNOWN_TYPE:
//    a) The FastCGI identifier of the record is not zero.
//    b) No management requests exist for the connection over which the record
//       was sent.
//    c) The content length is not equal to its specified length, eight bytes.
//    d) The management request at the beginning of the management request
//       queue does is an FCGI_GET_VALUES request. All FastCGI application
//       servers must accept FCGI_GET_VALUES requests, and FCGI_GET_VALUES
//       is the only management request type specified in the first version of
//       the FastCGI protocol.
//    Any other record type:
//    a) All other types are rejected as they should not be sent to a FastCGI
//       client server.
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

class UnknownType : public ServerEvent
{
 public:
  // 1) For a default constructed instance, Request accesses a default-
  //    constructed ManagementRequestData instance.
  // 2) For other, non-moved-from instances, Request accesses a
  //    ManagementRequestData instance with the data of the
  //    appropriate ManagementRequestData instance which was used in a call to
  //    TestFcgiClientInterface::SendBinaryManagementRequest.
  inline const ManagementRequestData& Request() const noexcept
  {
    return request_;
  }

  // 1) For a default constructed instance, RequestId returns the
  //    RequestIdentifier given by (-1, 0).
  // 2) For other, non-moved-from instances, RequestId returns the
  //    RequestIdentifier given by (connection, 0) where connection is the
  //    local socket descriptor of the socket over which the management
  //    request and its corresponding FCGI_UNKNOWN_TYPE response were sent.
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

                    ////// TestFcgiClientInterface //////

//    TestFcgiClientInterface provides an implementation of the FastCGI
// protocol for programs which make requests of FastCGI application servers.
// Such programs may be called clients or client servers.
//    The interface allows connections to be made to distinct FastCGI
// application servers. Different socket domains may be used; all connections
// are stream-based as FastCGI requires stream-based sockets.
//    Each interface instance has a management request queue for each
// connection. As all management requests on a given connection share the same
// request identifier (connection, 0), management request order is preserved
// by the queue. Responses to management requests are associated with
// management requests according to request order.
//    A user can manually close a connection through a call to CloseConnection.
// When this is done, all pending application requests and pending management
// requests are lost. The interface informs a user that an application server
// closed a connection by returning an appropriate ConnectionClosure instance
// from a call to RetrieveServerEvent. As for manual closure, all pending
// application and management requests are lost when this occurs.
//    The interface uses the notion of allocated and released request
// identifiers. All request identifiers are initially unallocated. This is
// equivalent to being released. When a request is made, a FastCGI identifier
// is chosen by the interface. Let this identifier be ID. The pair
// (connection, ID) is used to construct an fcgi_si::RequestIdentifier instance.
// This value within RequestIdentifier identifies the request. Once the request
// is made, this value transitions from being released to being allocated. Only
// released values are used for new requests. Once a response has been received
// for a request identified by value v = (connection, ID), value is not
// released until a call to ReleaseId(v) or ReleaseId(connection) is made. This
// is true regardless of intervening closure of connection by an application
// server or through calls of CloseConnection(connection).
//    Note that the handling of allocated and released request identifiers for
// requests which received a response (completed requests) prevents the reuse
// of a request identifier before it is explicitly released by the user of
// the interface. This allows processing of the responses to requests to be
// deferred while preventing ambiguous request identifier values.
//    TestFcgiServerEvent uses instances of types derived from the abstract
// type ServerEvent to represent information which was received from
// application servers. ServerEvent was defined for this purpose. An internal
// queue is used to store events.
//    The event queue is also used by SendAbortRequest,
// SendBinaryManagementRequest, and SendGetValuesRequest to report the detection
// during their invocation of the closure of a connection by its peer.
//
//    Several features of the interface make it suited for programs which
// test implementations of the FastCGI protocol for application servers or
// which test application servers.
// 1) The interface is not concurrent (though I/O multiplexing on connections
//    is performed).
//    a) Interface methods may not be safely called from multiple threads.
//    b) A single server event may be retrieved at a time. Calls to
//       RetrieveServerEvent block until an event is ready.
//    c) Methods which do not read incoming data may add events to the common
//       event queue.
// 2) InvalidRecord is used to expose invalid records received from application
//    servers. This behavior is performed instead of dropping the record.
// 3) Encoding errors in the content of FCGI_GET_VALUES_RESULT records are
//    exposed through the IsCorrupt method of GetValuesResult.
// 4) A reference type is used to represent request values when requests are
//    submitted to the interface by a user. Copies of request data are not made
//    by the interface. This allows a reduction of the memory footprint of
//    the internal state used to represent and track requests while still
//    allowing request data identity to be returned with the response to a
//    request. It also allows the data of a single request to be reused with
//    low overhead in the case where many requests with the same data are
//    submitted to an application server.
// 5) Management requests with types other than FCGI_GET_VALUES can be sent.
//    The content of such requests is not constrained.
// 6) Errors and exceptions are handled simply. This is appropriate and
//    acceptable for testing code.
//    a) In some cases, program termination by the interface is allowed.
//    b) Though methods have clearly defined exception specifications,
//       exceptions are propagated with little expectation of or support for
//       recovery.
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

  // Returns the total number of completed and unreleased requests which are
  // managed by the interface. When non-zero, this value may be decreased by an
  // appropriate call of ReleaseId.
  inline std::size_t CompletedRequestCount() const noexcept
  {
    return completed_request_set_.size();
  }

  // Returns the number of completed and unreleased requests which were made on
  // any connection which was identified by a socket descriptor equal to
  // connection. When non-zero, this value may be decreased by a call of
  // ReleaseId(connection) or ReleaseId(v) where the connection of v is
  // connection and v identifies a completed and unreleased request.
  std::size_t CompletedRequestCount(int connection) const;

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

  // Returns the total number of connected socket descriptors which are managed
  // by the interface.
  inline int ConnectionCount() const noexcept
  {
    return number_connected_;
  }

  // Returns true if connection is a connected socket descriptor managed by
  // the interface. Note that false is returned in the case that connection
  // is closed but request identifiers which are associated with completed
  // requests on connection are present.
  bool IsConnected(int connection) const;

  // Returns the number of pending management requests for connection.
  std::size_t ManagementRequestCount(int connection) const;

  inline std::size_t ReadyEventCount() const noexcept
  {
    return micro_event_queue_.size();
  }

  // Returns the total number of pending requests.
  inline std::size_t PendingRequestCount() const noexcept
  {
    return pending_request_map_.size();
  }

  // Returns the number of pending requests for connection.
  std::size_t PendingRequestCount(int connection) const;

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

  // Attempts to send a management request to connection with the specified
  // binary sequence as content and a management request type given by type.
  //
  // Parameters:
  // connection: The descriptor of a socket connection.
  // type:       The type of the management request. A type defined by the
  //             FastCGI protocol is not required.
  // (Copy overload)
  // byte_ptr:   A pointer to the first byte in the byte sequence which will
  //             form the content of the management request.
  // length:     The length of the content byte sequence.
  // (Move overload)
  // data:       The data to be moved internally and sent to the server over
  //             connection.
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
  //    b) Data was stored to allow an appropriate response object to be
  //       initialized upon receipt of a response. The response object will
  //       contain a reference to a byte sequence equal the specified content
  //       byte sequence.
  //    c) For the move overload: No copy of data was made. data is in a
  //       moved from state.
  bool SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, const std::uint8_t* byte_ptr, std::size_t length);
  bool SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, std::vector<std::uint8_t>&& data);

  // Attempts to send an FCGI_GET_VALUES management request on connection.
  // Names are taken from params_map. Regardless of the values of the
  // name-value pairs of params_map, the name-value pair values are encoded as
  // if they were empty.
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
  //    b) The data of the request, with name-value pair values ignored, was
  //       stored to allow appropriate initialization of a GetValuesResult
  //       object upon receipt of a response.
  //    c) For the move overload: No copy of params_map was made. params_map
  //       is in a moved-from state.
  bool SendGetValuesRequest(int connection, const ParamsMap& params_map);
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

} // namespace fcgi
} // namespace a_component

#endif // A_COMPONENT_TEST_FCGI_CLIENT_INTERFACE_INCLUDE_TEST_FCGI_CLIENT_INTERFACE_H_
