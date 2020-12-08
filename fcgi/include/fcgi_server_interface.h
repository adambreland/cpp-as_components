#ifndef A_COMPONENT_FCGI_INCLUDE_FCGI_SERVER_INTERFACE_H_
#define A_COMPONENT_FCGI_INCLUDE_FCGI_SERVER_INTERFACE_H_

#include <cstdint>       // For uint8_t, ... .
#include <cstdlib>       // For EXIT_FAILURE.
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_request_identifier.h"

namespace a_component {
namespace fcgi {

// A forward declaration to break the cyclic dependency between FcgiRequest
// and FcgiServerInterface.
class FcgiRequest;

// Description:
//    FcgiServerInterface is a singleton class which implements the majority of
// the FastCGI protocol for application servers. This class and its associated
// class FcgiRequest support multi-threaded applications. FcgiRequest objects
// are produced by the AcceptRequests method of FcgiServerInterface. The thread
// which houses the instance of FcgiServerInterface is supposed to execute
// calls to AcceptRequests in a loop. A request object produced by a call may
// be moved to a worker thread and serviced from it. The methods of FcgiRequest
// allow the response to a request to be made without explicit synchronization
// between threads.
//
//    As specified by the FastCGI protocol, the interface fully supports:
// 1) multiple client connections
// 2) request multiplexing over a single connection
// AcceptRequests uses I/O multiplexing on connections and will block until new
// connection requests or request data are present.
//
// Request content validation relative to role expectations:
//    FcgiServerInterface does not validate request information relative to
// FastCGI role expectations. For example, the equality of the number of bytes
// of FCGI_STDIN input and the CONTENT_LENGTH environment variable represented
// as a FCGI_PARAMS name-value pair is not verified for the Responder role.
// Moreover, the presence of expected environment variables for a request as
// defined by the FCGI_PARAMS stream is not verified by FcgiServerInterface.
//
// Request completion and roles:
//   Three separate conditions are used to determine when a request is first
// completed. The role of the request determines which conditions is used.
//    Responder: (FCGI_PARAMS, FCFI_STDIN needed; FCGI_DATA optional.)
//    A Responder request is considerd complete when either:
//    1) No FCGI_DATA records have been received and FCGI_PARAMS and FCGI_STDIN
//       are complete.
//    2) Each of FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA is complete.
//
//    Authorizer: (FCGI_PARAMS needed; FCGI_STDIN and FCGI_DATA optional.)
//    An Authorizer request is considered complete when either:
//    1) No FCGI_STDIN or FCGI_DATA records have been received and FCGI_PARAMS
//       is complete.
//    2) No FCGI_DATA records have been received and FCGI_STDIN and FCGI_PARAMS
//       are complete.
//    3) No FCGI_STDIN records have been received and FCGI_DATA and FCGI_PARAMS
//       are complete.
//    4) Each of FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA is complete.
//
//    Filter and unknown roles: (All streams needed.)
//    A Filter request and an unknown request are considered completed when
//    each of FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA are complete.
//
// Configuration:
//    FCGI_LISTENSOCK_FILENO: The FastCGI standard specifies that the listening
// socket descriptor of an interface be equal to STDIN_FILENO == 0 == 
// FCGI_LISTENSOCK_FILENO. This requirement is not enforced by
// FcgiServerInterface. The descriptor of the listening socket is provided
// to the interface constructor as an argument. This descriptor and the file
// description it is associated with are not managed by the interface (though
// it will be made non-blocking).
//
//    The interface is configured with:
// a) A maximum number of concurrent connections.
// b) A maximum number of active requests for a connection.
// c) A default response if a request is aborted by a client before notice of
//    receipt of the request was given by the interface to the application.
// d)    For internet domain sockets (AF_INET and AF_INET6), the environment
//    variable FCGI_WEB_SERVER_ADDRS is inspected during interface construction
//    to generate a list of authorized IP addresses.
//       When FCGI_WEB_SERVER_ADDRS is unbound or bound with an empty value,
//    address validation does not occur. The internet "any address" special
//    address values (0.0.0.0 for IPv4 and :: for IPv6) have no special meaning
//    to FcgiServerInterface. If a client connection from any address should
//    be accepted, FCGI_WEB_SERVER_ADDRS should be unbound or bound with an
//    empty value.
// 
// Overloaded state:
//    The interface may be put into and removed from an overloaded state. This
// state allows previously-received requests to be serviced while preventing
// new requests or connections from being accepted. 
//
// Bad state:
//    During use, the interface or FcgiRequest objects produced by the
// interface may encounter errors which corrupt the state of the interface.
// When this occurs, the interface assumes a bad state. The current state of the
// interface may be queried by calling interface_status. Once in a bad state,
// the interface should be destroyed.
//
// Synchronization:
//    It is expected that all public methods of FcgiServerInterface are called
// on the interface from the same thread which houses the interface. In other
// words, interface method calls are not thread safe. In particular, putting
// the interface into or removing the interface from an overloaded state
// should be performed synchronously with the thread which houses the interface.
//    Synchronization of the destruction of an interface and the destruction of
// FcgiRequest objects produced by the interface need not be explicitly handled.
//
// Program termination:
//    It may occur that an underlying system error would prevent an invariant
// from being maintained. In these cases, the interface terminates the program
// by calling std::terminate. A termination handler is not registered by the
// interface.
class FcgiServerInterface {
 public:

