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
  std::vector<FCGIRequest> AcceptRequests();

  inline int connection_count() const
  {
    return record_status_map_.size();
  }

  inline bool get_overload() const
  {
    return application_overload_;
  }

  inline void set_overload(bool overload_status)
  {
    application_overload_ = overload_status;
  }

  // No copy, move, or default construction.
  FCGIServerInterface() = delete;
  FCGIServerInterface(uint32_t max_connections, uint32_t max_requests,
    uint16_t role, int32_t app_status_on_abort = EXIT_FAILURE);
  FCGIServerInterface(const FCGIServerInterface&) = delete;
  FCGIServerInterface(FCGIServerInterface&&) = delete;

  FCGIServerInterface& operator=(const FCGIServerInterface&) = delete;
  FCGIServerInterface& operator=(FCGIServerInterface&&) = delete;

  ~FCGIServerInterface();

 private:
  friend class fcgi_si::FCGIRequest;
  friend class fcgi_si::RecordStatus;

  // HELPER FUNCTIONS

  // AcceptConnection wraps the accept system call. It performs socket error
  // checking and FastCGI IP address validation. When a connection is accepted,
  // interface state is updated so that requests can be received over the
  // connection.
  //
  // It is intended that AcceptConnection is called in a loop in the
  // implementation of AcceptRequests. When -1 is returned, a blocking
  // error must have occurred and the loop should terminate.
  //
  // Preconditions:
  // 1) The file descriptor given by FCGI_LISTENSOCK_FILENO is associated with
  //    a listening socket.
  //
  // Parameters: none.
  //
  // Synchronization:
  // 1) May implicitly acquire and release interface_state_mutex_.
  //
  // Exceptions:
  // 1) Strong exception guarantee for FCGIServerInterface instance state.
  // 2) May directly throw:
  //    a) std::system_error
  //    b) std::logic_error
  //
  // Effects:
  // 1) If a connection request was pending on FCGI_LISTENSOCK_FILENO and the
  //    connection was validated after being accepted, a new connected socket
  //    with a descriptor equal to the returned value is present. The socket
  //    is non-blocking. The returned socket descriptor is added to
  //    record_status_map_, write_mutex_map_, and request_count_map_. The
  //    appropriate default values are added as map values for the descriptor.
  // 2) If the connection request was accepted and then rejected or was
  //    accepted and a non-terminal, non-blocking error was returned by
  //    accept, 0 is returned. A call to AcceptConnection may be made again.
  // 3) If a blocking error was returned by accept, -1 is returned.
  // 4) If accept returned an error which could not be handled, a
  //    std::runtime_error object is thrown with information on the value of
  //    errno set by the call to accept.
  // 5) If another system call returned an error which could not be handled, a
  //    std::runtime_error object is thrown with information on errno.
  // 6) If a violation of the invariant on maps which contain socket
  //    descriptors as keys is found, a std::logic_error object is thrown.
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
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Ater a throw, local_bad_interface_state_detected_ == true.
  //
  // Effects:
  // 1) A RequestData object with the given role and close_connection values
  //    was added to request_map_ with a key of request_id. The number of
  //    requests associated with request_id.descriptor() in request_count_map_
  //    was incremented.
  void AddRequest(RequestIdentifier request_id, uint16_t role,
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
  //    was associated with the description of FCGI_LISTENSOCK_FILENO.
  template <typename C>
  void ConnectionClosureProcessing(C* first_ptr, typename C ::iterator 
    first_iter, typename C ::iterator first_end_iter, C* second_ptr, 
    typename C ::iterator second_iter, typename C ::iterator second_end_iter);

  // Examines the completed record associated with the connected socket
  // represented by connection and performs various actions according to
  // the type of the record. The state of the FCGIServerInterface
  // object may be changed by the call.
  //
  // Intended to be called from within the implementation of Read().
  //
  // Parameters:
  // connection: A connected socket descriptor.
  //
  // Preconditions:
  // 1) The record represented by the RecordStatus object associated
  //    with connection must be complete.
  // 
  //
  // Caller Responsibilities:
  // 1) If a non-null RequestIdentifier object is returned, the list of
  //    RequestIdentifier objects returned by Read() must contain a
  //    RequestIdentifier object equivalent to this object.
  //
  // Effects, in general:
  // 1) Either the null RecordIdentifier object is returned or a non-null
  //    RequestIdentifier object is returned.
  //    a) A non-null RequestIdentifier indicates that the request
  //       is complete. See Caller Responsibilities above.
  //    b) If the returned RequestIdentifier object is null, no action is
  //       required by the caller. Interface state may have been changed.
  //
  // Effects for record types:
  // 1) Management record:
  //    A null RequestIdentifier object is returned. In addition:
  //    An appropriate response is sent over connection.
  //    Also:
  //    a) For FCGI_GET_VALUES, an FCGI_GET_VALUES_RESULT record is sent.
  //    b) Any other type causes an FCGI_UNKNOWN_TYPE record to be sent.
  // 2) Begin request record:
  //    A null RequestIdentifier object is returned. In addition:
  //    a) A begin request record for a request which already exists is ignored.
  //    b) Otherwise, the FCGI_request_ID is made active.
  // 3) Abort record:
  //    A null RequestIdentifier object is returned. In addition:
  //    a) Aborts to inactive requests and requests which have already been
  //       aborted are ignored.
  //    b) If the request of the record has not been assigned, the request is
  //       deleted, an FCGI_END_REQUEST record is sent to the peer, and the
  //       FCGI_request_ID is made inactive. The protocolStatus field of the
  //       record is set to FCGI_REQUEST_COMPLETE (0). The appStatus field of
  //       the record is equal to -1 (in two's complement).
  //    c) If the request of the record has been assigned, the abort variable
  //       of the associated RequestData object is set.
  // 4) Params, stdin, and data stream records:
  //    A null or non-null request identifier may be returned.
  //    a) Stream records of these types which do not apply to an active
  //       request or which apply to a request whose corresponding stream has
  //       already been completed are ignored. A null RequestIdentifier
  //       object is returned.
  //    b) Otherwise, if the size of the content section of the record is
  //       nonzero, the content is appended to the corresponding stream content
  //       buffer in the RequestData object associated with the identifier.
  //       A null RequestIdentifier object is returned.
  //    c) If the size of the content section of the record is zero, the
  //       corresponding stream is completed. The RequestData object is
  //       checked for completion. If complete, the identifier is returned.
  //       If not complete, a null RequestIdentifier object is returned.
  RequestIdentifier ProcessCompleteRecord(int connection,
    RecordStatus* record_status_ptr);

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
  //    write_mutex_map_.  
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

  bool SendFCGIEndRequest(int connection, RequestIdentifier request_id,
    uint8_t protocol_status, int32_t app_status);

  bool SendFCGIUnknownType(int connection, fcgi_si::FCGIType type);

  bool SendGetValuesResult(int connection, const RecordStatus& record_status);

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
  // 1) Implicitly acquires and releases the write mutex associated with
  //    connection.
  //
  // Excceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) After a throw, several changes in interface state may have occurred:
  //    a) The connection could have been added to connections_to_close_set_.
  //    b) The connection could have been corrupted. The corruption flag is
  //       set in this case.
  //    c) The interface may be in a bad state.
  //    No other changes will have occurred.
  //
  // Effects:
  // 1) If true was returned, the byte sequence was sent.
  // 2) If false was returned, the connection was found to be closed. The
  //    descriptor given by connection was added to connections_to_close_set_.
  bool SendRecord(int connection, const uint8_t* buffer_ptr, std::size_t count);

  // DATA MEMBERS

  // Configuration parameters:
    // The default application exit status that will be sent when requests
    // are rejected by the interface without involvement of the application.
  int32_t app_status_on_abort_;
  uint32_t maximum_connection_count_;
  uint32_t maximum_request_count_per_connection_;
  uint16_t role_;
  int socket_domain_;
    // A list of IP addresses from which the interface will accept connections.
    // The IP version is given by socket_domain_ (AF_INET or AF_INET6).
  std::set<std::string> valid_ip_address_set_;

  // The state of the application-set overload flag.
  bool application_overload_ {false};

  // This map takes the file descriptor of the connection and accesses the
  // RecordStatus object which summarizes the current state of record receipt
  // from the client which initiated the connection. Per the FastCGI protocol,
  // information from the client is a sequence of complete FastCGI records.
  std::map<int, RecordStatus> record_status_map_ {};

  // A set for connections which were found to have been closed by the peer or
  // which were corrupted by the interface through a partial write.
  // Connection closure occurs in a cleanup step.
  std::set<int> connections_to_close_set_ {};

  std::set<int> dummy_descriptor_set_ {};

  // A flag which is checked by AcceptRequests when it is entered. When
  // set, AcceptRequests throws immediately.
  bool local_bad_interface_state_detected_ {false};

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
