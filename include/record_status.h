#ifndef FCGI_SERVER_INTERFACE_INCLUDE_RECORD_STATUS_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_RECORD_STATUS_H_

#include <cstdint>

#include <vector>

#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_identifier.h"

namespace fcgi_si {

// RecordStatus objects are used as internal components of an
// FCGIServerInterface object. They represent the status of a FastCGI record as
// it received over a socket connection. The method Read 
//
class RecordStatus {
public:

  // Reads FastCGI records from a connected socket until it blocks, is found 
  // to be closed, or an error prevents further reading. While reading, FastCGI
  // records are validated and interface state is updated appropriately. On
  // normal exit, a list is returned of request identifiers for the requests 
  // which are ready to be used to create an FCGIRequest object.
  //
  // Parameters:
  // connection: The file descriptor of a connected socket.
  //
  // Preconditions:
  // 1) connection must refer to a connected socket which was previously
  //    opened by the interface by a call to AcceptConnection and has not yet
  //    been removed from the interface by a call to RemoveConnection.
  //
  // Synchronization:
  // 1) mplicitly acquires and
  //
  // Exceptions:
  // 1) May thro exceptions derived from 
  // 
  // Effects:
  //
  std::vector<RequestIdentifier> Read();

  RecordStatus() = default;
  RecordStatus(int connection, FCGIServerInterface* interface_ptr) noexcept;
  RecordStatus(const RecordStatus&) = delete;
  RecordStatus(RecordStatus&&) noexcept;

  RecordStatus& operator=(const RecordStatus&) = delete;
  RecordStatus& operator=(RecordStatus&&) noexcept;

  ~RecordStatus() = default;

private:
  void ClearRecord();

  inline std::uint32_t ExpectedBytes() const noexcept
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

  // Examines the completed record associated with the connected socket
  // represented by connection and performs various actions according to
  // the type of the record. The state of the FCGIServerInterface
  // object associated with the RecordStatus instance may be changed by the
  // call.
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
  RequestIdentifier ProcessCompleteRecord();

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
  // 1) Implicitly acquires and releases 
  //    FCGIServerInterface::interface_state_mutex_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) After a throw, the values of the private data members are indeterminate.
  //
  // Effects:
  // 1) content_bytes_expected_, padding_bytes_expected_, type_, and request_id_
  //    have the values which where encoded in the header.
  // 2) The validity of the record was determined and is recorded in
  //    invalid_record_.
  void UpdateAfterHeaderCompletion();

  int connection_;

  // The header of the FCGI record. The number of valid bytes in a
  // prefix of header is determined by the value of bytes received.
  std::uint8_t header_[FCGI_HEADER_LEN] = {};

  // An accumulator variable to track header, content, and padding
  // completion and, hence, record completion.
  std::uint32_t bytes_received_ {0};

  std::uint16_t content_bytes_expected_ {0};
  std::uint8_t padding_bytes_expected_ {0};

  FCGIType type_ {static_cast<FCGIType>(0)};
  RequestIdentifier request_id_ {};

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

  FCGIServerInterface* i_ptr_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_RECORD_STATUS_H_