  // Attempts to return a list of FcgiRequest objects which are ready for 
  // service. Attempts to update internal state as appropriate for data and
  // connection requests sent by clients. 
  //
  // Preconditions:
  // 1) Signal handling: SIGPIPE must be appropriately handled by the
  //    application. If SIGPIPE is not handled, the default system behavior of
  //    program termination will apply when it is discovered through a write
  //    operation that a connection was closed by the peer.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) An unrecoverable error from a system call causes a std::system_error
  //    exception to be thrown with the value of errno set by the call.
  // 3) If the interface is in a bad state as determined by a call of
  //    interface_status, an exception is thrown.
  // 4) In the event of a throw, the returned value of a call to
  //    interface_status may be used to determine if recovery can be
  //    performed. A returned value of false indicates that recovery is not
  //    possible.
  // 5) After recovery from an exception, a connection may have been scheduled
  //    for closure. 
  //    a) Requests associated with the connection which had not yet been used
  //       to construct an FcgiRequest object will be deleted. 
  //    b) Requests associated with the connection which had been used to
  //       construct an FcgiRequest object will no longer communicate with
  //       their client.
  //    c) Closure of the connection implies the termination of all requests
  //       received over the connection.
  //
  // Effects:
  // 1) A call blocks until data or connection requests are received, with the
  //    following exceptions:
  //    a) In the case that requests which had been generated by a previous 
  //       call could not be returned because of an exception, those requests
  //       are returned immediately.
  //    b) AcceptRequests returns if it was blocked waiting for incoming
  //       connections or data and either:
  //       1) A signal affected the interface thread (errno == EINTR).
  //       2) An FcgiRequest object updated interface state in a way that
  //          the interface should respond to. For example, a call will return
  //          when a connection was corrupted by a request object. The
  //          interface state change is processed before the call returns.
  // 2) All connections which were ready for reading were read. Internal state
  //    was updated to reflect the read data.
  // 3) For FCGI_BEGIN_REQUEST records, if the interface was overloaded or the
  //    maximum request limit was met at the time of record receipt, the request
  //    was rejected with an FCGI_END_REQUEST record. The protocol status of
  //    the record was FCGI_OVERLOADED if FCGI_CANT_MPX_CONN could not apply.
  //    Otherwise the protocol status of the record was FCGI_CANT_MPX_CONN.
  //    The application status of the record was EXIT_FAILURE.
  // 4) For FCGI_ABORT_REQUEST records, either the request was deleted from the
  //    interface or state was updated so that inspection by the AbortStatus
  //    method of FcgiRequest will indicate abortion. 
  //    a) Request erasure occurs if the request had not yet been used to 
  //       construct an FcgiRequest object. In this case, an FCGI_END_REQUEST
  //       record was sent for the request. The protocol status was
  //       FCGI_REQUEST_COMPLETE. The application status was that given by the
  //       app_status_on_abort variable during interface construction.
  // 5) If all of the data for a request was received during reading, an
  //    FcgiRequest object was created for the request. It was added to the
  //    returned list.
  // 6) Management requests which were completed during reading were serviced.
  //    The only recognized management request is FCGI_GET_VALUES. If an
  //    error is present in the content of an FCGI_GET_VALUES request, an
  //    FCGI_GET_VALUES_RESULT response with an empty body was sent.
  //    All other management requests received an FCGI_UNKNOWN_TYPE response.
  // 7) New connections which were waiting to be accepted were accepted. 
  //    a) For internet domains, connections were validated against the list of
  //       authorized IP addresses if the list contains addresses. Unauthorized
  //       connections were immediately closed.
  //    b) If the interface was overloaded or the maximum number of connections
  //       was met, new connections were immediately closed.
  //    c) Connections were validated for socket domain and socket type. The
  //       reference domain and type were those determined from 
  //       listening_socket during interface construction.
  // 8) Connections which were scheduled to be closed were closed. Connection
  //    closure scheduling occurs in several cases:
  //    a) On the completion of a request for which the FCGI_KEEP_CONN flag was
  //       not set in the request's FCGI_BEGIN_REQUEST record. Closure will 
  //       occur even if other requests on the connection have been received
  //       from the client.
  //    b) As part of the response to the discovery that a client closed its
  //       end of a connection.
  //    c) The request was aborted by an FCGI_ABORT_REQUEST record, the request
  //       had not yet been used to construct an FcgiRequest object, and the
  //       FCGI_KEEP_CONN flag of the request's FCGI_BEGIN_REQUEST record was
  //       not set. (This is a consequence of a.)
  //    d) A write operation on the connection blocked for a time that exceeded
  //       the limit set by fcgi_si::write_block_timeout. The interface assumed
  //       that the connection was no longer being read by the client.
  //    e) If an error during reading or writing corrupted the connection or
  //       corrupted internal state associated with the connection. Corruption
  //       is associated with errors; an exception is thrown at the source of
  //       an error.
  std::vector<FcgiRequest> AcceptRequests();

  // Gets the current number of connected sockets which were accepted by
  // the listening socket associated with listening_socket.
  //
  // Preconditions: none.
  inline std::size_t connection_count() const noexcept
  {
    return record_status_map_.size() + dummy_descriptor_set_.size();
  }

  // Returns the current overload status of the interface. Returns false
  // unless the interface was put into an overloaded state by a call of
  // set_overload(true).
  //
  // Preconditions: none.
  inline bool get_overload() const noexcept
  {
    return application_overload_;
  }

  // Returns the current state of the interface. False indicates that the
  // interface is in a bad state and should be destroyed.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) Throws std::system_error if the synchronization primitive used to
  //    access interface state encounters an error.
  bool interface_status() const;

  // Sets the overload flag of the interface to overload_status. 
  //
  // Parameters:
  // overload_status: True if the interface should be put into the overloaded
  //                  state. False if the interface should be put into the
  //                  regular state.
  // 
  // Preconditions: none.
  //
  // Effects:
  // 1) While the flag is set:
  //    a) All new connections will be accepted and then immediately closed.
  //    b) All FCGI_BEGIN_REQUEST records will be rejected with an 
  //       FCGI_END_REQUEST record. The protocol_status of the record will
  //       be FCGI_CANT_MPX_CONN if it is applicable and FCGI_OVERLOADED
  //       otherwise. The application status of the record will be EXIT_FAILURE.
  //       After the rejection of an FCGI_BEGIN_REQUEST record, any received
  //       records which are associated with the rejected request will be
  //       ignored.
  //    c) Management requests will be serviced normally.
  //    d) Requests which were previously accepted from the receipt of an
  //       FCGI_BEGIN_REQUEST record will continue to be able to receive data.
  //    e) Requests which were previously assigned to the application through
  //       the production of an FcgiRequest object may be serviced normally.
  inline void set_overload(bool overload_status) noexcept
  {
    application_overload_ = overload_status;
  }

