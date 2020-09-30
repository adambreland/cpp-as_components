#include "include/record_status.h"

#include <cstdint>          // For std::uint32_t.
#include <cstring>          // For std::memcpy
#include <limits>
#include <mutex>
#include <vector>

#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_identifier.h"

// Class implementation notes:
// 1) Discipline for accessing shared state:
//    a) Whenever FcgiServerInterface::interface_state_mutex_ must be
//       acquired to read a shared value, the bad_interface_state_detected_
//       flag must be checked. If the flag is set, the action should be halted
//       by throwing a std::runtime_error object.
//    b) Since a RecordStatus object is used as an internal component of a
//       FcgiServerInterface object, the mutex acquisition and release patterns
//       that apply to FcgiServerInterface objects apply to RecordStatus
//       objects.
// 2) Obligations depended upon by other classes:
//    a)    Since RecordStatus is a friend of FcgiServerInterface and 
//       RequestData objects are components of FcgiServerInterface objects, 
//       RecordStatus methods may manipulate RequestData objects through the
//       public interface of RequestData. 
//          When a RecordStatus object is associated with a RequestData object
//       of the request_map_ private data member of FcgiServerInterface,
//       ProcessFCGI_PARAMS must be called on the RequestData object upon
//       completion of the request associated with the RecordStatus object.
//       This method puts the RequestData object into a state which can be
//       used by the FcgiRequest constructor to generate a request from the
//       data of the request.
namespace fcgi_si {

RecordStatus::
RecordStatus(int connection, FcgiServerInterface* interface_ptr)
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

void RecordStatus::ClearRecord() noexcept
{
  // connection_ is unchanged.
  std::memset(header_, 0, FCGI_HEADER_LEN);
  bytes_received_ = 0;
  content_bytes_expected_ = 0U;
  padding_bytes_expected_ = 0U;
  type_ = static_cast<FcgiType>(0U);
  request_id_ = RequestIdentifier {};
  invalidated_by_header_ = false;
  local_record_content_buffer_.clear();
  // i_ptr_ is unchanged.
}

RequestIdentifier RecordStatus::ProcessCompleteRecord()
{
  auto InterfaceCheck = [this]()->void
  {
    if(i_ptr_->bad_interface_state_detected_)
      throw std::runtime_error {"The interface was found to be "
        "corrupt in a call to "
        "fcgi_si::RecordStatus::ProcessCompleteRecord."};
  };

  try
  {
    // Null request identifier.
    RequestIdentifier result {};

    // Check if it is a management record (every management record is valid).
    if(request_id_.Fcgi_id() == 0U)
    {
      if(type_ == FcgiType::kFCGI_GET_VALUES)
      {
        // The maximum length of local_record_content_buffer_.size() is limited
        // to the maximum value of the content of a FastCGI record. As such,
        // it will not be too large to be stored in a variable of type
        // std::int_fast32_t.
        i_ptr_->SendGetValuesResult(connection_, 
          local_record_content_buffer_.data(), 
          local_record_content_buffer_.size());
      }
      else // Unknown type.
        i_ptr_->SendFcgiUnknownType(connection_, type_);
    }
    // Check if the record is valid. Ignore record if it is not.
    else if(invalidated_by_header_)
    {
      /*no-op*/
    }
    else // The record must be a valid application record. Process it.
    {
      switch(type_)
      {
        case FcgiType::kFCGI_BEGIN_REQUEST: {
          // Extract role
          std::uint16_t role 
            {local_record_content_buffer_[kBeginRequestRoleB1Index]};
          role <<= 8;
          role += local_record_content_buffer_[kBeginRequestRoleB0Index];

          // Determine if the request limit was reached for the connection.
          bool limit_reached {false};
          { 
            // Start lock handling block.
            // ACQUIRE interface_state_mutex_.
            std::lock_guard<std::mutex> interface_state_lock
              {FcgiServerInterface::interface_state_mutex_};
            InterfaceCheck();

            std::map<int, int>::iterator request_count_it
              {i_ptr_->request_count_map_.find(connection_)};
            // Logic error check.
            if(request_count_it == i_ptr_->request_count_map_.end())
            {
              i_ptr_->bad_interface_state_detected_ = true;
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
              i_ptr_->SendFcgiEndRequest(connection_, request_id_,
                FCGI_CANT_MPX_CONN, EXIT_FAILURE) :
              i_ptr_->SendFcgiEndRequest(connection_, request_id_,
                FCGI_OVERLOADED, EXIT_FAILURE);
          else if(i_ptr_->application_overload_)
            i_ptr_->SendFcgiEndRequest(connection_, request_id_,
              FCGI_OVERLOADED, EXIT_FAILURE);
          else // We can accept the request.
          {
            // Extract close_connection value.
            bool close_connection =
              !(local_record_content_buffer_[kBeginRequestFlagsIndex]
                & FCGI_KEEP_CONN);
            // ACQUIRE interface_state_mutex_.
            std::lock_guard<std::mutex> interface_state_lock
              {FcgiServerInterface::interface_state_mutex_};
            InterfaceCheck();
            
            i_ptr_->AddRequest(request_id_, role, close_connection);
          } // RELEASE interface_state_mutex_.
          break;
        }
        case FcgiType::kFCGI_ABORT_REQUEST: {
          // Has the request already been assigned?
          bool send_end_request {false};
          { 
            // Start lock handling block.
            // ACQUIRE interface_state_mutex_.
            std::lock_guard<std::mutex> interface_state_lock
              {FcgiServerInterface::interface_state_mutex_};
            InterfaceCheck();
            
            //    Between header validation for the abort record and now, the
            // request may have been removed from request_map_ by the
            // FcgiRequest object for the request for several reasons.
            //    Thus, failure to find the request is not an error, but indicates
            // that the abort can be ignored.
            //    Not checking for request removal would introduce a race 
            // condition between the FcgiRequest object and the interface. If the 
            // FcgiRequest object removed the request, the below expression
            // request_data_it->second.get_status() becomes an access through a 
            // non-dereferenceable iterator.
            auto request_data_it {i_ptr_->request_map_.find(request_id_)};
            if(request_data_it == i_ptr_->request_map_.end())
              break;

            if(request_data_it->second.get_status() == 
              RequestStatus::kRequestAssigned)
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

          // If send_end_request == true, the request is not assigned and cannot
          // be removed from request_map_ by an FcgiRequest object.
          if(send_end_request)
            i_ptr_->SendFcgiEndRequest(connection_, request_id_,
              FCGI_REQUEST_COMPLETE, i_ptr_->app_status_on_abort_);
            // Don't bother checking if the connection was closed by the
            // peer by inspecting the return value of the call to
            // SendFcgiEndRequest() as it would be difficult to act on
            // this information in the middle of the call to read.
          break;
        }
        // Processing for these types can be performed in one block.
        // Fall through if needed.
        case FcgiType::kFCGI_PARAMS:
        case FcgiType::kFCGI_STDIN:
        case FcgiType::kFCGI_DATA: {
          bool send_end_request {false};

          // Should we complete the stream?
          if(content_bytes_expected_ == 0U)
          {
            // Access interface state to find the RequestData object
            // associated with the current request. Note that, since the
            // request has not been assigned (as a stream record was valid),
            // no other thread can access the found RequestData object.
            // A pointer to the RequestData object will be used to access and
            // mutate the object to allow interface_state_mutex_ to be released.

            // ACQUIRE interface_state_mutex_.
            std::unique_lock<std::mutex> unique_interface_state_lock
              {FcgiServerInterface::interface_state_mutex_};
            InterfaceCheck();

            std::map<RequestIdentifier, RequestData>::iterator request_data_it 
              {i_ptr_->request_map_.find(request_id_)};

            if(request_data_it == i_ptr_->request_map_.end())
              throw std::logic_error {"An expected request was not found in "
                "request_map_ in a call to "
                "fcgi_si::RecordStatus::ProcessCompleteRecord."};
            
            RequestData* request_data_ptr {&request_data_it->second};
            
            // RELEASE interface_state_mutex_.
            unique_interface_state_lock.unlock();

            (type_ == FcgiType::kFCGI_PARAMS) ?
              request_data_ptr->CompletePARAMS() :
            (type_ == FcgiType::kFCGI_STDIN)  ?
              request_data_ptr->CompleteSTDIN()  :
              request_data_ptr->CompleteDATA();

            // Check if the request is complete. If it is, validate the
            // FCGI_PARAMS stream. This also puts the RequestData object into a
            // valid state to be used for construction of an FcgiRequest object.
            if(request_data_ptr->CheckRequestCompletionWithConditionalUpdate())
            {
              if(request_data_ptr->ProcessFCGI_PARAMS())
                result = request_id_;
              else // The request has a malformed FCGI_PARAMS stream. Reject.
              {
                // ACQUIRE interface_state_mutex_.
                unique_interface_state_lock.lock();
                InterfaceCheck();

                // Check if we should indicate that a request was made by the
                // client web sever to close the connection.
                if(request_data_ptr->get_close_connection())
                  i_ptr_->application_closure_request_set_.insert(connection_);

                i_ptr_->RemoveRequest(request_data_it);
                send_end_request = true;
              }
            }
          } /* Conditionally RELEASE interface_state_mutex_. */ /*
          else
            The record had content which was appended to the proper
            stream when the content was received. No action need be taken now. */

          if(send_end_request) // (Because of a malformed FCGI_PARAMS stream.)
            i_ptr_->SendFcgiEndRequest(connection_, request_id_,
              FCGI_REQUEST_COMPLETE, EXIT_FAILURE);
          break;
        }
        default :
          throw std::logic_error {"An unexpected record type was encountered "
            "in a call to fcgi_si::RecordStatus::ProcessCompleteRecord."};
      }
    }
    return result; // Default (null) RequestIdentifier if not assinged to.
  }
  catch(...)
  {
    std::unique_lock<std::mutex> unique_interface_state_lock
      {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
    try
    {
      // ACQUIRE inteface_state_mutex_.
     unique_interface_state_lock.lock();
    }
    catch(...)
    {
      std::terminate();
    }
    InterfaceCheck();
    try
    {
      i_ptr_->application_closure_request_set_.insert(connection_);
    }
    catch(...)
    {
      i_ptr_->bad_interface_state_detected_ = true;
      throw;
    }

    throw;
  } // RELEASE interface_state_mutex_.
}

// Implementation specification:
//
// Synchronization:
// 1) May acquire and release interface_state_mutex_.
// 2) May implicitly acquire and release the write mutex associated with
//    the connection of the RecordStatus object.
std::vector<RequestIdentifier> RecordStatus::ReadRecords()
{
  auto InterfaceCheck = [this]()->void
  {
    if(i_ptr_->bad_interface_state_detected_)
      throw std::runtime_error {"The interface was found to be "
        "corrupt in a call to "
        "fcgi_si::RecordStatus::ReadRecords."};
  };

  // Number of bytes read at a time from connected sockets.
  constexpr int kBufferSize {512};
  std::uint8_t read_buffer[kBufferSize];

  // Return value to be modified during processing.
  std::vector<RequestIdentifier> request_identifiers {};

  // Read from the connection until it would block (no more data),
  // it is found to be disconnected, or an unrecoverable error occurs.
  while(true)
  {
    std::int_fast32_t number_bytes_processed {0};

    // A safe narrowing conversion as the return value is in the range
    // [0, kBufferSize] and kBufferSize is fairly small.
    //
    // Note that reading does not require synchronization as only the
    // interface reads from the connected sockets.
    std::int_fast32_t number_bytes_received(socket_functions::SocketRead(
      connection_, read_buffer, kBufferSize));

    // Check for a disconnected socket or an unrecoverable error.
    if(number_bytes_received < kBufferSize)
    {
      if((errno == 0) || ((errno != EAGAIN) && (errno != EWOULDBLOCK)))
      {
        std::unique_lock<std::mutex> unique_interface_state_lock
          {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
        try
        {
          // ACQUIRE interface_state_mutex_.
          unique_interface_state_lock.lock();
        }
        catch(...)
        {
          std::terminate();
        }
        InterfaceCheck();
        try
        {
          i_ptr_->application_closure_request_set_.insert(connection_);
        }
        catch(...)
        {
          i_ptr_->bad_interface_state_detected_ = true;
          throw;
        }
        if(errno == 0)
        {
          // Connection was closed. Discard any read data and update interface
          // state.
          return {};
        }
        else // Unrecoverable error.
        {
          std::error_code ec {errno, std::system_category()};
          throw std::system_error {ec, "read from a call to "
            "NonblockingSocketRead"};
        }
      } // RELEASE interface_state_mutex_.
    }
    // Processed received bytes.
    while(number_bytes_processed < number_bytes_received)
    {
      std::int_fast32_t number_bytes_remaining =
        number_bytes_received - number_bytes_processed;

      // Process received bytes according to header and content/padding
      // completion. Record completion is checked after header addition.

      if(!IsHeaderComplete())
      {
        std::int_fast32_t remaining_header {FCGI_HEADER_LEN - bytes_received_};
        bool header_can_be_completed 
          {remaining_header <= number_bytes_remaining};
        std::int_fast32_t number_to_write {(header_can_be_completed) ?
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
            std::unique_lock<std::mutex> unique_interface_state_lock
              {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
            try
            {
              // ACQUIRE interface_state_mutex_.
              unique_interface_state_lock.lock();
            }
            catch(...)
            {
              std::terminate();
            }
            InterfaceCheck();
            try
            {
              i_ptr_->application_closure_request_set_.insert(connection_);
            }
            catch(...)
            {
              i_ptr_->bad_interface_state_detected_ = true;
              throw;
            }

            throw;
          } // RELEASE interface_state_mutex_.
        }
      }
      // Header is complete, but the record may not be.
      // Either the content is complete or it isn't.
      else
      {
        std::int_fast32_t header_and_content {content_bytes_expected_};
        header_and_content += FCGI_HEADER_LEN;
        std::int_fast32_t remaining_content {};
        if(bytes_received_ < header_and_content)
          remaining_content = header_and_content - bytes_received_;
        else
          remaining_content = 0;

        if(remaining_content > 0) // Content incomplete.
        {
          std::int_fast32_t number_to_write
            {(remaining_content <= number_bytes_remaining) ?
              remaining_content : number_bytes_remaining};
          // Determine what we should do with the bytes based on rejection
          // and type. Every record is rejected if it is not one of the
          // six types below. Accordingly, we only need to check for those
          // types.
          if(!invalidated_by_header_)
          {
            if(request_id_.Fcgi_id() == 0U
                || type_ == FcgiType::kFCGI_BEGIN_REQUEST
                || type_ == FcgiType::kFCGI_ABORT_REQUEST)
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
                std::unique_lock<std::mutex> unique_interface_state_lock
                  {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
                try
                {
                  // ACQUIRE interface_state_mutex_.
                  unique_interface_state_lock.lock();
                }
                catch(...)
                {
                  std::terminate();
                }
                InterfaceCheck();
                try
                {
                  i_ptr_->application_closure_request_set_.insert(connection_);
                }
                catch(...)
                {
                  i_ptr_->bad_interface_state_detected_ = true;
                  throw;
                }

                throw;
              } // RELEASE interface_state_mutex_.
            }
            else // Append to non-local buffer.
            {
              try
              {
                // ACQUIRE interface_state_mutex_ to locate append location.
                // The key request_id_ must be present as the record is valid
                // and it is not a begin request record.
                std::unique_lock<std::mutex> unique_interface_state_lock
                  {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
                try
                {
                  unique_interface_state_lock.lock();
                }
                catch(...)
                {
                  std::terminate();
                }
                InterfaceCheck();

                std::map<RequestIdentifier, RequestData>::iterator
                  request_map_iter {i_ptr_->request_map_.find(request_id_)};
                if(request_map_iter == i_ptr_->request_map_.end())
                {
                  i_ptr_->bad_interface_state_detected_ = true;
                  throw std::logic_error {"request_map_ did not have an "
                    "expected RequestData object."};
                }
                switch(type_) {
                  case FcgiType::kFCGI_PARAMS : {
                    request_map_iter->second.AppendToPARAMS(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case FcgiType::kFCGI_STDIN : {
                    request_map_iter->second.AppendToSTDIN(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case FcgiType::kFCGI_DATA : {
                    request_map_iter->second.AppendToDATA(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  default : {
                    i_ptr_->bad_interface_state_detected_ = true;
                    throw std::logic_error {"An invalid type was encountered "
                      "in a call to Read."};
                  }
                }
              } 
              catch(...)
              {
                if(i_ptr_->bad_interface_state_detected_ != true)
                {
                  try
                  {
                    i_ptr_->application_closure_request_set_.insert(connection_);
                  }
                  catch(...)
                  {
                    i_ptr_->bad_interface_state_detected_ = true;
                    throw;
                  }
                }
                throw;
              }
            } // RELEASE interface_state_mutex_.
          }
          // Whether the record was valid or not and whether the data is added
          // to RecordStatus or not, the tracking variables must be updated.
          number_bytes_processed += number_to_write;
          // Follow usage discipline for RecordStatus.
          bytes_received_ += number_to_write;
        }
        else // Padding incomplete.
        {
          std::int_fast32_t remaining_padding 
            {(header_and_content + padding_bytes_expected_) - bytes_received_};
          std::int_fast32_t number_to_write = 
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
          std::unique_lock<std::mutex> unique_interface_state_lock
            {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
          try
          {
            // ACQUIRE interface_state_mutex_.
            unique_interface_state_lock.lock();
          }
          catch(...)
          {
            std::terminate();
          }
          InterfaceCheck();
          try
          {
            i_ptr_->application_closure_request_set_.insert(connection_);
          }
          catch(...)
          {
            i_ptr_->bad_interface_state_detected_ = true;
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
  type_ = static_cast<FcgiType>(header_[kHeaderTypeIndex]);
  std::uint16_t Fcgi_request_id = header_[kHeaderRequestIDB1Index];
  Fcgi_request_id <<= 8; // one byte
  Fcgi_request_id += header_[kHeaderRequestIDB0Index];
  request_id_ = RequestIdentifier(connection_, Fcgi_request_id);

  // Determine if the record should be rejected based on header
  // information.

  // Every management record is accepted.
  if(Fcgi_request_id == FCGI_NULL_REQUEST_ID)
    return;

  // Check if type-specific content length requirements are met. Note that
  // a management record type that does not have the null request id will be
  // rejected. Any record with the null request id won't be subjected to this
  // check.
  switch(type_) {
    case FcgiType::kFCGI_BEGIN_REQUEST : {
      if(content_bytes_expected_ != 8)
        invalidated_by_header_ = true;
      break;
    }
    case FcgiType::kFCGI_ABORT_REQUEST : {
      if(content_bytes_expected_ != 0)
        invalidated_by_header_ = true;
      break;
    }
    // These cases cannot be validated with local information alone.
    //
    // Fall through to the next check which accesses the interface.
    case FcgiType::kFCGI_PARAMS :
    case FcgiType::kFCGI_STDIN :
    case FcgiType::kFCGI_DATA :
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
    {FcgiServerInterface::interface_state_mutex_};
  // Before the checks, make sure that the interface is in a good state.
  if(i_ptr_->bad_interface_state_detected_)
    throw std::runtime_error {"The interface was found to be corrupted "
      "in a call to fcgi_si::RecordStatus::UpdateAfterHeaderCompletion."};
  
  // Note that it is expected that find may sometimes return the past-the-end
  // iterator.
  std::map<RequestIdentifier, RequestData>::iterator request_map_iter 
    {i_ptr_->request_map_.find(request_id_)};
  switch(type_)
  {
    case FcgiType::kFCGI_BEGIN_REQUEST : {
      invalidated_by_header_ = (request_map_iter != i_ptr_->request_map_.end());
      break;
    }
    case FcgiType::kFCGI_ABORT_REQUEST : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_abort());
      break;
    }
    case FcgiType::kFCGI_PARAMS : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_PARAMS_completion());
      break;
    }
    case FcgiType::kFCGI_STDIN : {
      invalidated_by_header_ = (request_map_iter == i_ptr_->request_map_.end()
        || request_map_iter->second.get_STDIN_completion());
      break;
    }
    case FcgiType::kFCGI_DATA : {
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
