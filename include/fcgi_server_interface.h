#ifndef FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_H_

#include <cstdint>       // For uint8_t, ... .
#include <cstdlib>       // For EXIT_FAILURE.
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include "include/fcgi_request.h"
#include "include/protocol_constants.h"
#include "include/record_status.h"
#include "include/request_data.h"
#include "include/request_identifier.h"

namespace fcgi_si {

// 
//
//
class FCGIServerInterface {
 public:

  // Returns a list of FCGIRequest objects which are ready for service.
  //
  // Parameters: none.
  //
  // Preconditions:
  // 1) Signal handling: SIGPIPE must be appropriately handled by the
  //    application. If SIGPIPE is not handled, the default behavior of
  //    program termination will apply when it is discovered through a write
  //    operation that a connection was closed by the peer.
  //
  // Effects:
  // 1) All connections which were ready for reading were read. Internal state
  //    was updated to reflect the read data.
  // 2) For FCGI_BEGIN_REQUEST records, if the interface was overloaded or the
  //    maximum request limit was met at the time of receipt, the request
  //    was rejected with an FCGI_END_REQUEST record. The protocol status of
  //    the record was FCGI_OVERLOADED or FCGI_CANT_MPX_CONN as appropriate.
  //    The application status of the record was EXIT_FAILURE.
  // 3) For FCGI_ABORT_REQUEST records, either the request was deleted from the
  //    interface or state was updated so that inspection by the AbortStatus
  //    method of FCGIRequest will indicate abortion. 
  //    a) Request erasure occurs if the request had not yet been used to 
  //       construct an FCGIRequest object. In this case, an FCGI_END_REQUEST
  //       record was sent for the request. The protocol status was
  //       FCGI_REQUEST_COMPLETE. The application status was that given by the
  //       app_status_on_abort variable during interface construction.
  // 4) If all of the data for a request was received during reading, an
  //    FCGIRequest object was created for the request. It was added to the
  //    returned list.
  // 5) Management requests which were completed during reading were serviced.
  //    The only currently-recognized management request is FCGI_GET_VALUES. 
  //    All other management requests receive an FCGI_UNKNOWN_TYPE response.
  // 6) New connections which were waiting to be accepted were accepted. 
  //    a) Connections were validated against the list of authorized IP 
  //       addresses if the list contains addresses. Unauthorized connections 
  //       were immediately closed.
  //    b) If the interface was overloaded or the maximum number of connections
  //       was met, new connections were immediately closed.
  //    c) Connections were validated for socket domain and socket type. The
  //       reference domain and type were those determined from 
  //       FCGI_LISTENSOCK_FILENO during interface construction.
  // 7) Connections which were scheduled to be closed were closed. Connection
  //    closure scheduling occurs in two instances:
  //    a) On the completion of a request for which the FCGI_KEEP_CONN flag was
  //       not set in the request's FCGI_BEGIN_REQUEST record. Closure will 
  //       occur even if other requests on the connection have been received
  //       from the client.
  //    b) If an error during reading or writing corrupted the connection or
  //       internal state associated with the connection.
  std::vector<FCGIRequest> AcceptRequests();

  inline std::size_t connection_count() const noexcept
  {
    return record_status_map_.size() + dummy_descriptor_set_.size();
  }

  inline bool get_overload() const noexcept
  {
    return application_overload_;
  }

  // Sets the overload flag of the interface to overload_status. 
  //
  // Parameters:
  // overload_status: True if the interface should be put into the overloaded
  //                  state. False otherwise.
  // 
  // Preconditions: none.
  //
  // Effects:
  // 1) While the flag is set:
  //    a) All new connections will be accepted and them immediately closed.
  //    b) All requests for which data receipt was completed will be rejected
  //       with an FCGI_END_REQUEST record with a protocol status of
  //       FCGI_OVERLOADED and an application status of EXIT_FAILURE.
  //    c) Requests which were previously assigned to the application may
  //       be serviced normally.
  inline void set_overload(bool overload_status) noexcept
  {
    application_overload_ = overload_status;
  }

  // TODO explain how addresses are given in the value of the environment
  // variable FCGI_WEB_SERVER_ADDRS as per the FastCGI standard.
  //
  // TODO explain construction configuration checks and what results in
  // a throw. The public specification is what will be used when constructing
  // tests. Construction behavior must conform to the FastCGI standard and
  // can be more strict.
  FCGIServerInterface(int max_connections, int max_requests,
    std::int32_t app_status_on_abort = EXIT_FAILURE);