  // Parameters:
  // listening_descriptor: The descriptor of the listening socket to be
  //                       used by the interface to accept connections.
  // max_connections:      The maximum number of socket connections of those
  //                       which were accepted by the listening socket.
  // max_requests:         The maximum number of active requests for a
  //                       socket connection.
  // app_status_on_abort:  The application status which will be returned by the
  //                       interface in the case that both:
  //                       a) An abort is requested by a client with an 
  //                          FCGI_ABORT_REQUEST record. 
  //                       b) The request has yet to be assigned to the
  //                          application by the generation of an FcgiRequest
  //                          object.
  //                       The default value is EXIT_FAILURE.
  //
  // Preconditions:
  // 1) Signal handling: SIGPIPE must be handled by the application. Failure to
  //    handle SIGPIPE will result in the default behavior of program
  //    termination upon the discovery of a closed socket connection by a
  //    write.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) An unrecoverable error from a system call causes a std::system_error
  //    exception to be thrown with the value of errno set by the call.
  // 3) (Configuration errors) An exception is thrown if:
  //    a) Either of max_connections or max_requests is less than or equal to
  //       zero.
  //    b) listening_socket does not refer to a socket.
  //    c) The socket type of listening_socket is not SOCK_STREAM.
  //    d) The socket provided by listening_socket is not listening.
  //    e) The socket domain of listening_socket is AF_INET or AF_INET6,
  //       FCGI_WEB_SERVER_ADDRS is bound to a non-empty value, and no
  //       valid addresses are found when that value is processed. An address
  //       is valid if inet_pton finds the address to be valid when given the
  //       appropriate socket domain.
  // 4) An exception if thrown if, during construction, another
  //    FcgiServerInterface object exists.
  // 5) The file description of listening_descriptor may or may not have been
  //    made non-blocking.
  //
  // Effects:
  // 1) The FcgiServerInterface object is ready to be used in a loop which
  //    calls AcceptRequests to update interface state as clients request
  //    connections and send request data.
  // 2) The new interface object is differentiated from recent, previous 
  //    objects. The new interface is safe to use in the presence of 
  //    FcgiRequest objects which were generated from a previous interface.
  // 3) The file description of listening_descriptor was made non-blocking
  //    (O_NONBLOCK). No other open file status flags were changed.
  FcgiServerInterface(int listening_descriptor, int max_connections,
    int max_requests, std::int32_t app_status_on_abort = EXIT_FAILURE);

  // No copy, move, or default construction.
  FcgiServerInterface() = delete;
  FcgiServerInterface(const FcgiServerInterface&) = delete;
  FcgiServerInterface(FcgiServerInterface&&) = delete;
  FcgiServerInterface& operator=(const FcgiServerInterface&) = delete;
  FcgiServerInterface& operator=(FcgiServerInterface&&) = delete;

  ~FcgiServerInterface();

 private:
  // MEMBER TYPES

  enum class RequestStatus {kRequestPending, kRequestAssigned};

  class RequestData {
   public:
    using size = std::allocator_traits<std::allocator<std::uint8_t>>::size_type;

    inline void set_connection_closed_by_interface() noexcept
    {
      connection_closed_by_interface_ = true;
    }

    inline RequestStatus get_status() const noexcept
    {
      return request_status_;
    }

    inline bool get_abort() const noexcept
    {
      return client_set_abort_;
    }

    inline void set_abort() noexcept
    {
      client_set_abort_ = true;
    }

    inline bool get_close_connection() const noexcept
    {
      return close_connection_;
    }

    inline std::uint16_t get_role() const noexcept
    {
      return role_;
    }

    // Checks if a request has been received in-full according to the
    // completion logic of the role of the request.
    //
    // Parameters: none.
    //
    // Preconditions: none.
    //
    // Effects:
    // 1) Complete request receipt was checked and the status returned by:
    //    FCGI_RESPONDER:  FCGI_PARAMS and FCGI_STDIN are complete and either
    //                     FCGI_DATA is complete or no data has been received
    //                     for FCGI_DATA.
    //    FCGI_AUTHORIZER: FCGI_PARAMS is complete and a combination of
    //                     FCGI_STDIN and FCGI_DATA being complete or not being
    //                     associated with data is true.
    //    FCGI_FILTER:     FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA are complete.
    // 2) If a request was complete, the stream completion observers return
    //    true. E.g. get_DATA_completion returns true for any completed request.
    bool CheckRequestCompletionWithConditionalUpdate() noexcept;

    // Attempts to convert the FCGI_PARAMS byte sequence which is encoded in the
    // FastCGI name-value pair format into a std::map object with names as
    // map keys and values as map values. The map is held within the
    // RequestData object and later used in the construction of an FcgiRequest
    // object.
    //
    // Parameters: none
    //
    // Preconditions:
    // 1) This method may only be called once the FCGI_PARAMS stream is complete
    //    as determined by get_PARAMS_completion.
    //
    // Exceptions:
    // 1) May throw exceptions derived from std::exception.
    // 2) In the event of a throw, the call had no effect on the RequestData
    //    object (strong exception guarantee).
    //
    // Effects:
    // 1) If true was returned, the FCGI_PARAMS byte sequence was used to
    //    construct an internal environment variable map.
    // 2) If false was returned, the FCGI_PARAMS byte sequence had a FastCGI
    //    name-value pair binary formatting error or the list of environment
    //    variable definitions had distinct definitions for the same variable.
    bool ProcessFCGI_PARAMS();

    inline bool get_PARAMS_completion() const noexcept
    {
      return FCGI_PARAMS_complete_;
    }

    inline void CompletePARAMS() noexcept
    {
      FCGI_PARAMS_complete_ = true;
    }

    inline void AppendToPARAMS(const std::uint8_t* buffer_ptr, size count)
    {
      FCGI_PARAMS_.insert(FCGI_PARAMS_.end(), buffer_ptr, buffer_ptr + count);
    }

    inline bool get_STDIN_completion() const noexcept
    {
      return FCGI_STDIN_complete_;
    }

    inline void CompleteSTDIN() noexcept
    {
      FCGI_STDIN_complete_ = true;
    }

    inline void AppendToSTDIN(const std::uint8_t* buffer_ptr, size count)
    {
      FCGI_STDIN_.insert(FCGI_STDIN_.end(), buffer_ptr, buffer_ptr + count);
    }

    inline bool get_DATA_completion() const noexcept
    {
      return FCGI_DATA_complete_;
    }

    inline void CompleteDATA() noexcept
    {
      FCGI_DATA_complete_ = true;
    }

    inline void AppendToDATA(const std::uint8_t* buffer_ptr, size count)
    {
      FCGI_DATA_.insert(FCGI_DATA_.end(), buffer_ptr, buffer_ptr + count);
    }

