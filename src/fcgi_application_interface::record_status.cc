// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint32_t.
// C++ standard library headers.
#include <mutex>

#include "include/data_types.h"
#include "include/fcgi_application_interface.h"
#include "include/fcgi_request.h"

fcgi_si::FCGIApplicationInterface::RecordStatus::
RecordStatus()
: bytes_received {0}, content_bytes_expected {0}, padding_bytes_expected {0},
  type {0}, request_id {}, invalid_record {false}
{}

RecordStatus& operator=(const RecordStatus& record_status)
{
  for(char i {0}; i < fcgi_si::FCGI_HEADER_LEN; i++)
  {
    header[i] = record_status.header[i];
  }

  bytes_received = record_status.bytes_received;
  content_bytes_expected = record_status.content_bytes_expected;
  padding_bytes_expected = record_status.padding_bytes_expected;
  type = record_status.type;
  request_id = record_status.request_id;
  rejected = record_status.rejected;
  local_record_content_buffer = record_status.local_record_content_buffer;

  return *this;
}

void fcgi_si::FCGIApplicationInterface::RecordStatus::
UpdateAfterHeaderCompletion(int connection)
{
  // Extract number of content bytes from two bytes.
  content_bytes_expected =
    header[fcgi_si::kHeaderContentLengthB1Index];
  content_bytes_expected <<= 8; // one byte
  content_bytes_expected +=
    header[fcgi_si::kHeaderContentLengthB0Index];

  // Extract number of padding bytes.
  padding_bytes_expected =
    header[fcgi_si::kHeaderPaddingLengthIndex];

  // Extract type and request_id.
  type = header[fcgi_si::kHeaderTypeIndex];
  uint16_t FCGI_request_id =
    header[fcgi_si::kHeaderRequestIDB1Index];
  FCGI_request_id << 8; // one byte
  FCGI_request_id +=
    header[fcgi_si::kHeaderRequestIDB0Index];
  request_id = fcgi_si::
    RequestIdentifier(connection, FCGI_request_id);

  // Determine if the record should be rejected based on header
  // information.

  // Every management record is accepted.
  if(FCGI_request_id == fcgi_si::FCGI_NULL_REQUEST_ID)
    return;

  // Not a management record. Use type to determine rejection.
  // Acquire the interface state mutex to access current RequestIdentifiers.
  std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
  auto request_map_iter {request_map_.find(request_id)};
  switch(type)
  {
    case fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST : {
      invalid_record = (request_map_iter != request_map_.end())
      break;
    }
    case fcgi_si::FCGIType::kFCGI_ABORT_REQUEST : {
      invalid_record = (request_map_iter == request_map_.end()
        || request_map_iter->second.get_abort())
      break;
    }
    case fcgi_si::FCGIType::kFCGI_PARAMS : {
      invalid_record = (request_map_iter == request_map_.end()
        || request_map_iter->second.get_FCGI_PARAMS_completion())
      break;
    }
    case fcgi_si::FCGIType::kFCGI_STDIN : {
      invalid_record = (request_map_iter == request_map_.end()
        || request_map_iter->second.get_FCGI_STDIN_completion())
      break;
    }
    case fcgi_si::FCGIType::kFCGI_DATA : {
      invalid_record = request_map_iter == request_map_.end()
        || request_map_iter->second.get_FCGI_DATA_completion())
      break;
    }
    // No other cases should occur. Reject any others.
    default {
      invalid_record = true;
    }
  } // end switch
} // interface_state_mutex_ released here.

inline uint32_t
fcgi_si::FCGIApplicationInterface::RecordStatus::
ExpectedBytes()
{
  return padding_bytes_expected + content_bytes_expected
         + fcgi_si::FCGI_HEADER_LEN;
}

inline bool
fcgi_si::FCGIApplicationInterface::RecordStatus::
IsHeaderComplete()
{
  return bytes_received >= fcgi_si::FCGI_HEADER_LEN;
}

inline bool
fcgi_si::FCGIApplicationInterface::RecordStatus::
IsRecordComplete()
{
  return ExpectedBytes() == bytes_received;
}