  // No copy, move, or default construction.
  FCGIServerInterface() = delete;
  FCGIServerInterface(const FCGIServerInterface&) = delete;
  FCGIServerInterface(FCGIServerInterface&&) = delete;
  FCGIServerInterface& operator=(const FCGIServerInterface&) = delete;
  FCGIServerInterface& operator=(FCGIServerInterface&&) = delete;

  ~FCGIServerInterface();

 private:
  friend class FCGIRequest;
  friend class RecordStatus;

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
  // 1) The file descriptor given by FCGI_LISTENSOCK_FILENO is associated with
  //    a listening socket.
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
  // 2) If a connection request was pending on FCGI_LISTENSOCK_FILENO and the
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
  // request_id:       The RequestIdentifier value of the new element.
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
  //    was added to request_map_ with a key of request_id. The number of
  //    requests associated with request_id.descriptor() in request_count_map_
  //    was incremented.
  void AddRequest(RequestIdentifier request_id, std::uint16_t role,
    bool close_connection);

  // Iterates over the referenced containers of descriptors. These descriptors
  // are scheduled for closure. Attempts to close the descriptors. This 
  // helper is intended to iterate over connections_to_close_set_ and
  // application_closure_request_set_.
  // 
  // Parameters:
  // first_ptr, second_ptr:   Pointers to containers which contain connected
  //                          socket descriptors.
  // first_iter, second_iter: Iterators which point to the starting descriptors
  //                          of *first_ptr and *second_ptr, respectively.
  // first_end_iter:          A pointer to one-past-the-last element of
  //                          *first_ptr which will be processed.
  // second_end_iter:         A pointer to one-past-the-last element of
  //                          *second_ptr which will be processed.
  //
  // Preconditions:  
  // 1) interface_state_mutex_ must be held prior to a call.
  // 2) (Duck typing) C::iterator and C::value_type are types and C::erase is a
  //    member function. All have the usual semantics.
  // 3) C::value_type is int.
  // 4) C::iterator satsifies, at least, the requirements of 
  //    LegacyForwardIterator.
  // 
  // Exceptions:
  // 1) A call may cause program termination if an exception occurs which could
  //    result in a file descriptor leak or spurious closure by the interface.
  // 2) May throw exceptions derived from std::exception.
  // 3) In the event of a throw:
  //    a) the interface is in a state which allows safe execution of the 
  //       interface destructor (basic exception guarantee).
  //    b) bad_interface_state_detected_ == true
  //
  // Effects:
  // 1) Both of the referenced containers were emptied.
  // 2) The connected sockets represented by the descriptors in the union
  //    of the containers were closed.
  // 3) If a connection had assigned requests, the descriptor of the
  //    connection was added to dummy_descriptor_set_ and the descriptor
  //    was associated with the description of FCGI_LISTENSOCK_FILENO in an
  //    atomic fashion. This allows the connection to be closed while 
  //    preventing the reuse of the descriptor by the interface while 
  //    requests which use that descriptor are still present.
  // 4) For every request which is associated with one of the descriptors in 
  //    the sets, the connection_closed_by_interface_ flag of the RequestData 
  //    object of the request was set.
  // 5) write_mutex_map_ and record_status_map_ are updated to reflect the
  //    closure of the connections.
  template <typename C>
  void ConnectionClosureProcessing(C* first_ptr, typename C ::iterator 
    first_iter, typename C ::iterator first_end_iter, C* second_ptr, 
    typename C ::iterator second_iter, typename C ::iterator second_end_iter);

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
  // Effects:
  // 1) Requests in request_map_ which were associated with connection and
  //    which were not assigned were removed from request_map_.
  // 2) Requests in request_map_ which were associated with connection and
  //    which were assigned had the connection_closed_by_interface_ flag of
  //    their RequestData object set.
  // 3) If no assigned requests were present, the connection was closed.
  // 4) If assigned requests were present:
  //    a) The descriptor was added to dummy_descriptor_set_.
  //    b) The connected socket associated with the descriptor was closed.
  //    c) The descriptor is associated with the file description of 
  //       FCGI_LISTENSOCK_FILENO a.k.a. STDIN_FILENO so that the descriptor
  //       will not be reused until properly processed as a member of 
  //       dummy_desriptor_set_.
  // 5) The element associated with the key connection was removed from
  //    write_mutex_map_ and record_status_map_.  
  void RemoveConnection(int connection);

