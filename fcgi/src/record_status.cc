// MIT License
//
// Copyright (c) 2021 Adam J. Breland
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

#include "include/socket_functions.h"

#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_request.h"
#include "include/fcgi_request_identifier.h"
#include "include/fcgi_server_interface.h"

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

namespace as_components {
namespace fcgi {

FcgiServerInterface::RecordStatus::
RecordStatus(int connection, FcgiServerInterface* interface_ptr)
: connection_ {connection},
  i_ptr_ {interface_ptr}
{}

FcgiServerInterface::RecordStatus::
RecordStatus(RecordStatus&& record_status) noexcept
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

FcgiServerInterface::RecordStatus& FcgiServerInterface::RecordStatus::
operator=(RecordStatus&& record_status) noexcept
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

void FcgiServerInterface::RecordStatus::ClearRecord() noexcept
{
  // connection_ is unchanged.
  std::memset(header_, 0, FCGI_HEADER_LEN);
  bytes_received_ = 0;
  content_bytes_expected_ = 0U;
  padding_bytes_expected_ = 0U;
  type_ = static_cast<FcgiType>(0U);
  request_id_ = FcgiRequestIdentifier {};
  invalidated_by_header_ = false;
  local_record_content_buffer_.clear();
  // i_ptr_ is unchanged.
}

std::map<FcgiRequestIdentifier, FcgiServerInterface::RequestData>::iterator
FcgiServerInterface::RecordStatus::ProcessCompleteRecord(
  std::vector<std::map<FcgiRequestIdentifier, RequestData>::iterator>*
    request_iterators_ptr,
  std::map<FcgiRequestIdentifier, RequestData>::iterator* request_iter_ptr)
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
    std::map<FcgiRequestIdentifier, FcgiServerInterface::RequestData>::iterator
    request_map_end {i_ptr_->request_map_end_};
    // Initialize result to the end value to signify no result.
    std::map<FcgiRequestIdentifier, FcgiServerInterface::RequestData>::iterator
    result {request_map_end};
    // Copy the cached iterator which was passed through request_iter_ptr.
    std::map<FcgiRequestIdentifier, FcgiServerInterface::RequestData>::iterator
    local_request_iter {*request_iter_ptr};

    // Check if it is a management record (every management record is valid).
    if(request_id_.Fcgi_id() == FCGI_NULL_REQUEST_ID)
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
        case FcgiType::kFCGI_BEGIN_REQUEST : {
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
            
            // Update the pointed-to iterator for future calls that concern
            // this request.
            *request_iter_ptr = i_ptr_->AddRequest(request_id_, role,
              close_connection);
          } // RELEASE interface_state_mutex_.
          break;
        }
        case FcgiType::kFCGI_ABORT_REQUEST : {
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
            //    Thus, failure to find the request is not an error, but
            // indicates that the abort can be ignored.
            //    Not checking for request removal would introduce a race 
            // condition between the FcgiRequest object and the interface. If
            // the FcgiRequest object removed the request, the below
            // expression local_request_iter->second.get_status() becomes an
            // access through a non-dereferenceable iterator.
            //
            // Check if the cached iterator can be used.
            if((local_request_iter == request_map_end) ||
               (local_request_iter->first.Fcgi_id() != request_id_.Fcgi_id()))
            {
              local_request_iter = i_ptr_->request_map_.find(request_id_);
            }
            if(local_request_iter == request_map_end)
            {
              break;
            }
            // The value-result parameter request_iter_ptr either should be or
            // must be used to update the pointed-to iterator to one that will
            // be handled correctly and will not be invalid.
            if(local_request_iter == *request_iter_ptr)
            {
              *request_iter_ptr = request_map_end;
            }
            if(local_request_iter->second.get_status() ==
              RequestStatus::kRequestAssigned)
            {
              local_request_iter->second.set_abort();
            }
            else // Not assigned. We can erase the request and update state.
            {
              // Check if we should indicate that a request was made by the
              // client web server to close the connection.
              if(local_request_iter->second.get_close_connection())
              {
                i_ptr_->application_closure_request_set_.insert(connection_);
              }

              // It is possible that the data which completes a request is
              // processed in the same call to ReadRecords that processes the
              // data for an abort request for the request. If the current
              // abort request record is such an abort request, then removing
              // the request will invalidate an interator in
              // *request_iterators_ptr. An invalid iterator must be removed.
              if(local_request_iter->second.get_STDIN_completion() &&
                 local_request_iter->second.get_DATA_completion()  &&
                 local_request_iter->second.get_PARAMS_completion())
              {
                // Not assigned but completed implies "just completed."
                bool found {false};
                for(std::vector<std::map<FcgiRequestIdentifier,
                  RequestData>::iterator>::iterator iter
                  {request_iterators_ptr->begin()};
                  iter != request_iterators_ptr->end();
                  ++iter)
                {
                  if(*iter == local_request_iter)
                  {
                    request_iterators_ptr->erase(iter);
                    found = true;
                    break;
                  }
                }
                if(!found)
                {
                  throw std::logic_error {"A request which was completed "
                    "but not yet assigned was not found in the list of "
                    "iterator instances provided to a call to "
                    "FcgiServerInterface::RecordStatus::ProcessCompleteRecord. "
                    "This list was searched because an abort request was "
                    "received for the request."};
                }
              }
              send_end_request = true;
              i_ptr_->RemoveRequest(local_request_iter);
            }
          } // RELEASE interface_state_mutex_.

