#include <cstdint>          // For std::uint32_t.
#include <cstring>          // For std::memcpy

#include <mutex>
#include <vector>

#include "external/error_handling/include/error_handling.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/record_status.h"
#include "include/request_identifier.h"

namespace fcgi_si {

// Implementation note:
// The value zero is used for type_ as no FastCGI record has this value as a
// type. This is appropriate as no record identity has yet been assigned to the
// RecordStatus object.
RecordStatus::
RecordStatus(int connection, FCGIServerInterface* interface_ptr) noexcept
: connection_ {connection},
  i_ptr_ {interface_ptr}
{}

RecordStatus::RecordStatus(RecordStatus&& record_status) noexcept
: connection_ {record_status.connection_},
  bytes_received_ {record_status.bytes_received_},
  content_bytes_expected_ {record_status.content_bytes_expected_},
  padding_bytes_expected_ {record_status.padding_bytes_expected_},
  type_ {record_status.type_},
  request_id_ {record_status.request_id_},
  invalidated_by_header_ {record_status.invalidated_by_header_},
  local_record_content_buffer_ {std::move(
    record_status.local_record_content_buffer_)},
  i_ptr_ {record_status.i_ptr_}
{
  std::memcpy(header_, record_status.header_, FCGI_HEADER_LEN);
}

RecordStatus& RecordStatus::operator=(RecordStatus&& record_status) noexcept
{
  if(this != &record_status)
  {
    std::memcpy(header_, record_status.header_, FCGI_HEADER_LEN);

    connection_ = record_status.connection_;
    bytes_received_ = record_status.bytes_received_;
    content_bytes_expected_ = record_status.content_bytes_expected_;
    padding_bytes_expected_ = record_status.padding_bytes_expected_;
    type_ = record_status.type_;
    request_id_ = record_status.request_id_;
    invalidated_by_header_ = record_status.invalidated_by_header_;
    local_record_content_buffer_ = std::move(
      record_status.local_record_content_buffer_);
    i_ptr_ = record_status.i_ptr_;
  }
  return *this;
}

void RecordStatus::ClearRecord()
{
  // connection_ is unchanged.
  std::memset(header_, 0, FCGI_HEADER_LEN);
  bytes_received_ = 0;
  content_bytes_expected_ = 0;
  padding_bytes_expected_ = 0;
  type_ = static_cast<FCGIType>(0);
  request_id_ = RequestIdentifier {};
  invalidated_by_header_ = false;
  local_record_content_buffer_.clear();
  // i_ptr_ is unchanged.
}

RequestIdentifier RecordStatus::ProcessCompleteRecord()
{
  RequestIdentifier result {};

  // Check if it is a management record (every management record is valid).
  if(request_id_.FCGI_id() == 0)
  {
    if(type_ == FCGIType::kFCGI_GET_VALUES)
      i_ptr_->SendGetValuesResult(
        connection_, 
        local_record_content_buffer_.data(), 
        local_record_content_buffer_.size());
    else // Unknown type.
      i_ptr_->SendFCGIUnknownType(connection_, type_);
  }
  // Check if the record is valid. Ignore record if it is not.
  else if(invalidated_by_header_)
  {}
  else // The record must be a valid application record. Process it.
  {
    switch(type_)
    {
      case FCGIType::kFCGI_BEGIN_REQUEST: {
        // Extract role
        uint16_t role 
          {local_record_content_buffer_[kBeginRequestRoleB1Index]};
        role <<= 8;
        role += local_record_content_buffer_[kBeginRequestRoleB0Index];

        // Determine if the request limit was reached for the connection.
        bool limit_reached {false};
        { // Start lock handling block.
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock
            {FCGIServerInterface::interface_state_mutex_};
          std::map<int, int>::iterator request_count_it
            {i_ptr_->request_count_map_.find(connection_)};
          // Logic error check.
          if(request_count_it == i_ptr_->request_count_map_.end())
          {
            i_ptr_->local_bad_interface_state_detected_ = true;
            throw std::logic_error {"request_count_map_ did not have an "
              "expected socket descriptor."};
          }
          limit_reached = (request_count_it->second
            >= i_ptr_->maximum_request_count_per_connection_);
        } // RELEASE interface_state_mutex_.

        // Reject or accept a new request based on the request limit and the
        // application_set overload flag.
        if(limit_reached)
          (i_ptr_->maximum_request_count_per_connection_ == 1) ?
            i_ptr_->SendFCGIEndRequest(connection_, request_id_,
              FCGI_CANT_MPX_CONN, EXIT_FAILURE) :
            i_ptr_->SendFCGIEndRequest(connection_, request_id_,
              FCGI_OVERLOADED, EXIT_FAILURE);
        else if(i_ptr_->application_overload_)
          i_ptr_->SendFCGIEndRequest(connection_, request_id_,
            FCGI_OVERLOADED, EXIT_FAILURE);
        else // We can accept the request.
        {
          // Extract close_connection value.
          bool close_connection =
            !(local_record_content_buffer_[kBeginRequestFlagsIndex]
              & FCGI_KEEP_CONN);
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock
            {FCGIServerInterface::interface_state_mutex_};
          i_ptr_->AddRequest(request_id_, role, close_connection);
        } // RELEASE interface_state_mutex_.
        break;
      }
      case FCGIType::kFCGI_ABORT_REQUEST: {
        // Has the request already been assigned?
        bool send_end_request {false};
        { // Start lock handling block.
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock
            {FCGIServerInterface::interface_state_mutex_};
          auto request_data_it {i_ptr_->request_map_.find(request_id_)};

          if(request_data_it->second.get_status()
             == RequestStatus::kRequestAssigned)
            request_data_it->second.set_abort();
          else // Not assigned. We can erase the request and update state.
          {
            // Check if we should indicate that a request was made by the
            // client web sever to close the connection.
            if(request_data_it->second.get_close_connection())
              i_ptr_->application_closure_request_set_.insert(connection_);
            i_ptr_->RemoveRequest(request_data_it);
            send_end_request = true;
          }
        } // RELEASE interface_state_mutex_.
        if(send_end_request)
          i_ptr_->SendFCGIEndRequest(connection_, request_id_,
            FCGI_REQUEST_COMPLETE, i_ptr_->app_status_on_abort_);
          // Don't bother checking if the connection was closed by the
          // peer by inspecting the return value of the call to
          // SendFCGIEndRequest() as it would be difficult to act on
          // this information in the middle of the call to read.
        break;
      }
      case FCGIType::kFCGI_PARAMS:
      case FCGIType::kFCGI_STDIN:
      case FCGIType::kFCGI_DATA: {
        bool send_end_request {false};
        // Should we complete the stream?
        if(content_bytes_expected_ == 0)
        {
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock
            {FCGIServerInterface::interface_state_mutex_};
          auto request_data_it {i_ptr_->request_map_.find(request_id_)};

          (type_ == FCGIType::kFCGI_PARAMS) ?
            request_data_it->second.CompletePARAMS() :
          (type_ == FCGIType::kFCGI_STDIN)  ?
            request_data_it->second.CompleteSTDIN() :
            request_data_it->second.CompleteDATA();

          // Check if the request is complete. If it is, validate the
          // FCGI_PARAMS stream. This also puts the RequestData object into a
          // valid state to be used for construction of an FCGIRequest object.
          if(request_data_it->second.IsRequestComplete())
          {
            if(request_data_it->second.ProcessFCGI_PARAMS())
              result = request_id_;
            else // The request has a malformed FCGI_PARAMS stream. Reject.
            {
              // Check if we should indicate that a request was made by the
              // client web sever to close the connection.
              if(request_data_it->second.get_close_connection())
                i_ptr_->application_closure_request_set_.insert(connection_);
              i_ptr_->RemoveRequest(request_data_it);
              send_end_request = true;
            }
          }
        } /* RELEASE interface_state_mutex_. */ /*
        else
          The record had content which was appended to the proper
          stream when the content was received. No action need be taken now. */

        if(send_end_request) // (Because of a malformed FCGI_PARAMS stream.)
          i_ptr_->SendFCGIEndRequest(connection_, request_id_,
            FCGI_REQUEST_COMPLETE, EXIT_FAILURE);
        break;
      }
    }
  }
  return result; // Default (null) RequestIdentifier if not assinged to.
}

std::vector<RequestIdentifier> RecordStatus::Read()
{
  // Number of bytes read at a time from connected sockets.
  constexpr int kBufferSize {512};
  std::uint8_t read_buffer[kBufferSize];

  // Return value to be modified during processing.
  std::vector<RequestIdentifier> request_identifiers {};

  // Read from the connection until it would block (no more data),
  // it is found to be disconnected, or an unrecoverable error occurs.
  while(true)
  {
    // Read from socket.
    std::uint32_t number_bytes_processed = 0;
    std::uint32_t number_bytes_received =
      socket_functions::SocketRead(connection_, read_buffer, kBufferSize);

    // Check for a disconnected socket or an unrecoverable error.
    if(number_bytes_received < kBufferSize)
    {
      if(errno == 0)
      {
        // Connection was closed. Discard any read data and update interface
        // state.
        try
        {
          i_ptr_->connections_to_close_set_.insert(connection_);
          return {};
        }
        catch(...)
        {
          i_ptr_->local_bad_interface_state_detected_ = true;
          throw;
        }
      }
      if((errno != EAGAIN) && (errno != EWOULDBLOCK)) // Unrecoverable error.
      {
        try
        {
          i_ptr_->connections_to_close_set_.insert(connection_);
        }
        catch(...)
        {
          i_ptr_->local_bad_interface_state_detected_ = true;
          throw;
        }
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, ERRNO_ERROR_STRING("read from a call to "
          "NonblockingSocketRead")};
      }
    }

    // Process bytes received, if any. The check is needed as blocking
    // errors may return zero bytes if nothing was read.
    if(number_bytes_received > 0)
      break;
    while(number_bytes_processed < number_bytes_received)
    {
      std::uint32_t number_bytes_remaining =
        number_bytes_received - number_bytes_processed;

      // Process received bytes according to header and content/padding
      // completion. Record completion is checked after header addition.

      // Is the header complete?
      if(!IsHeaderComplete())
      {
        std::uint32_t remaining_header {FCGI_HEADER_LEN - bytes_received_};
        bool header_can_be_completed 
          {remaining_header <= number_bytes_remaining};
        std::uint32_t number_to_write {(header_can_be_completed) ?
          remaining_header : number_bytes_remaining};
        std::memcpy(&header_[bytes_received_], 
            &read_buffer[number_bytes_processed], number_to_write);
        number_bytes_processed += number_to_write;
        // Follow usage discipline for RecordStatus.
        bytes_received_ += number_to_write;
        // Update the RecordStatus object if the header has been completed.
        // Part of this update is conditionally setting the rejected flag.
        if(header_can_be_completed)
        {
          try
          {
            UpdateAfterHeaderCompletion();
          }
          catch(...)
          {
            try
            {
              i_ptr_->connections_to_close_set_.insert(connection_);
            }
            catch(...)
            {
              i_ptr_->local_bad_interface_state_detected_ = true;
              throw;
            }
            throw;
          }
        }
      }
      // Header is complete, but the record may not be.
      // Either the content is complete or it isn't.
      else
      {
        std::uint32_t header_and_content
          {std::uint32_t(FCGI_HEADER_LEN) + content_bytes_expected_};
        std::uint32_t remaining_content {(bytes_received_ < header_and_content) ?
          header_and_content - bytes_received_ : 0};

        if(remaining_content > 0) // Content incomplete.
        {
          std::uint32_t number_to_write
            {(remaining_content <= number_bytes_remaining) ?
              remaining_content : number_bytes_remaining};
          // Determine what we should do with the bytes based on rejection
          // and type. Every record is rejected if it is not one of the
          // six types below. Accordingly, we only need to check for those
          // types.
          if(!invalidated_by_header_)
          {
            if(request_id_.FCGI_id() == 0
                || type_ == FCGIType::kFCGI_BEGIN_REQUEST
                || type_ == FCGIType::kFCGI_ABORT_REQUEST)
            {
              // Append to local buffer.
              try
              {
                local_record_content_buffer_.insert(
                  local_record_content_buffer_.end(), 
                  &read_buffer[number_bytes_processed],
                  &read_buffer[number_bytes_processed] + number_to_write);
                // Defer tracking variable updates to cover all cases.
              }
              catch(...)
              {
                try
                {
                  i_ptr_->connections_to_close_set_.insert(connection_);
                }
                catch(...)
                {
                  i_ptr_->local_bad_interface_state_detected_ = true;
                  throw;
                }
                throw;
              }
            }
            else // Append to non-local buffer.
            {
              try
              {
                // ACQUIRE interface_state_mutex_ to locate append location.
                // The key request_id_ must be present as the record is valid
                // and it is not a begin request record.
                std::lock_guard<std::mutex> interface_state_lock
                  {FCGIServerInterface::interface_state_mutex_};

                std::map<RequestIdentifier, RequestData>::iterator
                  request_map_iter {i_ptr_->request_map_.find(request_id_)};
                if(request_map_iter == i_ptr_->request_map_.end())
                {
                  i_ptr_->local_bad_interface_state_detected_ = true;
                  throw std::logic_error {"request_map_ did not have an "
                    "expected RequestData object."};
                }
                switch(type_) {
                  case FCGIType::kFCGI_PARAMS : {
                    request_map_iter->second.AppendToPARAMS(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case FCGIType::kFCGI_STDIN : {
                    request_map_iter->second.AppendToSTDIN(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case FCGIType::kFCGI_DATA : {
                    request_map_iter->second.AppendToDATA(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  default : {
                    i_ptr_->local_bad_interface_state_detected_ = true;
                    throw std::logic_error {"An invalid type was encountered "
                      "in a call to Read."};
                  }
                }
              } // RELEASE interface_state_mutex_.
              catch(...)
              {
                if(i_ptr_->local_bad_interface_state_detected_ != true)
                {
                  try
                  {
                    i_ptr_->connections_to_close_set_.insert(connection_);
                  }
                  catch(...)
                  {
                    i_ptr_->local_bad_interface_state_detected_ = true;
                    throw;
                  }
                }
                throw;
              }
            } 
          }
          // Whether the record was valid or not and whether the data is added
          // to RecordStatus or not, the tracking variables must be updated.
          number_bytes_processed += number_to_write;
          // Follow usage discipline for RecordStatus.
          bytes_received_ += number_to_write;
        }
        else // Padding incomplete.
        {
          std::uint32_t remaining_padding 
            {(header_and_content + padding_bytes_expected_) - bytes_received_};
          std::uint32_t number_to_write = 
            {(remaining_padding <= number_bytes_remaining) ?
              remaining_padding : number_bytes_remaining};
          // Ignore padding. Skip ahead without processing.
          bytes_received_ += number_to_write;
          number_bytes_processed += number_to_write;
        }
      }
      // Potentially completed a record.
      if(IsRecordComplete())
      {
        try
        {
          RequestIdentifier request_id
            {ProcessCompleteRecord()};
          ClearRecord();
          if(request_id != RequestIdentifier {})
            request_identifiers.push_back(request_id);
        }
        catch(...)
        {
          try
          {
            i_ptr_->connections_to_close_set_.insert(connection_);
          }
          catch(...)
          {
            i_ptr_->local_bad_interface_state_detected_ = true;
            throw;
          }
          throw;
        }
      } // Loop to check if more received bytes need to be processed.
    } // On exit, looped through all received data as partitioned by record
      // segments.

    // Check if an additional read should be made on the socket. A short count
    // can only mean that a call to read() blocked as EOF and other errors
    // were handled above.
    if(number_bytes_received < kBufferSize)
      break;
  } // End the while loop which keeps reading from the socket.

  return request_identifiers;
}

void RecordStatus::UpdateAfterHeaderCompletion()
{
  // Extract number of content bytes from two bytes.
  content_bytes_expected_ = header_[kHeaderContentLengthB1Index];
  content_bytes_expected_ <<= 8; // one byte
  content_bytes_expected_ += header_[kHeaderContentLengthB0Index];

  // Extract number of padding bytes.
  padding_bytes_expected_ = header_[kHeaderPaddingLengthIndex];

  // Extract type and request_id.
  type_ = static_cast<FCGIType>(header_[kHeaderTypeIndex]);
  std::uint16_t FCGI_request_id = header_[kHeaderRequestIDB1Index];
  FCGI_request_id <<= 8; // one byte
  FCGI_request_id += header_[kHeaderRequestIDB0Index];
  request_id_ = RequestIdentifier(connection_, FCGI_request_id);

  // Determine if the record should be rejected based on header
  // information.

  // Every management record is accepted.
  if(FCGI_request_id == FCGI_NULL_REQUEST_ID)
    return;

  // Check if type-specific content length requirements are met. Note that
  // a management record type that does not have the null request id will be
  // rejected. Any record with the null request id won't be subjected to this
  // check.
  switch(type_) {
    case FCGIType::kFCGI_BEGIN_REQUEST : {
      if(content_bytes_expected_ != 8)
        invalidated_by_header_ = true;
      break;
    }
    case FCGIType::kFCGI_ABORT_REQUEST : {
      if(content_bytes_expected_ != 0)
        invalidated_by_header_ = true;
      break;
    }
    // These cases cannot be validated with local information alone.
    // Fall through to the next check which accesses the interface.
    case FCGIType::kFCGI_PARAMS :
    case FCGIType::kFCGI_STDIN :
    case FCGIType::kFCGI_DATA :
      break;
    // No other cases should occur. Reject any others.
    default : {
      invalidated_by_header_ = true;
      break;
    }
  }
  if(invalidated_by_header_)
    return;

  // Perform checks which require access to current interface state.
  // ACQUIRE interface_state_mutex_.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  // Note that it is expected that find may sometimes return the past-the-end
  // iterator.
  std::map<RequestIdentifier, RequestData>::iterator request_map_iter 
    {i_ptr_->request_map_.find(request_id_)};
  switch(type_)
  {
    case FCGIType::kFCGI_BEGIN_REQUEST : {
      invalidated_by_header_ = (request_map_iter != i_ptr_->request_map_.end());
      break;
    }
    case FCGIType::kFCGI_ABORT_REQUEST : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_abort());
      break;
    }
    case FCGIType::kFCGI_PARAMS : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_PARAMS_completion());
      break;
    }
    case FCGIType::kFCGI_STDIN : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_STDIN_completion());
      break;
    }
    case FCGIType::kFCGI_DATA : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_DATA_completion());
      break;
    }
    // No other cases should occur. Reject any others.
    default : {
      invalidated_by_header_ = true;
    }
  }
} // RELEASE interface_state_mutex_.

} // namespace fcgi_si
