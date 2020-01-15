// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint32_t.
// C++ standard library headers.
#include <mutex>

#include "include/data_types.h"
#include "include/fcgi_application_interface.h"
#include "include/fcgi_request.h"

fcgi_si::RecordStatus::
RecordStatus()
: bytes_received {0}, content_bytes_expected {0}, padding_bytes_expected {0},
  type {static_cast<fcgi_si::FCGIType>(0)}, request_id {}, invalid_record {false}
{}

fcgi_si::RecordStatus&
fcgi_si::RecordStatus::
operator=(RecordStatus&& record_status)
{
  if(this != &record_status)
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
    invalid_record = record_status.invalid_record;
    local_record_content_buffer = std::move(record_status.local_record_content_buffer);
  }
  return *this;
}

inline uint32_t
fcgi_si::RecordStatus::
ExpectedBytes()
{
  return padding_bytes_expected + content_bytes_expected
         + fcgi_si::FCGI_HEADER_LEN;
}

inline bool
fcgi_si::RecordStatus::
IsHeaderComplete()
{
  return bytes_received >= fcgi_si::FCGI_HEADER_LEN;
}

inline bool
fcgi_si::RecordStatus::
IsRecordComplete()
{
  return ExpectedBytes() == bytes_received;
}
