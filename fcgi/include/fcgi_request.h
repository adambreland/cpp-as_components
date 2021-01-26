// MIT License
//
// Copyright (c) 2021 Adam J. Breland
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef AS_COMPONENTS_FCGI_INCLUDE_FCGI_REQUEST_H_
#define AS_COMPONENTS_FCGI_INCLUDE_FCGI_REQUEST_H_

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <vector>

#include "include/fcgi_server_interface.h"
#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_request_identifier.h"
#include "include/fcgi_utilities.h"

namespace as_components {
namespace fcgi {

// See the fcgi namespace README for a discussion of FcgiRequest.
class FcgiRequest {
 public:

  // Returns true if the request was aborted by the client or the interface.
  // Returns false otherwise. In particular, calls on default-constructed and
  // moved-from requests return false.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) A call may throw an object derived from std::exception. All throws
  //    indicate that the request cannot be serviced and that the request
  //    should be destroyed.
  bool AbortStatus();

  // Completes the response of an FcgiRequest object.
  //
  // Note that allowing a valid FcgiRequest to be destroyed without completion
  // prevents terminal records from being sent for the request. The destructor
  // of FcgiRequest maintains interface invariants but does not send
  // information to the client upon request destruction.
  //
  // Parameters:
  // app_status: The application status that would be returned at the exit of an
  //             equivalent CGI program which serviced the request. This value
  //             is encoded in the FCGI_END_REQUEST record sent by the interface
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
  //       of app_status. The protocol status of this record is 
  //       FCGI_REQUEST_COMPLETE.
  //    b) The request was completed. Calls to Complete, Write, and WriteError
  //       will have no effect.
  // 2) If the call returned false:
  //    a) If the request had not been completed at the time of the call:
  //       1) It was discovered that the connection to the client is closed.
  //          No further action is needed for this request.
  //       2) The request was completed. Calls to Complete, Write, and
  //          WriteError will have no effect.
  //    b) If the request had been completed at the time of the call or the
  //       request was default-constructed or moved-from, the call had no
  //       effect.
  inline bool Complete(int32_t app_status)
  {
    return EndRequestHelper(app_status, FCGI_REQUEST_COMPLETE);
  }

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

  // Returns the value of the FCGI_KEEP_CONN flag which was present in the
  // FCGI_BEGIN_REQUEST record for the request.
  inline bool get_keep_conn() const noexcept
  {
    return !close_connection_;
  }

  // Returns the internal request identifier for the request. Request
  // identifiers are ordered pairs whose first component is the socket
  // descriptor of the connection of the request and whose second component
  // is the FastCGI ID of the request.
  inline FcgiRequestIdentifier get_request_identifier() const noexcept
  {
    return request_identifier_;
  }

  // Default-constructed and moved-from requests have a role value of zero.
  // This value does not correspond to any FastCGI role.
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