    RequestData() = default;
    RequestData(std::uint16_t role, bool close_connection);
    
    // Move only.
    RequestData(RequestData&&) = default;
    RequestData& operator=(RequestData&&) = default;

    // No copy.
    RequestData(const RequestData&) = delete;
    RequestData& operator=(const RequestData&) = delete;

    ~RequestData() = default;

   private:
    // Make FcgiRequest a friend class as its constructor will move data
    // from the representation of RequestData objects.
    friend class FcgiRequest;

    // Request data and completion status
    bool                      FCGI_PARAMS_complete_ {false};
    bool                      FCGI_STDIN_complete_  {false};
    bool                      FCGI_DATA_complete_   {false};
    std::vector<std::uint8_t> FCGI_PARAMS_          {};
    std::vector<std::uint8_t> FCGI_STDIN_           {};
    std::vector<std::uint8_t> FCGI_DATA_            {};

    // Map to hold processed FCGI_PARAMS_ data.
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
                  environment_map_ {};

    // Request metadata
    std::uint16_t role_;
    bool          client_set_abort_               {false};
    bool          close_connection_;
    RequestStatus request_status_                 {RequestStatus::kRequestPending};
    bool          connection_closed_by_interface_ {false};
  };

  // RecordStatus objects are used as internal components of an
  // FcgiServerInterface object. A RecordStatus object represents the status of
  // a FastCGI record as it is received over a socket connection. The method
  // ReadRecords updates interface state as records are received over the
  // connection. It produces a list of requests which are complete and ready to
  // be used to create an FcgiRequest object.
  class RecordStatus {
   public:

    // Reads FastCGI records from a connected socket until it blocks, is found 
    // to be closed, or an error prevents further reading. While reading,
    // FastCGI records are validated and interface state is updated
    // appropriately. On normal exit, a list is returned of request identifiers.
    // The identifiers indicate the requests which are ready to be used to
    // create an FcgiRequest object.
    //
    // Parameters: none.
    //
    // Preconditions:
    // 1) connection must refer to a connected socket which:
    //    a) Was previously opened by the interface by a call to
    //       AcceptConnection.
    //    b) Has not yet been removed from the interface by a call to 
    //       RemoveConnection.
    //
    // Synchronization:
    // 1) May acquire and release interface_state_mutex_.
    // 2) May implicitly acquire and release the write mutex associated with
    //    the connection of the RecordStatus object.
    //
    // Exceptions:
    // 1) May throw exceptions derived from std::exception.
    // 2) In the event of a throw, either:
    //    a) The connection was scheduled to be closed.
    //    b) The interface was put into a bad state. 
    //    c) The interface was found to be in a bad state and needed information
    //       could not be acquired.
    // 
    // Effects:
    // 1) Returns a list of request identifiers. This list may be empty. The
    //    presence of a request identifier indicates that the associated request
    //    is complete and may be used to construct an FcgiRequest object to be
    //    passed to the application.
    // 2) The return of an empty list does not indicate an absence of side-
    //    effects. For example, interface state may have been updated to track
    //    partially-complete requests or the interface may have sent the
    //    response to a FastCGI management request to a client.
    std::vector<std::map<FcgiRequestIdentifier, RequestData>::iterator>
    ReadRecords();

    RecordStatus() = default;
    RecordStatus(int connection, FcgiServerInterface* interface_ptr);

    // No copy.
    RecordStatus(const RecordStatus&) = delete;
    RecordStatus& operator=(const RecordStatus&) = delete;

    // Move only.  
    RecordStatus(RecordStatus&&) noexcept;
    RecordStatus& operator=(RecordStatus&&) noexcept;

    ~RecordStatus() = default;

   private:

    // Sets the RecordStatus object to a state which is equivalent to the state
    // that it had immediately after normal construction. The local storage 
    // container is not destroyed but is rather cleared.
    void ClearRecord() noexcept;

    inline std::int_fast32_t ExpectedBytes() const noexcept
    {
      return padding_bytes_expected_ + content_bytes_expected_
            + FCGI_HEADER_LEN;
    }

    inline bool IsHeaderComplete() const noexcept
    {
      return bytes_received_ >= FCGI_HEADER_LEN;
    }

    inline bool IsRecordComplete() const noexcept
    {
      return ExpectedBytes() == bytes_received_;
    }

