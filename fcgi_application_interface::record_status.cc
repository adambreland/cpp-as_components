fcgi_synchronous_interface::FCGIApplicationInterface::RecordStatus::
RecordStatus()
: bytes_received {0}, content_bytes_expected {0}, padding_bytes_expected {0},
  type {0}, request_id {}, invalid_record {false}
{}

RecordStatus& operator=(const RecordStatus& record_status)
{
  for(char i {0}; i < fcgi_synchronous_interface::FCGI_HEADER_LEN; i++)
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

void fcgi_synchronous_interface::FCGIApplicationInterface::RecordStatus::
UpdateAfterHeaderCompletion(int connection)
{
  // Extract number of content bytes from two bytes.
  content_bytes_expected = header[4];
  content_bytes_expected <<= 8; // one byte
  content_bytes_expected += header[5];

  // Extract number of padding bytes.
  padding_bytes_expected = header[6];

  // Extract type and request_id.
  type = header[1];
  uint16_t FCGI_request_id = header[2];
  FCGI_request_id << 8; // one byte
  FCGI_request_id += header[3];
  request_id = fcgi_synchronous_interface::
    RequestIdentifier(connection, FCGI_request_id);

  // Determine if the record should be rejected based on header
  // information.

  // Every management record is accepted.
  if(FCGI_request_id == fcgi_synchronous_interface::FCGI_NULL_REQUEST_ID)
    return;

  // Not a management record. Use type to determine rejection.
  // Acquire the interface state mutex to access current RequestIdentifiers.
  std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
  auto request_map_iter {request_map_.find(request_id)};
  switch(type)
  {
    case fcgi_synchronous_interface::FCGIType::kFCGI_BEGIN_REQUEST : {
      invalid_record = (request_map_iter != request_map_.end())
      break;
    }
    case fcgi_synchronous_interface::FCGIType::kFCGI_ABORT_REQUEST : {
      invalid_record = (request_map_iter == request_map_.end()
        || request_map_iter->second.get_abort())
      break;
    }
    case fcgi_synchronous_interface::FCGIType::kFCGI_PARAMS : {
      invalid_record = (request_map_iter == request_map_.end()
        || request_map_iter->second.get_FCGI_PARAMS_completion())
      break;
    }
    case fcgi_synchronous_interface::FCGIType::kFCGI_STDIN : {
      invalid_record = (request_map_iter == request_map_.end()
        || request_map_iter->second.get_FCGI_STDIN_completion())
      break;
    }
    case fcgi_synchronous_interface::FCGIType::kFCGI_DATA : {
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
fcgi_synchronous_interface::FCGIApplicationInterface::RecordStatus::
ExpectedBytes()
{
  return padding_bytes_expected + content_bytes_expected
         + fcgi_synchronous_interface::FCGI_HEADER_LEN;
}

inline bool
fcgi_synchronous_interface::FCGIApplicationInterface::RecordStatus::
IsHeaderComplete()
{
  return bytes_received >= fcgi_synchronous_interface::FCGI_HEADER_LEN;
}

inline bool
fcgi_synchronous_interface::FCGIApplicationInterface::RecordStatus::
IsRecordComplete()
{
  return ExpectedBytes() == bytes_received;
}