  // Rejects a request by closing the FCGI_STDOUT and FCGI_STDERR streams
  // and sending a terminal FCGI_END_REQUEST record with an application
  // status given by app_status and a protocol status of FCGI_UNKNOWN_ROLE.
  //
  // Functions as Complete except for the value of protocol status. See above.
  inline bool RejectRole(int32_t app_status)
  {
    return EndRequestHelper(app_status, FCGI_UNKNOWN_ROLE);
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
  //       client. (No FastCGI records are sent if begin_iter == end_iter.)
  // 2) If false was returned.
  //    a) If the request had not been previously completed:
  //       1) The connection was found to be closed or the connection was found
  //          to be corrupted. In the case of corruption, it is unknown how
  //          the connection became corrupt. No further action need be taken to
  //          service the request. The request should be destroyed. The
  //          connection should be present in the closure set (in the case of
  //          corruption, this may depend on the entity which corrupted the
  //          connection).
  //       2) The request was completed. Calls to Complete, Write, and
  //          WriteError will have no effect.
  //    b) If the request had been previously completed or the request was
  //       default-constructed or moved-from, the call had no effect.
  template<typename ByteIter>
  bool Write(ByteIter begin_iter, ByteIter end_iter);

  // As for Write, but the stream FCGI_STDERR is used instead of FCGI_STDOUT.
  template<typename ByteIter>
  bool WriteError(ByteIter begin_iter, ByteIter end_iter);

  FcgiRequest();
  FcgiRequest(FcgiRequest&&) noexcept;

  // Move assignment may only occur to FcgiRequest objects which have been 
  // default-constructed, completed, or moved from. An exception is thrown if
  // a move is attempted to an FcgiRequest object which is in any other state.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) In the event of a throw, neither the source nor the destination
  //    FcgiRequest object were modified (strong exception guarantee).
  FcgiRequest& operator=(FcgiRequest&&);

  // No copy.
  FcgiRequest(const FcgiRequest&) = delete;
  FcgiRequest& operator=(const FcgiRequest&) = delete;

  ~FcgiRequest();

 private:
  friend class FcgiServerInterface;

  // The constructor is private as only an FcgiServerInterface object
  // should create FcgiRequest objects through calls to AcceptRequests().
  //
  // Parameters:
  // request_id:       The FcgiRequestIdentifier used as the key for the request
  //                   request_map_.
  // interface_id:     The current value of
  //                   FcgiServerInterface::interface_identifier_. This value
  //                   is used by an FcgiRequest object to check if the
  //                   interface which created an FcgiRequest object has not
  //                   been destroyed.
  // interface_ptr:    A pointer to the current FcgiServerInterface object.
  //                   The pointer is equal to this in interface method calls.
  // request_data_ptr: A pointer to the RequestData object associated the
  //                   the FcgiRequestIdentifier key of request_map_.
  // write_mutex_ptr:  A pointer to the write mutex of the connection
  //                   over which the request was sent. The descriptor of this
  //                   connection is equal to request_id.descriptor().
  //
  // Preconditions:
  // 1) request_id_ is a key of request_map_.
  // 2) All pointers are associated with the FcgiServerInterface object
  //    of request_map_. The correct RequestData object and write mutex pair
  //    were used to initialize request_data_ptr, write_mutex_ptr, and
  //    bad_connection_state_ptr.
  // 3) interface_id is the identifier of the FcgiServerInterface object
  //    associated with request_map_.
  //
  // Synchronization requirements and discussion:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) Throws std::logic_error if:
  //    a) Any of interface_ptr, request_data_ptr, write_mutex_ptr, or
  //       bad_connection_state_ptr are null.
  //    b) An FcgiRequest has already been generated from *request_data_ptr.
  //
  //    If a throw occurs, bad_interface_state_detected_ is set (as this
  //    means that the implementation of FcgiServerInterface has an error).
  //
  // Effects:
  // 1) Constructs an FcgiRequest which:
  //    a) Contains the environment variable (FCGI_PARAMS), FCGI_STDIN, and
  //       FCGI_DATA information of the request.
  //    b) Contains the role and connection-closure-upon-response-completion
  //       information of the request.
  //    c) Is associated with the interface object which created it.
  // 2) After construction, request_status_ == RequestStatus::kRequestAssigned
  //    for the RequestData object given by *request_data_ptr.
  FcgiRequest(FcgiRequestIdentifier request_id, unsigned long interface_id,
    FcgiServerInterface* interface_ptr,
    FcgiServerInterface::RequestData* request_data_ptr,
    std::mutex* write_mutex_ptr, bool* bad_connection_state_ptr,
    int write_fd);

  // Attempts to complete the STDOUT and STDERR streams and send an
  // FCGI_END_REQUEST record to complete the request. The application status
  // and protocol status of the record are as given.
  //
  // Parameters:
  // app_status:      The application status that would be returned at the exit
  //                  of an equivalent CGI program which served the request.
  //                  This value is encoded in the FCGI_END_REQUEST record sent
  //                  by the interface to the client.
  // protocol_status: A byte value used by the FastCGI interface to communicate
  //                  why the response for a request is complete.
  //
  // Preconditions:
  // 1) protocol_status is one of FCGI_REQUEST_COMPLETE (to indicate successful
  //    servicing of the request) or FCGI_UNKNOWN_ROLE (to indicate that the
  //    application cannot service requests with the role given by role_).
  //
  // Synchronization:
  // 1) Acquires and releases interface_state_mutex_.
  // 2) May acquire and release a write mutex.
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
  //       of app_status. The protocol_status was given by protocol_status.
  //    b) The request was completed. Calls to Complete, Write, and WriteError
  //       will have no effect.
  // 2) If the call returned false:
  //    a) If the request had not been completed at the time of the call:
  //            It was discovered that the connection to the client is closed.
  //          No further action is needed for this request.
  //       2) The request was completed. Calls to Complete, Write, and
  //          WriteError will have no effect.
  //    b) If the request had been completed at the time of the call or the
  //       request was default-constructed or moved-from, the call had no
  //       effect.
  bool EndRequestHelper(std::int32_t app_status, std::uint8_t protocol_status);

  // A helper function which tries to write a null byte to the interface
  // pipe and throws an error if it cannot. This function is used in the
  // current implementation of the prevention of interface blocking in the
  // presence of local work (connection closure requests) and state changes
  // (the transition from good to bad interface state). 
  //
  // Preconditions:
  // 1) The interface associated with the request must exist.
  // 2) The interface associated with the request must be in a valid state.
  // 3) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) Throws std::logic error if any error from a call to write prevented
  //    the write. errno error EINTR is handled.
  //
  // Synchronization:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Effects:
  // 1) A single null byte was written to the interface pipe
  //    (interface_pipe_write_descriptor_). errno error EINTR was handled.
  void InterfacePipeWrite();

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
  // 1) May throw exceptions derived from std::exception.
  // 2) After a throw, it  must be assumed that the request cannot be serviced.
  //    a) The request was completed and should be destroyed.
  //    b) The interface is in a bad state, i.e.
  //       bad_interface_state_detected_ == true.
  // 3) Program termination may occur if interface state cannot be updated
  //    during a throw.
  //
  // Synchronization:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Effects:
  // 1) If true was returned:
  //    a) The interface is in a valid state for writing. The write mutex of the
  //       request may be acquired. The file descriptor given by
  //       request_identifier.descriptor() is associated with a valid 
  //       description, i.e. it is associated with the connected socket of the
  //       request.
  // 2) If false was returned:
  //    Any of the following interface states may have been found:
  //    a) No interface exists.
  //    b) An interface other than the interface which created the request
  //       object exists.
  //    c) The interface of the request is corrupt.
  //    d) The interface closed the connection of the request.
  //
  //    In all of these cases:
  //    a) The request was completed. completed_ and was_aborted_ were set.
  //
  //    If the proper interface was in a good state but closed the connection:
  //    a) The request was removed from the interface.
  bool InterfaceStateCheckForWritingUponMutexAcquisition();

  //    Attempts to a perform a scatter-gather write on the socket given
  // by request_identifier_.descriptor(). Write blocking is subject to the
  // time-out limit set by fcgi_si::write_block_timeout. If errors occur during
  // the write or if connection closure is discovered, interface invariants are
  // maintained. If interface invariants may not be maintained, the program
  // is terminated.
  //    Note that scatter-gather I/O is useful in general for request servicing
  // as user-provided byte sequences must be split into FastCGI records. The
  // header of such records will be stored in a buffer which is not contiguous
  // with that of the user byte sequence. Hence scatter-gather I/O.
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
  //                       and in contexts which do not require interface mutex
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
  //    d) Connection corruption invariants were maintained.
  // 3) Program termination may occur if invariants cannot be maintained during
  //    exceptional behavior.
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
  //    Either:
  //    a) The connection was found to be closed.
  //    b) InterfaceStateCheckForWritingUponMutexAcquisition returned false.
  //    c) The connection was found to be in a corrupted state. 
  //    d) A time-out relative to fcgi_si::write_block_timeout occurred.
  //    
  //    For any of these cases:
  //    a) The request should be destroyed.
  //    b) completed_ == true.
  //    c) Connection corruption invariants were maintained. The connection
  //       may have been corrupted. If so, the descriptor of the connection
  //       was added to application_closure_request_set_.
  //
  //    If a time-out occurred, the connection was added to
  //    application_closure_request_set_.
  //
  //    If the proper interface is in a good state, the request was removed
  //    from the interface.
  bool ScatterGatherWriteHelper(struct iovec* iovec_ptr, int iovec_count,
    std::size_t number_to_write, bool interface_mutex_held);

  // A utility function which allows fcgi_si::PartitionByteSequence to 
  // partition only a subrange of the range [begin_iter, end_iter).
  //
  // As for Write and Write error.
  template<typename ByteIter>
  bool WriteHelper(ByteIter begin_iter, ByteIter end_iter, FcgiType type);

  // State for internal request management. Constant after initialization.
    // Note that default constructed and moved-from FcgiRequest objects have
    // an associated_interface_id_ value of 0U.
  unsigned long associated_interface_id_;
  FcgiServerInterface* interface_ptr_;
  FcgiRequestIdentifier request_identifier_;
  FcgiServerInterface::RequestData* request_data_ptr_;
  std::mutex* write_mutex_ptr_;
  bool* bad_connection_state_ptr_;
  int interface_pipe_write_descriptor_;

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

} // namespace fcgi
} // namespace as_components

#include "include/fcgi_request_templates.h"

#endif // AS_COMPONENTS_FCGI_INCLUDE_FCGI_REQUEST_H_