    //    Takes various actions depending on the type of the completed record.
    // Returns either a non-null iterator to request_map_ or an iterator with
    // value equal to that of request_map_end_. A non-end iterator indicates
    // that the associated RequestData object is complete and ready to be used
    // to construct an FcgiRequest object.
    //    (This function is intended to be used within the implementation of
    // ReadRecords.)
    //
    // Parameters:
    // request_iterators_ptr: A pointer to a list of iterators to request_map_
    //                        which were produced by previous calls to
    //                        ProcessCompleteRecord.
    // request_iter_ptr:         A pointer to an iterator to request_map_ which
    //                        serves as a potentially "empty" (end) cache to
    //                        the most recently accessed RequestData object of
    //                        request_map_.
    //                           This parameter is a value-result parameter.
    //
    // Preconditions:
    // 1) The record represented by the RecordStatus object must be complete.
    // 2) ProcessCompleteRecord must not have been previously called on the
    //    complete record.
    // 
    // Caller Responsibilities:
    // 1) If an iterator which is not equal to the end is returned, the list of
    //    iterators returned by ReadRecords must contain an iterator equal to
    //    the returned iterator.
    //
    // Synchronization:
    // 1) May acquire and release interface_state_mutex_.
    // 2) May acquire and release the write mutex associated with connection_.
    //
    // Exceptions:
    // 1) May throw exceptions derived from std::exception.
    // 2) In the event of a throw, either the connection was added to
    //    the closure set or the interface was put into a bad state.
    // 3) An internal exception may result in program termination.
    //
    // Effects:
    // 1) Records which were deemed invalid upon completion of their headers
    //    are ignored. An end iterator is returned. (Note that all record types
    //    which are not listed below and which are not management records are
    //    deemed invalid.)
    // 2) Management record:
    //    a) An end iterator is returned. An appropriate response is sent over
    //       connection_.
    //    b) For FCGI_GET_VALUES, an FCGI_GET_VALUES_RESULT record is sent.
    //    c) Any other type causes an FCGI_UNKNOWN_TYPE record to be sent.
    // 3) Begin request record:
    //    a) An end iterator is returned.
    //    b) request_id_.Fcgi_id() is made active.
    // 4) Abort record:
    //    a) An end iterator is returned.
    //    b) If the request of the record is present and has not been assigned, 
    //       the request is deleted, an FCGI_END_REQUEST record is sent to the 
    //       client, and request_id_.Fcgi_id() is made inactive. 
    //       1) The protocolStatus field of the FCGI_END_REQUEST record is set
    //          to  FCGI_REQUEST_COMPLETE.
    //       2) The appStatus field of the record is equal to
    //          i_ptr_->app_status_on_abort_.
    //    c) If the request of the record is present and has been assigned, 
    //       the abort flag of the associated RequestData object is set. 
    //    Notes:
    //    1) A request may have been assigned and the response to the request
    //       completed between the time of abort record header validation and
    //       the call of ProcessCompleteRecord on the abort record. In other
    //       words, an abort request may be received in-full after the
    //       associated request has been handled but in-part before the
    //       associated request has been handled.
    //    2) It is possible that an abort request is received for a request
    //       which was just received in-full and, as such, is in the list of
    //       iterators to request_map_ which was produced through prior calls
    //       to ProcessCompleteRecord. This case requires removal of the
    //       iterator to such a request. Performing this removal requires
    //       access to the list. request_iter_ptr provides this access.
    // 5) Params, stdin, and data stream records:
    //    a) An end or non-end iterator may be returned.
    //    b) If the size of the content section of the record is non-zero, the
    //       content is appended to the corresponding stream content buffer in 
    //       the RequestData object associated with the identifier. An end
    //       iterator is returned.
    //    c) If the size of the content section of the record is zero, the
    //       corresponding stream is completed. The RequestData object is
    //       checked for completion. If complete, the appropriate iterator
    //       is returned. If it is not complete, an end iterator is returned.
    // 6) The iterator pointed to by request_iter_ptr may have been modified.
    //    It either is equal to request_map_.end() or is a valid iterator of
    //    request_map_.
    std::map<FcgiRequestIdentifier, RequestData>::iterator
    ProcessCompleteRecord(
      std::vector<std::map<FcgiRequestIdentifier, RequestData>::iterator>*
        request_iterators_ptr,
      std::map<FcgiRequestIdentifier, RequestData>::iterator* request_iter_ptr);

    //    Updates record information given a complete header so that the
    // processing of received record data can continue. For example, the byte
    // length of the record is encoded in the header and must be decoded for
    // processing to continue.
    //    Marks the record as invalid if its receipt doesn't make sense given
    // the protocol version implemented by fcgi_si and the history of record
    // receipt on the connection.
    //
    // Parameters:
    // connection: The file descriptor of the connected socket for which the 
    //             RecordStatus object tracks received record data.
    //
    // Preconditions:
    // 1) header_ has been filled with the header bytes of a FastCGI record.
    //
    // Synchronization:
    // 1) Implicitly acquires and releases interface_state_mutex_.
    //
    // Exceptions:
    // 1) May throw exceptions derived from std::exception.
    // 2) After a throw, the values of the private data members are
    //    indeterminate.
    //
    // Effects:
    // 1) content_bytes_expected_, padding_bytes_expected_, type_, and
    //    request_id_ have the values which where encoded in the header.
    // 2) The validity of the record was determined and is recorded in
    //    invalid_record_. 
    // 3) Validity requirements for record types:
    //    a) Management records: 
    //       1) All management records are accepted.
    //    b) Begin request records: 
    //       1) A begin request record for a request which already exists is 
    //          invalid.
    //       2) A begin request record which does not have a record content 
    //          length of eight bytes is invalid 
    //    c) Abort records:
    //       1) Aborts to inactive requests and requests which have already been
    //          aborted are invalid.
    //       2) Abort records which do not have a zero content length are
    //          invalid.
    //    d) Params, stdin, and data records:
    //       1) Records of these types which do not apply to an active
    //          request or which apply to a request whose corresponding stream
    //          has already been completed are invalid. (No content length 
    //          requirements may be given as zero length records have a special
    //          significance and any non-zero length up to the maximum value is
    //          allowed for data transfer.)
    //    e) All other known record types and all unknown record types which are
    //       not management records are invalid.
    void UpdateAfterHeaderCompletion(
      std::map<FcgiRequestIdentifier, RequestData>::iterator* request_iter_ptr);

    int connection_;

    // The header of the FCGI record. The number of valid bytes in a
    // prefix of header is determined by the value of bytes received.
    std::uint8_t header_[FCGI_HEADER_LEN] = {};

    // An accumulator variable to track header, content, and padding
    // completion and, hence, record completion.
    std::int_fast32_t bytes_received_ {0};

    std::uint16_t content_bytes_expected_ {0U};
    std::uint8_t padding_bytes_expected_ {0U};

    // Implementation note:
    // The value zero is used for type_ as no FastCGI record has this value as a
    // type. This is appropriate as no record identity has yet been assigned to
    // the RecordStatus object.
    FcgiType type_ {static_cast<FcgiType>(0U)};
    FcgiRequestIdentifier request_id_ {};

    // When the header is completed, the record is either rejected or
    // accepted. This is performed by UpdateAfterHeaderCompletion.
    // When rejected, all remaining bytes are ignored though the number
    // of bytes received is tracked. Rejection means that the record
    // should not have been sent.
    bool invalidated_by_header_ {false};

    // Management records and an FCGI_BEGIN_REQUEST record require
    // a local buffer as they have non-empty content but do not have
    // an associated application request in which to store the content.
    std::vector<std::uint8_t> local_record_content_buffer_ {};

    FcgiServerInterface* i_ptr_;
  };

  // FRIENDS

  friend class FcgiRequest;

  // HELPER FUNCTIONS