          // If send_end_request == true, the request is not assigned and
          // cannot be removed from request_map_ by an FcgiRequest object.
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
        case FcgiType::kFCGI_PARAMS : [[fallthrough]];
        case FcgiType::kFCGI_STDIN  : [[fallthrough]];
        case FcgiType::kFCGI_DATA   : {
          bool send_end_request {false};
          // Should we complete the stream?
          if(content_bytes_expected_ == 0U)
          {
            // Note that, since the request has not been assigned (as a stream
            // record was valid), no other thread can access the
            // RequestData object of the request. A pointer to the RequestData
            // object will be used to access and mutate the object to allow
            // interface_state_mutex_ to be released.

            // ACQUIRE interface_state_mutex_.
            std::unique_lock<std::mutex> unique_interface_state_lock
              {FcgiServerInterface::interface_state_mutex_};
            InterfaceCheck();
            // Check if the cached iterator can be used.
            if((local_request_iter == request_map_end) ||
               (local_request_iter->first.Fcgi_id() != request_id_.Fcgi_id()))
            {
              local_request_iter = i_ptr_->request_map_.find(request_id_);
            }
            if(local_request_iter == request_map_end)
            {
              throw std::logic_error {"An expected request was not found in "
                "request_map_ in a call to "
                "FcgiServerInterface::RecordStatus::ProcessCompleteRecord."};
            }
            RequestData* request_data_ptr {&local_request_iter->second};
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
              //    In the case that the request is complete and well-formed,
              // it is expected that no more records will be received for it.
              // As such, if the external cached iterator pointed to this
              // request, then it should be reset.
              //    In the case that the request is complete but malformed,
              // the external cached iterator must be set to a value that will
              // not be invalid when ProcessCompleteRecord returns.
              if(local_request_iter == *request_iter_ptr)
              {
                *request_iter_ptr = request_map_end;
              }

              if(request_data_ptr->ProcessFCGI_PARAMS())
              {
                result = local_request_iter;
              }
              else // The request has a malformed FCGI_PARAMS stream. Reject.
              {
                // ACQUIRE interface_state_mutex_.
                unique_interface_state_lock.lock();
                InterfaceCheck();
                // Check if we should indicate that a request was made by the
                // client web sever to close the connection.
                if(request_data_ptr->get_close_connection())
                {
                  i_ptr_->application_closure_request_set_.insert(connection_);
                }
                send_end_request = true;
                i_ptr_->RemoveRequest(local_request_iter);
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
    return result; // Default (null) FcgiRequestIdentifier if not assinged to.
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
std::vector<std::map<FcgiRequestIdentifier,
  FcgiServerInterface::RequestData>::iterator>
FcgiServerInterface::RecordStatus::ReadRecords()
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
  std::vector<std::map<FcgiRequestIdentifier, RequestData>::iterator>
    request_iterators {};

  const std::map<FcgiRequestIdentifier, RequestData>::iterator request_map_end
    {i_ptr_->request_map_end_};
  // An iterator over request_map_ which serves as a cache to the most recently
  // accessed RequestData item or to a safe null value (end).
  std::map<FcgiRequestIdentifier, RequestData>::iterator local_request_iter
    {request_map_end};

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
    std::int_fast32_t number_bytes_received(as_components::socket_functions::
      SocketRead(connection_, read_buffer, kBufferSize));

    // Check for a disconnected socket or an unrecoverable error.
    if(number_bytes_received < kBufferSize)
    {
      if((errno == EAGAIN) || (errno == EWOULDBLOCK))
      {
        // no-op here. The read buffer was emptied. Proceed to process any read
        // data.
      }
      else
      {
        // An error other than blocking due to an empty read buffer was
        // encountered.
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
          // Due to the error, schedule the local descriptor of the connection
          // for closure.
          i_ptr_->application_closure_request_set_.insert(connection_);
        }
        catch(...)
        {
          i_ptr_->bad_interface_state_detected_ = true;
          throw;
        }
        if((errno == 0) || (errno == ECONNRESET))
        {
          // Connection was closed. Discard any read data and update interface
          // state. The case errno == ECONNRESET implies that data was sent to
          // the peer and that the peer closed the connection before it read
          // the sent data.
          return {};
        }
        else
        {
          // All other cases are treated as unknown unrecoverable errors.
          std::error_code ec {errno, std::system_category()};
          throw std::system_error {ec, "read from a call to "
            "SocketRead"};
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
            UpdateAfterHeaderCompletion(&local_request_iter);
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
            if(request_id_.Fcgi_id() == FCGI_NULL_REQUEST_ID
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
              // ACQUIRE interface_state_mutex_.
              std::unique_lock<std::mutex> unique_interface_state_lock
                {FcgiServerInterface::interface_state_mutex_,
                  std::defer_lock};
              try
              {
                unique_interface_state_lock.lock();
              }
              catch(...)
              {
                std::terminate();
              }
              InterfaceCheck();
              try
              {
                // Check if the cached iterator is the correct iterator.
                if((local_request_iter == request_map_end) ||
                   (local_request_iter->first.Fcgi_id() !=
                    request_id_.Fcgi_id()))
                {
                  local_request_iter = i_ptr_->request_map_.find(request_id_);
                  if(local_request_iter == i_ptr_->request_map_.end())
                  {
                    i_ptr_->bad_interface_state_detected_ = true;
                    throw std::logic_error {"request_map_ did not have an "
                      "expected RequestData object."};
                  }
                }
                switch(type_) {
                  case FcgiType::kFCGI_PARAMS : {
                    local_request_iter->second.AppendToPARAMS(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case FcgiType::kFCGI_STDIN : {
                    local_request_iter->second.AppendToSTDIN(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case FcgiType::kFCGI_DATA : {
                    local_request_iter->second.AppendToDATA(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  default : {
                    i_ptr_->bad_interface_state_detected_ = true;
                    // An invalid type should have been rejected upon header
                    // completion. The presence of an invalid type here then
                    // indicates a logic error in the program.
                    throw std::logic_error {"An invalid type was encountered "
                      "in a call to FcgiServerInterface::ReadRecords."};
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
          std::map<FcgiRequestIdentifier, RequestData>::iterator result_iter
            {ProcessCompleteRecord(&request_iterators, &local_request_iter)};
          ClearRecord();
          if(result_iter != i_ptr_->request_map_end_)
            request_iterators.push_back(result_iter);
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
      } // Loop to check if more received bytes need to be processed.
    } // On exit, looped through all received data as partitioned by record
      // segments.

    // Check if an additional read should be made on the socket. A short count
    // can only mean that a call to read() blocked as EOF and other errors
    // were handled above.
    if(number_bytes_received < kBufferSize)
      break;
  } // End the while loop which keeps reading from the socket.

  return request_iterators;
}

void FcgiServerInterface::RecordStatus::UpdateAfterHeaderCompletion(
  std::map<FcgiRequestIdentifier, RequestData>::iterator* request_iter_ptr)
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
  request_id_ = FcgiRequestIdentifier(connection_, Fcgi_request_id);

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
    // Fall through to the next check which accesses the interface.
    case FcgiType::kFCGI_PARAMS : [[fallthrough]];
    case FcgiType::kFCGI_STDIN  : [[fallthrough]];
    case FcgiType::kFCGI_DATA   :
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
      "in a call to "
      "FcgiServerInterface::RecordStatus::UpdateAfterHeaderCompletion."};
  
  std::map<FcgiRequestIdentifier, RequestData>::iterator request_map_end
    {i_ptr_->request_map_end_};
  // Note that it is expected that find may sometimes return the past-the-end
  // iterator.
  std::map<FcgiRequestIdentifier, RequestData>::iterator request_map_iter 
    {*request_iter_ptr};
  if((request_map_iter == request_map_end) ||
     (request_map_iter->first.Fcgi_id() != request_id_.Fcgi_id()))
  {
    request_map_iter = i_ptr_->request_map_.find(request_id_);
    if(request_map_iter != request_map_end)
    {
      *request_iter_ptr = request_map_iter;
    }
  }

  switch(type_)
  {
    case FcgiType::kFCGI_BEGIN_REQUEST : {
      invalidated_by_header_ = (request_map_iter != request_map_end);
      break;
    }
    case FcgiType::kFCGI_ABORT_REQUEST : {
      invalidated_by_header_ = ((request_map_iter == request_map_end)
        || (request_map_iter->second.get_abort()));
      break;
    }
    case FcgiType::kFCGI_PARAMS : {
      invalidated_by_header_ = ((request_map_iter == request_map_end)
        || (request_map_iter->second.get_PARAMS_completion()));
      break;
    }
    case FcgiType::kFCGI_STDIN : {
      invalidated_by_header_ = ((request_map_iter == request_map_end)
        || (request_map_iter->second.get_STDIN_completion()));
      break;
    }
    case FcgiType::kFCGI_DATA : {
      invalidated_by_header_ = ((request_map_iter == request_map_end)
        || (request_map_iter->second.get_DATA_completion()));
      break;
    }
    // No other cases should occur. Reject any others.
    default : {
      invalidated_by_header_ = true;
    }
  }
} // RELEASE interface_state_mutex_.

} // namespace fcgi
} // namespace as_components