  // Attemps to remove the request pointed to by request_map_iter from
  // request_map_ while also updating request_count_map_.
  //
  // Parameters:
  // request_map_iter: An iterator which points to the 
  //                   std::pair<RequestIdentifier, RequestData> object of a
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
    std::map<RequestIdentifier, RequestData>::iterator request_map_iter)
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
  // 1) Depended on by FCGIRequest to safely remove an item from request_map_.
  //
  // Effects:
  // 1) If request_id was a key to an item of request_map_ and
  //    request_count_map_[request_id.descriptor()] could be decremented, the
  //    item was removed from request_map_ and 
  //    request_count_map_[request_id.descriptor()] was decremented.
  inline void RemoveRequest(RequestIdentifier
    request_id)
  {
    std::map<RequestIdentifier, RequestData>::iterator find_return 
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
  void RemoveRequestHelper(std::map<RequestIdentifier, RequestData>::iterator 
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
  // request_id:      A RequestIdentifier object which identifies the request
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
  bool SendFCGIEndRequest(int connection, RequestIdentifier request_id,
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
  bool SendFCGIUnknownType(int connection, FCGIType type);

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
  // [buffer_ptr, buffer_ptr + count) to a client over connection.
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
  // 2) In the event of a throw, the sequence given by 
  //    [buffer_ptr, buffer_ptr + count) is not modified.
  // 3) Throws:
  //    a) std::logic_error when the interface state was found to be corrupt.
  //       In this case, bad_interface_state_detected_ was set.
  //    b) std::system_error when an unrecoverable system error occurred
  //       while during the write. 
  // 4) After a throw, several changes in interface state may have occurred:
  //    a) The connection could have been added to connections_to_close_set_.
  //    b) The connection could have been corrupted. The corruption flag is
  //       set in this case.
  //    c) The interface may be in a bad state.
  //    No other changes will have occurred.
  // 5) Program termination will occur if the interface could not be put into a
  //    bad state when this was necessary.
  //
  // Effects:
  // 1) If true was returned, the byte sequence was sent.
  // 2) If false was returned, the connection was found to be closed or
  //    corrupted. The descriptor given by connection is present in a closure
  //    set.
  bool SendRecord(int connection, const std::uint8_t* buffer_ptr,
    std::int_fast32_t count);

  // DATA MEMBERS

  // Configuration parameters:
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

  // This map takes the file descriptor of a connection and accesses the
  // RecordStatus object of the connection. A RecordStatus object summarizes the 
  // current state of record receipt from the client which initiated the
  // connection. Per the FastCGI protocol, information from the client is a
  // sequence of complete FastCGI records.
  std::map<int, RecordStatus> record_status_map_ {};

  // A set for connections which were found to have been closed by the peer or
  // which were corrupted by the interface through a partial write.
  // Connection closure occurs in a cleanup step in AcceptRequests.
  std::set<int> connections_to_close_set_ {};

  std::set<int> dummy_descriptor_set_ {};

  std::vector<FCGIRequest> request_buffer_on_throw_ {};

  ///////////////// SHARED DATA REQUIRING SYNCHRONIZATION START ///////////////

  // A map to retrieve a connection's write mutex. These mutexes are used by
  // the interface and by FCGIRequest objects.
  //
  // The boolean member of std::pair<std::unique_ptr<std::mutex>, bool> is
  // shared state and should only be accessed under the protection of the
  // associated write mutex.
  //
  // (A unique_ptr was used as using std::mutex directly results in
  // compiler errors.)
  std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>> 
    write_mutex_map_ {};

  // Static state used by FCGIRequest objects to check if the interface with
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

  // This set holds the status of socket closure requests from FCGIRequest
  // objects. This is necessary as a web server can indicate in the
  // FCGI_BEGIN_REQUEST record of a request that the connection used for the
  // request be closed after request service. This status flag allows
  // for an orderly closure of the connection by the interface thread.
  std::set<int> application_closure_request_set_ {};

  // A map to retrieve the total number of requests associated with a
  // connection.
  std::map<int, int> request_count_map_ {};

  // A repository for incomplete request data and a marker for
  // assigned requests. The RequestIdentifier is the pair defined by the
  // connection socket descriptor value and the FCGI request number.
  std::map<RequestIdentifier, RequestData> request_map_ {};

  // A flag which indicates that the interface has become corrupt. Ideally,
  // this flag would only be set due to underlying system errors and not
  // because of bugs.
  bool bad_interface_state_detected_ {false};

  ///////////////// SHARED DATA REQUIRING SYNCHRONIZATION END /////////////////
};

} // namespace fcgi_si.

#include "include/fcgi_server_interface_templates.h"

#endif // FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_H_
