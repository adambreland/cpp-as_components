#ifndef FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_H_

#include <sys/types.h>     // For ssize_t.
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>         // For uint8_t.
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

#include "include/protocol_constants.h"
#include "include/request_data.h"
#include "include/request_identifier.h"
#include "include/utility.h"

namespace fcgi_si {

// Forward declaration to break the cyclic dependencies between FCGIRequest
// and FCGIServerInterface includes directives.
class FCGIServerInterface;

// FCGIRequest objects are produced by an instance of FCGIServerInterface. 
// A request object contains all of the information given to the interface by a
// client for a FastCGI request. Requests are serviced by inspecting this 
// information, writing to the FCGI_STDOUT and FCGI_STDERR streams from
// calls to Write and WriteError, respectively, and completing the request by 
// a call to Complete.
//
// Requests may be implicitly aborted in three cases: 
// 1) The client sends an FCGI_ABORT record for the request.
// 2) The client closes the connection of the request.
// 3) The interface is forced to close the connection of the request.
// AbortStatus allows the current abort status of request to be inspected.
//
// When connection closure is detected from a call:
// 1) Write, WriteError, and Complete return false. 
// 2) AbortStatus returns true.
// 3) The request is completed.
//
// Requests can be moved but not copied.
//
// Exceptions:
// 1) Calls to AbortStatus, Complete, Write, and WriteError may throw
//    exceptions derived from std::exception.
// 2) In the event of a throw, it must be assumed that an underlying error
//    prevents further servicing of the request. The request object should be
//    destroyed.
//
// Synchronization:
// 1) All calls on a particular request must be made in the same thread.
// 2) Calls on distinct requests in separate threads do not require
//    synchronization.
class FCGIRequest {
 public:

  // Returns true if the request was aborted by the client or the interface.
  // Returns false otherwise.
  //
  // Parameters: none.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) A call may throw an object derived from std::exception. All throws
  //    indicate that the request cannot be serviced and that the request
  //    should be destroyed.
  bool AbortStatus();

  // Completes the response of an FCGIRequest object.
  //
  // Parameters:
  // app_status: The applicaton status that would be returned at the exit of an
  //             equivalent CGI program which served the request. This value is
  //             encoded in the FCGI_END_REQUEST record sent by the interface
  //             to the client.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception.
  // 2) If an exception was thrown:
  //    a) No conclusions may be drawn regarding the transmission of terminal
  //       records or the state of the request object.
  //    b) A non-recoverable error must be assumed. The request should be
  //       destroyed.
  //
  // Effects:
  // 1) If the call returned true:
  //    a) Terminal empty records for the FCGI_STDOUT and FCGI_STDERR streams
  //       were sent. The records close these streams according to the
  //       FastCGI protocol. In addition, the client was informed that the
  //       request was serviced by the transmission of a final FCGI_END_REQUEST
  //       record. The application status of this record was given by the value
  //       of app_status.
  //    b) The request was completed. Calls to Complete, Write, and WriteError
  //       will have no effect.
  // 2) If the call returned false:
  //    a) If the request had not been completed at the time of the call:
  //       1) It was discovered that the connection to the client is closed.
  //          No further action is needed for this request.
  //       2) The request was completed. Calls to Complete, Write, and
  //          WriteError will have no effect.
  //    b) If the request had been completed at the time of the call, the call
  //       had no effect.
  bool Complete(int32_t app_status);

  inline bool get_completion_status() const noexcept
  {
    return completed_;
  }

  // Returns a constant reference to the FCGI_DATA byte sequence sent by the
  // client for the request.
  inline const std::vector<uint8_t>& get_DATA() const noexcept
  {
    return request_data_content_;
  }

  // Returns a constant reference to a std::map object which holds the
  // environment variables associated with the request. Keys of the map are
  // environment variable names.
  inline const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
  get_environment_map() const noexcept
  {
    return environment_map_;
  }

  inline uint16_t get_role() const noexcept
  {
    return role_;
  }

  // Returns a constant reference to the FCGI_STDIN byte sequence sent by the
  // client for the request.
  inline const std::vector<uint8_t>& get_STDIN() const noexcept
  {
    return request_stdin_content_;
  }

