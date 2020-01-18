#ifndef FCGI_SERVER_INTERFACE_RECORD_STATUS_H_
#define FCGI_SERVER_INTERFACE_RECORD_STATUS_H_

#include <cstdint>
#include <vector>

#include "include/protocol_constants.h"
#include "include/request_identifier.h"

namespace fcgi_si {

// A struct describing the status of the record currently being received
// on the connection. This type is a struct to allow the header and
// bytes_received variables to be accessed directly. Since writes occur
// to other objects but should cause an increment to bytes_received, struct
// status was deemed appropriate.
//
// Usage discipline:
// 1) The first time that the header is completed as determined by
//    bytes_received >= 8, UpdateAfterHeaderCompletion() must be called.
// 2) When associated bytes are processed, the bytes_received accumulator
//    must be incremented appropriately.
// 3) FCGI_BEGIN_REQUEST and management records use the local buffer for
//    data storage. Data should be stored there instead of non-locally in
//    a RequestData object.
// 4) The header of every record is stored locally. Valid header bytes are
//    determined by the value of bytes_received.
struct RecordStatus {

  inline uint32_t ExpectedBytes()
  {
    return padding_bytes_expected + content_bytes_expected
           + fcgi_si::FCGI_HEADER_LEN;
  }

  inline bool
  IsHeaderComplete()
  {
    return bytes_received >= fcgi_si::FCGI_HEADER_LEN;
  }

  inline bool
  IsRecordComplete()
  {
    return ExpectedBytes() == bytes_received;
  }

  RecordStatus();
  RecordStatus(const RecordStatus&) = delete;
  RecordStatus(RecordStatus&&) = delete;

  RecordStatus& operator=(const RecordStatus&) = delete;
  RecordStatus& operator=(RecordStatus&&);

  ~RecordStatus() = default;

  // The header of the FCGI record. The number of valid bytes in a
  // prefix of header is determined by the value of bytes received.
  uint8_t header[8];

  // An accumulator variable to track header, content, and padding
  // completion and, hence, record completion.
  uint32_t bytes_received;

  uint16_t content_bytes_expected;
  uint8_t padding_bytes_expected;

  fcgi_si::FCGIType type;
  fcgi_si::RequestIdentifier request_id;

  // When the header is completed, the record is either rejected or
  // accepted. This is performed by UpdateAfterHeaderCompletion.
  // When rejected, all remaining bytes are ignored though the number
  // of bytes received is tracked. Rejection means that the record
  // should not have been sent, hence the name invalid_record.
  bool invalid_record;

  // Management records and an FCGI_BEGIN_REQUEST record require
  // a local buffer as they have non-empty content but do not have
  // an associated application request in which to store the content.
  std::vector<uint8_t> local_record_content_buffer;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_RECORD_STATUS_H_
