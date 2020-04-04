#ifndef FCGI_SERVER_INTERFACE_INCLUDE_RECORD_STATUS_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_RECORD_STATUS_H_

#include <cstdint>

#include <vector>

#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_identifier.h"

namespace fcgi_si {

class RecordStatus {
public:
  inline bool EmptyRecord() const noexcept
  {
    return content_bytes_expected_ == 0;
  }

  inline bool get_invalid_status() const noexcept
  {
    return invalid_record_;
  }

  inline const std::vector<std::uint8_t>& get_local_content() const noexcept
  {
    return local_record_content_buffer_;
  }

  inline RequestIdentifier get_request_id() const noexcept
  {
    return request_id_;
  }

  inline FCGIType get_type() const noexcept
  {
    return type_;
  }

  std::vector<RequestIdentifier> Read(int connection);

  RecordStatus() = default;
  RecordStatus(FCGIServerInterface* interface_ptr) noexcept;
  RecordStatus(const RecordStatus&) = delete;
  RecordStatus(RecordStatus&&) noexcept;

  RecordStatus& operator=(const RecordStatus&) = delete;
  RecordStatus& operator=(RecordStatus&&) noexcept;

  ~RecordStatus() = default;

private:
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
  void UpdateAfterHeaderCompletion(int connection);

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
  bool invalid_record_ {false};

  // Management records and an FCGI_BEGIN_REQUEST record require
  // a local buffer as they have non-empty content but do not have
  // an associated application request in which to store the content.
  std::vector<std::uint8_t> local_record_content_buffer_ {};

  FCGIServerInterface* i_ptr_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_RECORD_STATUS_H_
