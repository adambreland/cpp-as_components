#ifndef FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_H_

#include <sys/types.h>     // For ssize_t.
#include <sys/uio.h>

#include <cstdint>         // For uint8_t.

#include <map>
#include <mutex>
#include <vector>

#include "include/protocol_constants.h"
#include "include/request_data.h"
#include "include/request_identifier.h"

namespace fcgi_si {

// Forward declaration to break cyclic dependency between FCGIRequest
// and FCGIServerInterface includes.
class FCGIServerInterface;

class FCGIRequest {
 public:

  // Returns true if the request was aborted. Returns false otherwise.
  //
  // Parameters: none.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) A call may throw an object derived from std::exception. In particular:
  //    a) A throw occurs if the FCGIServerInterface which created the request
  //       has been destroyed.
  //    b) A throw occurs if the call cannot be completed due to an
  //       underlying system error or the detection of invalid interface
  //       state.
  //
  //    All throws should be treated as an indication that the request cannot
  //    be serviced and that the request may be destroyed.
  bool AbortStatus();

  // Completes the response of an FCGIRequest object.
  //
  // Parameters:
  // app_status: The applicaton status that would be returned at the exit of an
  //             equivalent CGI program which served the request. This value is
  //             encoded in the FCGI_END_REQUEST record sent by the interface
  //             to the client.
  //
  // Requires: none.
  //
  // Exceptions:
  // 1)
  //
  // Effects:
  // 1) If the request has already been completed, a call to Complete has
  //    no effects.
  // 2) If the request has not been completed, terminal empty records for the
  //    FCGI_STDOUT and FCGI_STDERR streams are sent. These records close these
  //    streams according to the FastCGI protocol. In addition, the client is
  //    informed that the request has been serviced by sending a final
  //    FCGI_END_REQUEST record. The application status of this record is given
  //    by the value of app_status.
  void Complete(int32_t app_status);

  inline bool get_completion_status() const
  {
    return completed_;
  }

  // Returns a constant reference to the FCGI_DATA byte sequence sent by the
  // client for the request.
  inline const std::vector<uint8_t>& get_DATA() const
  {
    return request_data_content_;
  }

  // Returns a constant reference to a std::map object which holds the
  // environment variables associated with the request. Keys of the map are
  // environment variable names.
  inline const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
  get_environment_map() const
  {
    return environment_map_;
  }

  inline uint16_t get_role() const
  {
    return role_;
  }

  // Returns a constant reference to the FCGI_STDIN byte sequence sent by the
  // client for the request.
  inline const std::vector<uint8_t>& get_STDIN() const
  {
    return request_stdin_content_;
  }

  // Sends a byte sequence to the client on the FCGI_STDOUT stream.
  //
  // Parameters:
  // ref:        A constant reference to a container which holds the byte
  //             sequence to be sent.
  // begin_iter: An iterator that points to the first byte of the sequence to
  //             be sent.
  // end_iter:   An iterator that points to one-past-the-last byte of the
  //             sequence to be sent.
  //
  // Requires:
  // 1)
  //
  // Effects:
  // 1)
  //
  // Exceptions:
  // 1)
  inline bool Write(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter)
  {
    return PartitionByteSequence(ref, begin_iter, end_iter,
      fcgi_si::FCGIType::kFCGI_STDOUT);
  }

  inline bool WriteError(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter)
  {
    return PartitionByteSequence(ref, begin_iter, end_iter,
      fcgi_si::FCGIType::kFCGI_STDERR);
  }

  // No copy or default construction.
  FCGIRequest() = delete;

  FCGIRequest(const FCGIRequest&) = delete;
  FCGIRequest(FCGIRequest&&);

  FCGIRequest& operator=(const FCGIRequest&) = delete;
  FCGIRequest& operator=(FCGIRequest&&);

  ~FCGIRequest() = default;

private:
  friend class fcgi_si::FCGIServerInterface;

  // The constructor is private as only an FCGIServerInterface object
  // should create FCGIRequest objects through calls to AcceptRequests().
  //
  // Parameters:
  //
  // Requires:
  // 1) request_id_ is a key of request_map_.
  // 2) All pointers are associated with the FCGIServerInterface object
  //    of request_map_. The correct RequestData object and write mutex
  //    were used to initialize request_data_ptr and write_mutex_ptr.
  // 3) interface_id is the identifier of the FCGIServerInterface object
  //    associated with request_map_.
  //
  // Synchronization:
  // 1) interface_state_mutex_ must be held prior to a call.
  //
  // Exceptions:
  // 1) Throws std::logic_error if:
  //    a) Any of interface_ptr, request_data_ptr, and write_mutex_ptr are null.
  //    b) An FCGIRequest has already been generated from *request_data_ptr.
  //    If a throw occurs, no change is made to interface state
  //    (strong exception guarantee).
  //
  // Effects:
  // 1) After construction, request_status_ == RequestStatus::kRequestAssigned
  //    for the RequestData object associated with request_id in request_map_.
  FCGIRequest(RequestIdentifier request_id, unsigned long interface_id,
    FCGIServerInterface* interface_ptr, RequestData* request_data_ptr,
    std::mutex* write_mutex_ptr);

  bool PartitionByteSequence(const std::vector<uint8_t>& ref,
    std::vector<uint8_t>::const_iterator begin_iter,
    std::vector<uint8_t>::const_iterator end_iter, fcgi_si::FCGIType type);

  void CompleteAfterDiscoveredClosedConnection();

  // Attempts to a perform scatter-gather write on the active socket given
  // by fd.
  //
  // Parameters:
  // fd:                   The file descriptor of the connected socket to which
  //                       data will be written.
  // iovec_ptr:            A pointer to an array of struct iovec instances.
  // iovec_count:          The number of struct iovec instances to read data
  //                       from.
  // number_to_write:      The total number of bytes which would be written if
  //                       all the data referenced in the range
  //                       [*iovec_ptr, *(iovec_ptr + iovec_count)) was written.
  // interface_mutex_held: A flag which prevents deadlock by allowing mutex
  //                       ownership to be maintained across a call to
  //                       WriteHelper. When true, it is assumed that
  //                       interface_state_mutex_ is held and that interface
  //                       state may be accessed.
  //
  // Requires:
  //
  // Exceptions:
  //
  //
  // Effects:
  //
  bool WriteHelper(int fd, struct iovec* iovec_ptr, int iovec_count,
    std::size_t number_to_write, bool interface_mutex_held);

  unsigned long associated_interface_id_;
  FCGIServerInterface* interface_ptr_;
  RequestIdentifier request_identifier_;
  RequestData* request_data_ptr_;

  std::map<std::vector<uint8_t>, std::vector<uint8_t>> environment_map_;
  std::vector<uint8_t> request_stdin_content_;
  std::vector<uint8_t> request_data_content_;

  // For inspection of the role requested by the client server.
  uint16_t role_;

  // A flag to inform the call to Complete() that the connection associated
  // with the request should be closed by the interface.
  bool close_connection_;

  // A local abort value that is populated when an abort is detected by
  // the discovery of a closed connection or by inspection with AbortStatus.
  bool was_aborted_;

  // Forces the object to act as if it is null. Calls will return null
  // values (empty containers, false) or have no effect (e.g. a second
  // call to complete).
  bool completed_;

  // Synchronization
  std::mutex* write_mutex_ptr_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_H_