  // Attempts to send a byte sequence to the client on the FCGI_STDOUT stream.
  //
  // Parameters:
  // begin_iter: An iterator that points to the first byte of the sequence to
  //             be sent.
  // end_iter:   An iterator that points to one-past-the-last byte of the
  //             sequence to be sent.
  //
  // Preconditions:
  // 1) The range formed by [begin_iter, end_iter) must be a contiguous
  //    sequence of byte-sized objects.
  //
  // Exceptions:
  // 1) A call may throw exceptions derived from std::exception. 
  // 2) If an exception was thrown:
  //    a) No conclusions may be drawn about what part, if any, of the message
  //       was sent.
  //    b) A non-recoverable error must be assumed. The request should be
  //       destroyed.
  //
  // Effects:
  // 1) If true was returned.
  //    a) The byte sequence given by [begin_iter, end_iter) was sent to the 
  //       client.
  // 2) If false was returned.
  //    a) If the request had not been previously completed:
  //       1) The connection was found to be closed. No further action need be
  //          taken to service the request. The request should be destroyed.
  //       2) The request was completed. Calls to Complete, Write, and
  //          WriteError will have no effect.
  //    b) If the request had been previously completed, the call had no effect.
  template<typename ByteIter>
  bool Write(ByteIter begin_iter, ByteIter end_iter);

  // As for Write, but the stream FCGI_STDERR is used instead of FCGI_STDOUT.
  template<typename ByteIter>
  bool WriteError(ByteIter begin_iter, ByteIter end_iter);

  // No copy or default construction.
  FCGIRequest() = delete;

  FCGIRequest(const FCGIRequest&) = delete;
  FCGIRequest(FCGIRequest&&) noexcept;

  FCGIRequest& operator=(const FCGIRequest&) = delete;
  FCGIRequest& operator=(FCGIRequest&&) noexcept;

  ~FCGIRequest();

 private:
  friend class fcgi_si::FCGIServerInterface;

  // The constructor is private as only an FCGIServerInterface object
  // should create FCGIRequest objects through calls to AcceptRequests().
  //
  // Parameters:
  // request_id:       The RequestIdentifier used as the key for the request
  //                   request_map_.
  // interface_id:     The current value of
  //                   FCGIServerInterface::interface_identifier_. This value
  //                   is used by an FCGIRequest object to check if the
  //                   interface which created an FCGIRequest object has not
  //                   been destroyed.
  // interface_ptr:    A pointer to the current FCGIServerInterface object.
  //                   The pointer is equal to this in interface method calls.
  // request_data_ptr: A pointer to the RequestData object associated the
  //                   the RequestIdentifier key of request_map_.
  // write_mutex_ptr:  A pointer to the write mutex of the connection
  //                   over which the request was sent. The descriptor of this
  //                   connection is equal to request_id.descriptor().
  //
  // Preconditions:
  // 1) request_id_ is a key of request_map_.
  // 2) All pointers are associated with the FCGIServerInterface object
  //    of request_map_. The correct RequestData object and write mutex pair
  //    were used to initialize request_data_ptr, write_mutex_ptr, and
  //    bad_connection_state_ptr.
  // 3) interface_id is the identifier of the FCGIServerInterface object
  //    associated with request_map_.
  //
  // Synchronization requirements and discussion:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) Throws std::logic_error if:
  //    a) Any of interface_ptr, request_data_ptr, write_mutex_ptr, or
  //       bad_connection_state_ptr are null.
  //    b) An FCGIRequest has already been generated from *request_data_ptr.
  //
  //    If a throw occurs, bad_interface_state_detected_ is set (as this
  //    means that the implementation of FCGIServerInterface has an error).
  //
  // Effects:
  // 1) Constructs an FCGIRequest which:
  //    a) Contains the environment variable (FCGI_PARAMS), FCGI_STDIN, and
  //       FCGI_DATA information of the request.
  //    b) Contains the role and connection-closure-upon-response-completion
  //       information of the request.
  //    c) Is associated with the interface object which created it.
  // 2) After construction, request_status_ == RequestStatus::kRequestAssigned
  //    for the RequestData object given by *request_data_ptr.
  FCGIRequest(RequestIdentifier request_id, unsigned long interface_id,
    FCGIServerInterface* interface_ptr, RequestData* request_data_ptr,
    std::mutex* write_mutex_ptr, bool* bad_connection_state_ptr);