  //    AcceptConnection wraps the accept system call. It performs socket error
  // checking and FastCGI IP address validation. When a connection is accepted,
  // interface state is updated so that requests can be received over the
  // connection.
  //    It is intended that AcceptConnection is called in a loop in the
  // implementation of AcceptRequests. When -1 is returned, a blocking
  // error must have occurred and the loop should terminate.
  //
  // Parameters: none.
  //
  // Preconditions:
  // 1) The socket given by listening_socket_ is a listeningsocket.
  //
  // Synchronization:
  // 1) May implicitly acquire and release interface_state_mutex_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) May terminate the program if an exception occurs which could lead
  //    to a violation of file descriptor usage discipline.
  // 3) On a throw, interface state is not modified (strong exception
  //    guarantee).
  //
  // Effects:
  // 1) Connection validation uses several criteria:
  //    a) maximum_connection_count_
  //    b) application_overload_
  //    c) If valid_ip_address_set_ is non-empty, whether or not the IP address
  //       of the connection is in the set.
  //    d) Whether or not the socket domain and type match socket_domain_ and
  //       socket_type_, respectively.
  //    Failure to meet any criterion results in connection rejection.       
  // 2) If a connection request was pending on listening_socket_ and the
  //    connection was validated after being accepted:
  //    a) A new connected socket with a descriptor equal to the returned value
  //       is present.
  //    b) The socket is non-blocking. 
  //    c) The returned socket descriptor was added to record_status_map_, 
  //       write_mutex_map_, and request_count_map_. The appropriate default
  //       values were added as map values for the descriptor.
  // 3) If a connection was rejected, 0 was returned.
  // 4) If a blocking error was returned by accept, -1 was returned.
  int AcceptConnection();

  // Attempts to add a new RequestData object to request_map_ while
  // maintaining the invariant between request_map_ and request_count_map_.
  //
  // Parameters:
  // request_id:       The FcgiRequestIdentifier value of the new element.
  // role:             The FastCGI role needed to service the new request.
  // close_connection: A flag which indicates if the connection should be
  //                   closed after the request is serviced.
  //
  // Preconditions:
  // 1) interface_state_mutex_ must be held prior to a call.
  // 2) The descriptor of the request must be valid. In particular, it must
  //    already be present as a key of request_count_map_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Ater a throw, either bad_interface_state_detected_ == true or the
  //    call had no effect.
  //
  // Effects:
  // 1) A RequestData object with the given role and close_connection values
  //    was added to request_map_ with a key of request_id.
  // 2) The number of requests associated with request_id.descriptor() in
  //    request_count_map_ was incremented.
  // 3) An iterator to the added request was returned.
  std::map<FcgiRequestIdentifier, RequestData>::iterator AddRequest(
    FcgiRequestIdentifier request_id, std::uint16_t role,
    bool close_connection);

  // Attempts to remove the descriptor given by connection from
  // record_status_map_ and write_mutex_map_ while conditionally updating
  // dummy_descriptor_set_.
  //
  // Parameters:
  // connection: The connected socket descriptor to be removed from the
  //             interface.
  //
  // Preconditions:  
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) A call may cause program termination if an exception occurs which could
  //    result in a file descriptor leak or spurious closure by the interface.
  // 2) A call may throw exceptions derived from std::exception.
  // 3) In the event of a throw:
  //    a) The interface is left in a state which ensures the safe execution of
  //       the interface destructor (basic exception guarantee). In
  //       particular, one of the following is true:
  //       1) connection was removed from both record_status_map_ and
  //          write_mutex_map_ and close(connection) was called.
  //       2) connection remains in both record_status_map_ and write_mutex_map_
  //          and close(connection) was not called.
  //    b) It is indeterminate if the requests in request_map_ which were
  //       associated with connection were removed or modified.
  //    c) It must be assumed that the interface is corrupted and should be
  //       destroyed.
  //    d) bad_interface_state_detected_ == true
  //
  // Synchronization:
  // 1) Attempts to acquire the write mutex associated with connection.
  //
  // Effects:
  // 1) If false was returned, the write mutex could not be obtained without
  //    blocking. The connection and related state are unchanged.
  // 2) If true was returned, the follow apply:
  //    a) Requests in request_map_ which were associated with connection and
  //       which were not assigned were removed from request_map_.
  //    b) Requests in request_map_ which were associated with connection and
  //       which were assigned had the connection_closed_by_interface_ flag of
  //       their RequestData object set.
  //    c) If no assigned requests were present, the connection was closed.
  //    d) If assigned requests were present:
  //       1) The descriptor was added to dummy_descriptor_set_.
  //       2) The connected socket associated with the descriptor was closed.
  //       3) The descriptor is associated with the file description of 
  //          listening_socket_ so that the descriptor will not be reused until
  //          properly processed as a member of dummy_descriptor_set_.
  //    e) The element associated with the key connection was removed from
  //       write_mutex_map_ and record_status_map_.  
  bool RemoveConnection(int connection);

  // Attempts to remove the request pointed to by request_map_iter from
  // request_map_ while also updating request_count_map_.
  //
  // Parameters:
  // request_map_iter: An iterator which points to the 
  //                   std::pair<FcgiRequestIdentifier, RequestData> object of a
  //                   request in request_map_ or to request_map_.end().
  // 
  // Preconditions:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) Throws an exception derived from std::exception if:
  //    a) request_map_iter == request_map_.end()
  //    b) The request count for the descriptor of the request could not
  //       be decremented.
  // 2) After an exception throw:
  //    a) bad_interface_state_detected_ == true.
  //    b) request_map_ and request_count_map_ are unchanged.
  //    b) It must be assumed that the interface is corrupt and should be
  //       destroyed.
  //
  // Effects:
  // 1) If request_id was a key to an item of request_map_ and
  //    request_count_map_[request_id.descriptor()] could be decremented, the
  //    item was removed from request_map_ and 
  //    request_count_map_[request_id.descriptor()] was decremented.
  inline void RemoveRequest(
    std::map<FcgiRequestIdentifier, RequestData>::iterator request_map_iter)
  {
    RemoveRequestHelper(request_map_iter);
  }

