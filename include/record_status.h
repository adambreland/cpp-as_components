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
// it received over a socket connection. The method Read updates interface
// state as records are received over the connection. It produces a list of
// requests which are complete and ready to be used to create an FCGIRequest
// object.
class RecordStatus {
public:

  // Reads FastCGI records from a connected socket until it blocks, is found 
  // to be closed, or an error prevents further reading. While reading, FastCGI
  // records are validated and interface state is updated appropriately. On
  // normal exit, a list is returned of request identifiers. The identifiers
  // indicate the requests which are ready to be used to create an FCGIRequest
  // object.
  //
  // Parameters: none.
  //
  // Preconditions:
  // 1) connection must refer to a connected socket which:
  //    a) Was previously opened by the interface by a call to AcceptConnection.
  //    b) Has not yet been removed from the interface by a call to 
  //       RemoveConnection.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) In the event of a throw, either:
  //    a) The connection was scheduled to be closed.
  //    b) The interface was put into a bad state. 
  //    c) The interface was found to be in a bad state and needed information
  //       could not be acquired.
  //    In either case, additional calls to ReadRecords should not be made.
  // 
  // Effects:
  // 1) Returns a list of request identifiers. This list may be empty. The
  //    presence of a request identifier indicates that the associated request
  //    is complete and may be used to construct an FCGIRequest object to be
  //    passed to the application.
  // 2) The return of an empty list does not indicate an absence of side-
  //    effects. For example, interface state may have been updated to track
  //    partially-complete requests or the interface may have sent the response
  //    to a FastCGI management request to the client.
  std::vector<RequestIdentifier> ReadRecords();

  RecordStatus() = default;
  RecordStatus(int connection, FCGIServerInterface* interface_ptr);

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

  //    Takes various actions depending on the type of the completed record and
  // returns either a non-null RequestIdentifier object or a null object
  // (RequestIdentifier {}). A non-null object indicates that the associated
  // request is complete and ready to be used to construct an FCGIRequest
  // object.
  //    Intended to be used within the implementation of ReadRecords.
  //
  // Parameters: none.
  //
  // Preconditions:
  // 1) The record represented by the RecordStatus object must be complete.
  // 2) ProcessCompleteRecord must not have been previously called on the
  //    complete record.
  // 
  // Caller Responsibilities:
  // 1) If a non-null RequestIdentifier object is returned, the list of
  //    RequestIdentifier objects returned by ReadRecords must contain a
  //    RequestIdentifier object equivalent to the returned object.
  //
  // Synchronization:
  // 1) May acquire and release interface_state_mutex_.
  // 2) May acquire and release the write mutex associated with connection_.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) 
  //
  // Effects:
  // 1) Records which were deemed invalid upon completion of their headers are
  //    ignored. A null RequestIdentifier object is returned. (Note that all
  //    record types not listed below which are not management records are
  //    deemed invalid.)
  // 2) Management record:
  //    a) A null RequestIdentifier object is returned. An appropriate response
  //       is sent over connection_.
  //    b) For FCGI_GET_VALUES, an FCGI_GET_VALUES_RESULT record is sent.
  //    c) Any other type causes an FCGI_UNKNOWN_TYPE record to be sent.
  // 3) Begin request record:
  //    a) A null RequestIdentifier object is returned. 
  //    b) request_id_.FCGI_id() is made active.
  // 4) Abort record:
  //    a) A null RequestIdentifier object is returned.
  //    b) If the request of the record is present and has not been assigned, 
  //       the request is deleted, an FCGI_END_REQUEST record is sent to the 
  //       client, and request_id_.FCGI_id() is made inactive. 
  //       1) The protocolStatus field of the FCGI_END_REQUEST record is set to 
  //          FCGI_REQUEST_COMPLETE (0). 
  //       2) The appStatus field of the record is equal to
  //          i_ptr_->app_status_on_abort_.
  //    c) If the request of the record is present has been assigned, the abort
  //       flag of the associated RequestData object is set.
  // 5) Params, stdin, and data stream records:
  //    a) A null or non-null request identifier may be returned.
  //    b) If the size of the content section of the record is non-zero, the
  //       content is appended to the corresponding stream content buffer in 
  //       the RequestData object associated with the identifier. A null
  //       RequestIdentifier object is returned.
  //    c) If the size of the content section of the record is zero, the
  //       corresponding stream is completed. The RequestData object is
  //       checked for completion. If complete, the identifier is returned.
  //       If it is not complete, a null RequestIdentifier object is returned.
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
  // 1) Implicitly acquires and releases interface_state_mutex_.
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
  //       2) Abort records which do not have a zero content length are invalid.
  //    d) Params, stdin, and data records:
  //       1) Records of these types which do not apply to an active
  //          request or which apply to a request whose corresponding stream
  //          has already been completed are invalid. (No content length 
  //          requirements may be given as zero length records have a special
  //          significance and any non-zero length up to the maximum value is
  //          allowed for data transfer.)
  //    e) All other known record types and all unknown record types which are
  //       not management records are invalid.
  void UpdateAfterHeaderCompletion();

  int connection_;

  // The header of the FCGI record. The number of valid bytes in a
  // prefix of header is determined by the value of bytes received.
  std::uint8_t header_[FCGI_HEADER_LEN] = {};

  // An accumulator variable to track header, content, and padding
  // completion and, hence, record completion.
  std::int_fast32_t bytes_received_ {0};

  std::uint16_t content_bytes_expected_ {0U};
  std::uint8_t padding_bytes_expected_ {0U};

  FCGIType type_ {static_cast<FCGIType>(0U)};
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
