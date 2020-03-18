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

  // Returns true if the request was aborted by the client or the interface.
  // Returns false otherwise.
  //
  // Parameters: none.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) A call may throw an object derived from std::exception. All throws
  //    indication that the request cannot be serviced and that the request
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
  // 1) May throw exceptions derived from std::exception. See Effects.
  //
  // Effects:
  // 1) True was returned.
  //    a) Terminal empty records for the FCGI_STDOUT and FCGI_STDERR streams
  //       were sent. The records close these streams according to the
  //       FastCGI protocol. In addition, the client was informed that the
  //       request was serviced by the transmission of a final FCGI_END_REQUEST
  //       record. The application status of this record was given by the value
  //       of app_status.
  //    b) The request was completed. Calls to Complete, Write, and WriteError
  //       will have no effect.
  // 2) False was returned.
  //    a) If the request had not been completed at the time of the call:
  //       1) It was discovered that the connection to the client is closed.
  //          No further action is needed for this request.
  //       2) The request was completed. Calls to Complete, Write, and
  //          WriteError will have no effect.
  //    b) If the request had been completed at the time of the call, the call
  //       has no effect.
  // 3) An exception was thrown:
  //    a) No conclusions may be drawn regarding the transmission of terminal
  //       records or the state of the request object.
  //    b) A non-recoverable error must be assumed. The request should be
  //       destroyed.
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

  template<typename ByteIter>
  std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, std::size_t>
  PartitionByteSequence(ByteIter begin_iter, ByteIter end_iter,
    FCGIType type)
  {
    // Verify that ByteIter iterates over units of data which are the size of
    // a byte.
    static_assert(sizeof(std::uint8_t) == sizeof(decltype(*begin_iter)),
      "A call to PartitionByteSequence<> used an iterator type which did "
      "not iterate over data in units of bytes.");

    std::vector<std::uint8_t> header_byte_sequence {};
    // Only two headers will be needed to describe all records.
    header_byte_sequence.reserve(2*FCGI_HEADER_LEN);
    std::vector<struct iovec> iovec_list {};
    std::size_t number_to_write {0};

    std::size_t total_content_length {std::distance(begin_iter, end_iter)};
    const std::uint8_t* content_base_ptr {static_cast<const std::uint8_t*>(
      static_cast<const void*>(&(*begin_iter)))};

    // The content length of a record should be a multiple of 8 whenever possible.
    // kMaxRecordContentByteLength = 2^16 - 1
    // (2^16 - 1) - 7 = 2^16 - 8 = 2^16 - 2^3 = 2^3*(2^13 - 1) = 8*(2^13 - 1)
    constexpr uint16_t max_aligned_content_length
      {kMaxRecordContentByteLength - 7};

    // Determine how many full records are needed, if a partial record is
    // needed, and related information.
    std::size_t full_record_count
      {total_content_length / max_aligned_content_length};
    // Safe narrowing conversion.
    std::uint16_t partial_record_length
      {static_cast<std::uint16_t>(
        total_content_length % max_aligned_content_length)};
    bool partial_record_count {partial_record_length > 0};
    // Safe narrowing conversion.
    std::uint8_t padding_length
      {(partial_record_length % fcgi_si::FCGI_HEADER_LEN) ?
        static_cast<std::uint8_t>(fcgi_si::FCGI_HEADER_LEN -
          (partial_record_length % fcgi_si::FCGI_HEADER_LEN)) :
        static_cast<std::uint8_t>(0)};
    constexpr std::uint8_t padding[FCGI_HEADER_LEN] = {};

    if(partial_record_count)
    {

      if(padding_length)
    }





    return std::make_tuple(std::move(header_byte_sequence),
      std::move(iovec_list), number_to_write);
  }

  // Attempts to send a byte sequence to the client on the FCGI_STDOUT stream.
  //
  // Parameters:
  // ref:        A constant reference to a container which holds the byte
  //             sequence to be sent.
  // begin_iter: An iterator that points to the first byte of the sequence to
  //             be sent.
  // end_iter:   An iterator that points to one-past-the-last byte of the
  //             sequence to be sent.
  //
  // Preconditions:
  // 1)
  //
  // Exceptions:
  // 1)
  //
  // Effects:
  // 1)
  //
  template<typename ByteIter>
  bool Write(ByteIter begin_iter, ByteIter end_iter)
  {
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
    std::size_t> partition_return
      {PartitionByteSequence(begin_iter, end_iter, FCGIType::kFCGI_STDOUT)};

    return WriteHelper(std::get<1>(partition_return).data(),
      std::get<1>(partition_return).size(), std::get<2>(partition_return),
        false);
  }

  // As for Write, but the stream FCGI_STDERR is used instead of FCGI_STDOUT.
  template<typename ByteIter>
  bool WriteError(ByteIter begin_iter, ByteIter end_iter)
  {
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
    std::size_t> partition_return
      {PartitionByteSequence(begin_iter, end_iter, FCGIType::kFCGI_STDERR)};

    return WriteHelper(std::get<1>(partition_return).data(),
      std::get<1>(partition_return).size(), std::get<2>(partition_return),
        false);
  }

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
  //    a) Any of interface_ptr, request_data_ptr, or write_mutex_ptr are null.
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
    std::mutex* write_mutex_ptr);

  // Determines a partition of the byte sequence defined by
  // [begin_iter, end_iter) whose parts can be sent as the content of FastCGI
  // records. Determines headers and scatter-gather write information
  //
  //
  //
  //
  // bool PartitionByteSequence(const std::vector<uint8_t>& ref,
  //   std::vector<uint8_t>::const_iterator begin_iter,
  //   std::vector<uint8_t>::const_iterator end_iter, fcgi_si::FCGIType type);

  // Attempts to a perform a scatter-gather write on the active socket given
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
  // interface_mutex_held: A flag which allows a caller to indicate whether
  //                       or not interface_state_mutex_ is held before a call.
  //                       This allows WriteHelper to be called in contexts
  //                       which must maintain mutex ownership during the call
  //                       and in contexts which do not have or need mutex
  //                       ownership.
  //
  // Requires:
  // 1) completed_ == false.
  //
  // Exceptions:
  // 1) Exceptions derived from std::exception may be thrown. See Effects.
  //
  // Synchronization:
  // 1) If interface_mutex_held == true, interface_state_mutex_ must be held
  //    by the caller.
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
  // 3) An exception was thrown:
  //    a) No conclusions may be drawn about what part, if any, of the message
  //       was sent.
  //    b) A non-recoverable error must be assumed. The request should be
  //       destroyed.
  // Note: The returned boolean value is propagated in some cases to the user.
  bool WriteHelper(struct iovec* iovec_ptr, int iovec_count,
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

  // A local abort flag that is set when a request abort is detected by:
  // 1) The discovery that the connection over which responses has been sent
  //    has been closed by the client.
  // 2) The discovery that the interface associated with the request has
  //    closed the connection over which a response must be sent.
  // 3) Inspection with a call to AbortStatus.
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