  // Attempts to remove the request given by request_id from request_map_ while
  // also updating request_count_map_.
  //
  // Parameters:
  // request_id: The key to request_map_ for the request. This key implicitly
  //             holds the value of the connected socket descriptor of the
  //             request.
  //
  // Preconditions:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Throws an exception derived from std::exception if:
  //    a) No request was present in request_map_ for request_id.
  //    b) The request count for the descriptor of the request could not
  //       be decremented.
  // 3) After an exception throw:
  //    a) bad_interface_state_detected_ == true.
  //    b) request_map_ and request_count_map_ are unchanged.
  //    b) It must be assumed that the interface is corrupt and should be
  //       destroyed.
  //
  // Friends:
  // 1) Depended on by FcgiRequest to safely remove an item from request_map_.
  //
  // Effects:
  // 1) If request_id was a key to an item of request_map_ and
  //    request_count_map_[request_id.descriptor()] could be decremented, the
  //    item was removed from request_map_ and 
  //    request_count_map_[request_id.descriptor()] was decremented.
  inline void RemoveRequest(FcgiRequestIdentifier
    request_id)
  {
    std::map<FcgiRequestIdentifier, RequestData>::iterator find_return 
      {request_map_.find(request_id)};
    RemoveRequestHelper(find_return);
  }

  // A helper function for the overloads of RemoveRequest. It checks
  // iter against request_map_.end() and attempts to decrement the request
  // count on descriptor.
  //
  // Parameters:
  // iter:       An iterator to the request or to request_map_.end() if the
  //             request is not in request_map_.
  //
  // Preconditions:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) Throws an exception derived from std::exception if:
  //    a) No request was present in request_map_ for request_id.
  //    b) The request count for the descriptor of the request could not
  //       be decremented.
  // 2) After an exception throw:
  //    a) bad_interface_state_detected_ == true.
  //    b) request_map_ and request_count_map_ are unchanged.
  //    b) It must be assumed that the interface is corrupt and should be
  //       destroyed.
  //
  // Effects:
  // 1) If request_id was a key to an item of request_map_, the item was
  //    removed from request_map_ and
  //    request_count_map_[request_id.descriptor()] was decremented.
  void RemoveRequestHelper(std::map<FcgiRequestIdentifier, RequestData>::iterator 
    iter);

  // Parameters:
  // connection: the value of the connected socket descriptor for which
  //             requests will be removed.
  //
  // Preconditions:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) In the event of a throw:
  //    a) It must be assumed that the interface is corrupt and should
  //       be destroyed.
  //    b) It is indeterminate if requests were modified or deleted.
  //    c) bad_interface_state_detected_ == true
  // 
  // Effects:
  // 1) Requests associated with connection which were assigned
  //    had the connection_closed_by_interface_ flag of their RequestData
  //    object set.
  // 2) Requests associated with connection which were not assigned were
  //    removed from request_map_.
  // 3) Returns true if requests associated with connection were present and
  //    assigned. Returns false otherwise.
  bool RequestCleanupDuringConnectionClosure(int connection);

  // Attempts to send an FCGI_END_REQUEST record to a client over connection.
  // The request is identified by request_id. The body of the record contains
  // the given protocol_status and app_status fields.
  //
  // Parameters:
  // connection:      The descriptor of connection over which the
  //                  FCGI_END_REQUEST record will be sent.
  // request_id:      A FcgiRequestIdentifier object which identifies the request
  //                  and which will be used to indicate which request sent by
  //                  the client over connection is being ended.
  // protocol_status: The FastCGI protocol status for the final response to the
  //                  request.
  // app_status:      The exit status that an equivalent CGI program would
  //                  have returned on exit.
  //
  // Preconditions:
  // 1) connection must be in use by the interface.
  //
  // Synchronization:
  // 1) May acquire the write mutex associated with connection.
  // 2) May acquire interface_state_mutex_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) May throw any exceptions thrown by SendRecord. See the exception
  //    specification for SendRecord.
  //
  // Effects:
  // 1) If true was returned, an FCGI_UNKNOWN_TYPE record with type was
  //    sent over connection.
  // 2) If false was returned, the connection was found to be closed or
  //    corrupted. The descriptor given by connection is present in a closure
  //    set.
  bool SendFcgiEndRequest(int connection, FcgiRequestIdentifier request_id,
    std::uint8_t protocol_status, std::int32_t app_status);

  // Attempts to send an FCGI_UNKNOWN_TYPE management record. The
  // unknown type of the record body is given by type.
  //
  // Parameters:
  // connection: The descriptor of connection over which the
  //             FCGI_UNKNOWN_TYPE record will be sent. 
  // type:       The type which the FastCGI implementation did not recognize
  //             and which was received as the type of a management record.
  //
  // Preconditions:
  // 1) connection must be in use by the interface.
  //
  // Synchronization:
  // 1) May acquire the write mutex associated with connection.
  // 2) May acquire interface_state_mutex_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) May throw any exceptions thrown by SendRecord. See the exception
  //    specification for SendRecord.
  //
  // Effects:
  // 1) If true was returned, an FCGI_UNKNOWN_TYPE record with type was
  //    sent over connection.
  // 2) If false was returned, the connection was found to be closed or
  //    corrupted. The descriptor given by connection is present in a closure
  //    set.
  bool SendFcgiUnknownType(int connection, FcgiType type);

  // Attempts to send an FCGI_GET_VALUES_RESULT reply to an FCGI_GET_VALUES
  // management record. The binary data of the request is given by
  // [buffer_ptr, buffer_ptr + count).
  //
  // Parameters: 
  // connection: The descriptor of connection over which the 
  //             FCGI_GET_VALUES_RESULT record will be sent.
  // buffer_ptr: A pointer to the first byte a of sequence of name-value pairs
  //             encoded in the FastCGI name-value pair format. Note that
  //             FastCGI headers should not be present in the sequence given by
  //             [buffer_ptr, buffer_ptr + count). 
  // count:      The number of bytes in the binary name-value pair sequence
  //             pointed to by buffer_ptr.
  //
  // Preconditions:
  // 1) connection must be in use by the interface.
  // 2) buffer_ptr may not be null unless count == 0.
  // 3) The byte length of the content of a maximal response, when that content
  //    is encoded in the FastCGI name-value pair format, may not exceed the
  //    maximum size of the content of a FastCGI record. (A maximal response
  //    must be a single, well-formed FastCGI record.)
  //
  // Synchronization:
  // 1) May acquire the write mutex associated with connection.
  // 2) May acquire interface_state_mutex_.
  // 
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Throws std::invalid_argument if a nullptr is given and
  //    content_length != 0.
  // 3) Throws std::invalid_argument if content_length < 0.
  // 4) May throw any exception thrown by SendRecord. See the exception
  //    specification for SendRecord.
  // 5) In the event of a throw, the byte sequence given by 
  //    [content_ptr, content_ptr + content_length) is not modified.
  //
  // Effects:
  // 1) If true was returned, an FCGI_GET_VALUES_RESULT record with one
  //    instance of any understood name from the content of an
  //    FCGI_GET_VALUES record was sent over connection.
  // 2) If false was returned, the connection was found to be closed or
  //    corrupted. The descriptor given by connection is present in a closure
  //    set.
  bool SendGetValuesResult(int connection, const std::uint8_t* buffer_ptr, 
    std::int_fast32_t count);

