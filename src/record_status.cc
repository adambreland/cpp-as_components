#include <cstdint>         // For uint32_t.

#include <mutex>
#include <vector>

#include "external/error_handling/include/error_handling.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/record_status.h"
#include "include/request_identifier.h"

fcgi_si::RecordStatus::
RecordStatus(FCGIServerInterface* interface_ptr)
: header_ {}, bytes_received_ {0}, content_bytes_expected_ {0},
  padding_bytes_expected_ {0}, type_ {static_cast<fcgi_si::FCGIType>(0)},
  request_id_ {}, invalid_record_ {false}, i_ptr_ {interface_ptr}
{}

fcgi_si::RecordStatus::
RecordStatus(RecordStatus&& record_status)
: bytes_received_ {record_status.bytes_received_},
  content_bytes_expected_ {record_status.content_bytes_expected_},
  padding_bytes_expected_ {record_status.padding_bytes_expected_},
  type_ {record_status.type_},
  request_id_ {record_status.request_id_},
  invalid_record_ {record_status.invalid_record_},
  local_record_content_buffer_ {std::move(
    record_status.local_record_content_buffer_)},
  i_ptr_ {record_status.i_ptr_}
{
  std::memcpy(header_, record_status.header_, FCGI_HEADER_LEN);
}

fcgi_si::RecordStatus& fcgi_si::RecordStatus::
operator=(RecordStatus&& record_status)
{
  if(this != &record_status)
  {
    std::memcpy(header_, record_status.header_, FCGI_HEADER_LEN);

    bytes_received_ = record_status.bytes_received_;
    content_bytes_expected_ = record_status.content_bytes_expected_;
    padding_bytes_expected_ = record_status.padding_bytes_expected_;
    type_ = record_status.type_;
    request_id_ = record_status.request_id_;
    invalid_record_ = record_status.invalid_record_;
    local_record_content_buffer_ = std::move(
      record_status.local_record_content_buffer_);
    i_ptr_ = record_status.i_ptr_;
  }
  return *this;
}