  // Checks if the interface associated with the request is in a valid state for
  // writing. This member function is designed to be called immediately after
  // interface_state_mutex_ is obtained to begin a write operation.
  //
  // Parameters: none.
  //
  // Preconditions:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception. After a throw, it 
  //    must be assumed that the request cannot be serviced and should be 
  //    destroyed.
  // 2) If an exception was thrown:
  //    a) The request cannot be serviced. The request was completed and should
  //       be destroyed.
  //    b) Either the interface of the request has been destroyed or
  //       bad_interface_state_detected_ == true.
  //
  // Effects:
  // 1) If true was returned:
  //    a) The interface is in a valid state for writing. The write mutex of the
  //       request may be acquired. The file descriptor given by
  //       request_identifier.descriptor() is associated with a valid 
  //       description, i.e. it is associated with the connected socket of the
  //       request.
  // 2) If false was returned:
  //    a) The connection was closed by the interface.
  //    b) The request was completed. completed_ and was_aborted_ were set.
  //    c) The request was removed from the interface.
  bool InterfaceStateCheckForWritingUponMutexAcquisition();

  // Attempts to a perform a scatter-gather write on the active socket given
  // by request_identifier_.descriptor(). If errors occur during the write
  // or if connection closure is discovered, interface invariants are
  // maintained. If interface invariants may not be maintained, the program
  // is terminated.
  //
  // Parameters:
  // iovec_ptr:            A pointer to an array of struct iovec instances.
  // iovec_count:          The number of struct iovec instances from which to
  //                       read data.
  // number_to_write:      The total number of bytes which would be written if
  //                       all the data referenced in the range
  //                       [iovec_ptr, iovec_ptr + iovec_count) was written.
  // interface_mutex_held: A flag which allows a caller to indicate whether
  //                       or not interface_state_mutex_ is held before a call.
  //                       This allows WriteHelper to be called in contexts
  //                       which must maintain mutex ownership during the call
  //                       and in contexts which do not need interface mutex
  //                       ownership over the entire call.
  //
  // Preconditions:
  // 1) completed_ == false.
  // 2) The value of interface_mutex_held must be accurate. In other words,
  //    interface_mutex_held is true if and only if interface_state_mutex_
  //    is held by the caller.
  // 3) If interface_mutex_held == true, then the interface must be in a valid
  //    state.
  //
  // Exceptions:
  // 1) Exceptions derived from std::exception may be thrown.
  // 2) If an exception was thrown:
  //    a) No conclusions may be drawn about what part, if any, of the message
  //       was sent.
  //    b) A non-recoverable error must be assumed. The request should be
  //       destroyed.
  //    c) The transition of completed_ from false to true may or may not
  //       have occurred.
  //    d) Connection corruption invariants were maintained. The connection
  //       may have been corrupted. If so, the descriptor of the connection was
  //       added to application_closure_request_set_.
  //
  // Synchronization:
  // 1) interface_state_mutex_ may be acquired depending on the value of
  //    interface_mutex_held.
  // 2) In general, the write mutex of the request will be acquired.
  //
  // Effects:
  // 1) If true was returned:
  //    a) The message was sent successfully.
  //    b) No change in request state occurred.
  // 2) If false was returned:
  //    a) The connection of the response is closed. The request should be
  //       destroyed.
  //    b) completed_ == true
  //    c) The request was removed from the interface.
  //    d) Connection corruption invariants were maintained. The connection
  //       may have been corrupted. If so, the descriptor of the connection was
  //       added to application_closure_request_set_. 
  bool ScatterGatherWriteHelper(struct iovec* iovec_ptr, int iovec_count,
    std::size_t number_to_write, bool interface_mutex_held);

  // A utility function which allows PartitionByteSequence to partition only
  // a subrange of the range [begin_iter, end_iter).
  //
  // As for Write and Write error.
  template<typename ByteIter>
  bool WriteHelper(ByteIter begin_iter, ByteIter end_iter, FCGIType type);

  // State for internal request management. Constant after initialization.
  unsigned long associated_interface_id_;
  FCGIServerInterface* interface_ptr_;
  RequestIdentifier request_identifier_;
  RequestData* request_data_ptr_;
  std::mutex* write_mutex_ptr_;
  bool* bad_connection_state_ptr_;

  // Request information. Constant after initialization.
  std::map<std::vector<uint8_t>, std::vector<uint8_t>> environment_map_;
  std::vector<uint8_t> request_stdin_content_;
  std::vector<uint8_t> request_data_content_;
  uint16_t role_;
    // A flag which indicates that the connection associated with the request
    // should be closed by the interface after the request is no longer
    // relevant to the interface.
  bool close_connection_;

  // Variables.
    // A local abort flag which partially reflects client_set_abort_ of
    // the request's RequestData instance in request_map_.
  bool was_aborted_;
  bool completed_;
};

} // namespace fcgi_si

#include "include/fcgi_request_templates.h"

#endif // FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_H_