  // Attempts to send the byte sequence given by 
  // [buffer_ptr, buffer_ptr + count) to a client over connection. Writing
  // may block. If writing blocks, fcgi_si::write_block_timeout is used as
  // as a time limit for a single blocking call.
  //
  // Parameters:
  // connection: The file descriptor of the connection over which data will be
  //             sent.
  // buffer_ptr: A pointer to a byte buffer that contains the byte sequence
  //             to be sent.
  // count:      The number of bytes to send.
  //
  // Preconditions:
  // 1) The byte sequence must be a single, well-formed FastCGI record.
  // 2) SIGPIPE must be handled by the application before calling.
  //
  // Synchronization:
  // 1) Acquires and releases the write mutex associated with
  //    connection.
  // 2) May acquire and release interface_state_mutex_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Explicit throws:
  //    a) std::logic_error if count is negative.
  //    b) std::logic_error if connection was missing from the interface.
  //    c) std::system_error if an unrecoverable system error occurred
  //       during the write. 
  // 3) After a throw, several changes in interface state may have occurred:
  //    a) The connection could have been added to the closure set.
  //    b) The connection could have been corrupted. The corruption flag is
  //       set in this case.
  //    c) The interface may be in a bad state.
  //    No other changes will have occurred.
  // 4) Program termination will occur if the interface could not be put into a
  //    bad state when this was necessary.
  //
  // Effects:
  // 1) If true was returned, the byte sequence was sent.
  // 2) If false was returned, one of the following conditions prevented the
  //    write from completing:
  //    a) It was found that the connection was closed by the client.
  //    b) The connection was found to be corrupted.
  //    c) The most recent blocking call exceeded the
  //       fcgi_si::write_block_timeout limit.
  //    In all cases, the descriptor connection should be present in the
  //    closure set.
  bool SendRecord(int connection, const std::uint8_t* buffer_ptr,
    std::int_fast32_t count);

  // DATA MEMBERS
  
  // Non-shared static data members:
  // A timeout for blocking calls related to writing to prevent infinite
  // blocking in the unusual case that a mutex is held by the blocked thread.
  // The number of seconds until such a call times out.
  static constexpr time_t write_block_timeout {300};

  // Configuration parameters:
  int listening_descriptor_;
    // The default application exit status that will be sent when requests
    // are rejected by the interface without involvement of the application.
  std::int32_t app_status_on_abort_;
  int maximum_connection_count_;
  int maximum_request_count_per_connection_;
  int socket_domain_;
    // A list of IP addresses from which the interface will accept connections.
    // The IP version is given by socket_domain_ (AF_INET or AF_INET6).
  std::set<std::string> valid_ip_address_set_;

  // An application-set overload flag.
  bool application_overload_ {false};

  // File descriptors of the self-pipe which is used for wake ups on state
  // changes from blocking during I/O multiplexing for incoming connections 
  // and data.
  int self_pipe_read_descriptor_ {};
  int self_pipe_write_descriptor_ {};

  // This map takes the file descriptor of a connection and accesses the
  // RecordStatus object of the connection. A RecordStatus object summarizes the 
  // current state of record receipt from the client which initiated the
  // connection. Per the FastCGI protocol, information from the client is a
  // sequence of complete FastCGI records.
  std::map<int, RecordStatus> record_status_map_ {};

  std::set<int> dummy_descriptor_set_ {};

  std::vector<FcgiRequest> request_buffer_on_throw_ {};

  ///////////////// SHARED DATA REQUIRING SYNCHRONIZATION START ///////////////

  // A map to retrieve a connection's write mutex. These mutexes are used by
  // the interface and by FcgiRequest objects.
  //
  // The boolean member of std::pair<std::unique_ptr<std::mutex>, bool> is
  // shared state and should only be accessed under the protection of the
  // associated write mutex.
  //
  // (A unique_ptr was used as using std::mutex directly results in
  // compiler errors.)
  std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>> 
    write_mutex_map_ {};

  // Static state used by FcgiRequest objects to check if the interface with
  // which they are associated is alive. The mutex is also used for general
  // synchronization among request objects and between request objects and
  // the interface. interface_identifier_ == 0 if no interface object is
  // currently constructed.
  //
  // unsigned long was chosen as a large integer is desired and, conceptually,
  // modular arithmetic is used when incrementing the identifier.
  static std::mutex interface_state_mutex_;
  static unsigned long interface_identifier_;
  static unsigned long previous_interface_identifier_;

  // This set holds the status of socket closure requests from FcgiRequest
  // objects. This is necessary as a web server can indicate in the
  // FCGI_BEGIN_REQUEST record of a request that the connection used for the
  // request be closed after request service. This status flag allows
  // for an orderly closure of the connection by the interface thread.
  std::set<int> application_closure_request_set_ {};

  // A map to retrieve the total number of requests associated with a
  // connection.
  std::map<int, int> request_count_map_ {};

  // A repository for incomplete request data and a marker for
  // assigned requests. The FcgiRequestIdentifier is the pair defined by the
  // connection socket descriptor value and the FCGI request number.
  std::map<FcgiRequestIdentifier, RequestData> request_map_ {};

  // A flag which indicates that the interface has become corrupt. Ideally,
  // this flag would only be set due to underlying system errors and not
  // because of bugs which are detected by defensive checks.
  bool bad_interface_state_detected_ {false};

  ///////////////// SHARED DATA REQUIRING SYNCHRONIZATION END /////////////////

  const std::map<FcgiRequestIdentifier, RequestData>::iterator request_map_end_
    {request_map_.end()};
};

} // namespace fcgi
} // namespace a_component

// The definition of FcgiRequest must be present to allow proper instantiation
// of some of the template state of FcgiServerInterface.
#include "include/fcgi_request.h"

#endif // A_COMPONENT_FCGI_INCLUDE_FCGI_SERVER_INTERFACE_H_