std::vector<fcgi_si::RequestIdentifier>
fcgi_si::RecordStatus::Read(int connection)
{
  // Number of bytes read at a time from connected sockets.
  constexpr int kBufferSize {512};
  uint8_t read_buffer[kBufferSize];

  // Return value to be modified during processing.
  std::vector<RequestIdentifier> request_identifiers {};

  // Read from the connection until it would block (no more data),
  // it is found to be disconnected, or an unrecoverable error occurs.
  while(true)
  {
    // Read from socket.
    int number_bytes_processed = 0;
    int number_bytes_received =
      socket_functions::SocketRead(connection, read_buffer, kBufferSize);

    // Check for a disconnected socket or an unrecoverable error.
    if(number_bytes_received < kBufferSize)
    {
      if(errno == 0)
      {
        // Connection was closed. Discard any read data and update interface
        // state.
        i_ptr_->connections_to_close_set_.insert(connection);
        return {};
      }

      if((errno != EAGAIN) && (errno != EWOULDBLOCK)) // Unrecoverable error.
        throw std::runtime_error
          {ERRNO_ERROR_STRING("read from a call to NonblockingSocketRead")};
    }

    // Process bytes received (if any). This check is needed as blocking
    // errors may return zero bytes if nothing was read.
    if(number_bytes_received > 0)
    {
      while(number_bytes_processed < number_bytes_received)
      {
        int number_bytes_remaining = number_bytes_received
          - number_bytes_processed;

        // Process received bytes according to header and content/padding
        // completion. Record completion is checked after header addition.

        // Is the header complete?
        if(!IsHeaderComplete())
        {
          uint32_t remaining_header
            {fcgi_si::FCGI_HEADER_LEN - bytes_received_};
          if(remaining_header <= number_bytes_remaining)
          {
            for(uint32_t i {bytes_received_};
                i < fcgi_si::FCGI_HEADER_LEN; i++)
            {
              header_[i] = read_buffer[number_bytes_processed];
              number_bytes_processed++;
            }
            // Follow usage discipline for RecordStatus as the header was
            // modified.
            bytes_received_ += remaining_header;

            // Update record_status as header has been completed.
            // Part of this update is conditionally setting the rejected flag.
            UpdateAfterHeaderCompletion(connection);
          }
          else // Need more than we have to complete header.
          {
            for(uint32_t i {bytes_received_}; number_bytes_remaining > 0;
                (i++, number_bytes_remaining--))
            {
              header_[i] = read_buffer[number_bytes_processed];
              number_bytes_processed++;
            }
            // Follow usage discipline for RecordStatus as the header was
            // modified.
            bytes_received_ += remaining_header;
          }
        }
        // Header is complete, but the record is not.
        // Either the content is complete or it isn't.
        else
        {
          int32_t content_byte_difference {static_cast<int32_t>(fcgi_si::FCGI_HEADER_LEN
            + content_bytes_expected_ - bytes_received_)};
          int32_t remaining_content {(content_byte_difference > 0) ?
            content_byte_difference : 0};
          int32_t number_to_write {};

          if(remaining_content > 0) // Content incomplete.
          {
            number_to_write = (remaining_content <= number_bytes_remaining) ?
              remaining_content : number_bytes_remaining;
            // Determine what we should do with the bytes based on rejection
            // and type. Every record is rejected if it is not one of the
            // six types below. Accordingly, we only need to check for those
            // types.
            if(!invalid_record_)
            {
              if(request_id_.FCGI_id() == 0
                 || type_ == fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST
                 || type_ == fcgi_si::FCGIType::kFCGI_ABORT_REQUEST)
              // Append to local buffer.
              {
                for(int i {0}; i < number_to_write; i++)
                {
                  local_record_content_buffer_.
                    push_back(*(&read_buffer[number_bytes_processed]+i));
                }
              }
              else // Append to non-local buffer.
              {
                // ACQUIRE interface_state_mutex_ to locate append location.
                // The key request_id_ must be present as the record is valid
                // and it is not a begin request record.
                std::lock_guard<std::mutex> interface_state_lock
                  {i_ptr_->interface_state_mutex_};
                auto request_map_iter =
                  i_ptr_->request_map_.find(request_id_);
                switch(type_) {
                  case fcgi_si::FCGIType::kFCGI_PARAMS : {
                    request_map_iter->second.AppendToPARAMS(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case fcgi_si::FCGIType::kFCGI_STDIN : {
                    request_map_iter->second.AppendToSTDIN(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case fcgi_si::FCGIType::kFCGI_DATA : {
                    request_map_iter->second.AppendToDATA(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  default : {
                    // TODO add run-time error action as no other types should
                    // be present.
                  }
                }
              } // RELEASE interface_state_mutex_.
              // Follow discipline for local while
              number_bytes_processed += number_to_write;
              bytes_received_ += number_to_write;
            }
          }
          else // Padding incomplete.
          {
            int32_t remaining_padding {content_byte_difference
              + padding_bytes_expected_};
            number_to_write = (remaining_padding <= number_bytes_remaining) ?
              remaining_padding : number_bytes_remaining;
            // Ignore padding. Skip ahead without processing.
            bytes_received_ += number_to_write;
            number_bytes_processed += number_to_write;
          }
        }
        // Potentially completed a record.
        if(IsRecordComplete())
        {
          fcgi_si::RequestIdentifier request_id
            {i_ptr_->ProcessCompleteRecord(connection, this)};
          *this = RecordStatus {i_ptr_}; // (Clear the RecordStatus object.)
          if(request_id != RequestIdentifier {})
            request_identifiers.push_back(request_id);
        }
        // Loop to check if more received bytes need to be processed.
      } // On exit, looped through all received data as partitioned by record
        // segments.
    }
    // Check if an additional read should be made on the socket. A short count
    // can only mean that a call to read() blocked as EOF and other errors
    // were handled above.
    if(number_bytes_received < kBufferSize)
      break;
  } // On exit, end while loop which keeps reading from the socket.

  return request_identifiers;
}

void fcgi_si::RecordStatus::UpdateAfterHeaderCompletion(int connection)
{
  // Extract number of content bytes from two bytes.
  content_bytes_expected_ = header_[fcgi_si::kHeaderContentLengthB1Index];
  content_bytes_expected_ <<= 8; // one byte
  content_bytes_expected_ += header_[fcgi_si::kHeaderContentLengthB0Index];

  // Extract number of padding bytes.
  padding_bytes_expected_ = header_[fcgi_si::kHeaderPaddingLengthIndex];

  // Extract type and request_id.
  type_ = static_cast<fcgi_si::FCGIType>(header_[fcgi_si::kHeaderTypeIndex]);
  uint16_t FCGI_request_id = header_[fcgi_si::kHeaderRequestIDB1Index];
  FCGI_request_id <<= 8; // one byte
  FCGI_request_id += header_[fcgi_si::kHeaderRequestIDB0Index];
  request_id_ = fcgi_si::RequestIdentifier(connection, FCGI_request_id);

  // Determine if the record should be rejected based on header
  // information.

  // Every management record is accepted.
  if(FCGI_request_id == fcgi_si::FCGI_NULL_REQUEST_ID)
    return;

  // Not a management record. Use type to determine rejection.
  // ACQUIRE the interface state mutex to access current RequestIdentifiers.
  std::lock_guard<std::mutex> interface_state_lock
    {i_ptr_->interface_state_mutex_};
  auto request_map_iter = i_ptr_->request_map_.find(request_id_);
  switch(type_)
  {
    case fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST : {
      invalid_record_ = (request_map_iter != i_ptr_->request_map_.end());
      break;
    }
    case fcgi_si::FCGIType::kFCGI_ABORT_REQUEST : {
      invalid_record_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_abort());
      break;
    }
    case fcgi_si::FCGIType::kFCGI_PARAMS : {
      invalid_record_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_PARAMS_completion());
      break;
    }
    case fcgi_si::FCGIType::kFCGI_STDIN : {
      invalid_record_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_STDIN_completion());
      break;
    }
    case fcgi_si::FCGIType::kFCGI_DATA : {
      invalid_record_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_DATA_completion());
      break;
    }
    // No other cases should occur. Reject any others.
    default : {
      invalid_record_ = true;
    }
  }
} // RELEASE interface_state_mutex_.
