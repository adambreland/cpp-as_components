#ifndef FCGI_SERVER_INTERFACE_RECORD_STATUS_H_
#define FCGI_SERVER_INTERFACE_RECORD_STATUS_H_

#include <cstdint>
#include <vector>

#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_identifier.h"

namespace fcgi_si {

class RecordStatus {
public:
  inline bool EmptyRecord() const
  {
    return content_bytes_expected_ == 0;
  }

  inline bool get_invalid_status() const
  {
    return invalid_record_;
  }

  inline const std::vector<uint8_t>& get_local_content() const
  {
    return {local_record_content_buffer_};
  }

  inline fcgi_si::RequestIdentifier get_request_id() const
  {
    return request_id_;
  }

  inline fcgi_si::FCGIType get_type() const
  {
    return type_;
  }

  std::vector<RequestIdentifier> Read(int connection);

  RecordStatus() = default;
  RecordStatus(fcgi_si::FCGIServerInterface* interface_ptr);
  RecordStatus(const RecordStatus&) = delete;
  RecordStatus(RecordStatus&&) = delete;

  RecordStatus& operator=(const RecordStatus&) = delete;
  RecordStatus& operator=(RecordStatus&&);

  ~RecordStatus() = default;

private:
  inline uint32_t ExpectedBytes() const
  {
    return padding_bytes_expected_ + content_bytes_expected_
           + fcgi_si::FCGI_HEADER_LEN;
  }

  inline bool
  IsHeaderComplete() const
  {
    return bytes_received_ >= fcgi_si::FCGI_HEADER_LEN;
  }

  inline bool
  IsRecordComplete() const
  {
    return ExpectedBytes() == bytes_received_;
  }

  void UpdateAfterHeaderCompletion(int connection);

  // The header of the FCGI record. The number of valid bytes in a
  // prefix of header is determined by the value of bytes received.
  uint8_t header_[8];

  // An accumulator variable to track header, content, and padding
  // completion and, hence, record completion.
  uint32_t bytes_received_;

  uint16_t content_bytes_expected_;
  uint8_t padding_bytes_expected_;

  fcgi_si::FCGIType type_;
  fcgi_si::RequestIdentifier request_id_;

  // When the header is completed, the record is either rejected or
  // accepted. This is performed by UpdateAfterHeaderCompletion.
  // When rejected, all remaining bytes are ignored though the number
  // of bytes received is tracked. Rejection means that the record
  // should not have been sent, hence the name invalid_record.
  bool invalid_record_;

  // Management records and an FCGI_BEGIN_REQUEST record require
  // a local buffer as they have non-empty content but do not have
  // an associated application request in which to store the content.
  std::vector<uint8_t> local_record_content_buffer_;

  fcgi_si::FCGIServerInterface* i_ptr_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_RECORD_STATUS_H_
