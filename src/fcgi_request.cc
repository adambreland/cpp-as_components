#include <unistd.h>             // For portable use of select.
#include <sys/select.h>
#include <sys/time.h>           // For portable use of select.
#include <sys/types.h>          // For ssize_t and portable use of select.

#include <cerrno>
#include <cstdint>

#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "external/error_handling/include/error_handling.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_data.h"
#include "include/request_identifier.h"

fcgi_si::FCGIRequest::FCGIRequest(fcgi_si::RequestIdentifier request_id,
  fcgi_si::FCGIServerInterface* interface_ptr,
  fcgi_si::RequestData* request_data_ptr,
  std::mutex* write_mutex_ptr, std::mutex* interface_state_mutex_ptr)
: interface_ptr_             {interface_ptr},
  request_identifier_        {request_id},
  environment_map_           {std::move(request_data_ptr->environment_map_)},
  request_stdin_content_     {std::move(request_data_ptr->FCGI_STDIN_)},
  request_data_content_      {std::move(request_data_ptr->FCGI_DATA_)},
  role_                      {request_data_ptr->role_},
  close_connection_          {request_data_ptr->close_connection_},
  was_aborted_               {false},
  completed_                 {false},
  write_mutex_ptr_           {write_mutex_ptr},
  interface_state_mutex_ptr_ {interface_state_mutex_ptr}
{
  if(interface_ptr || request_data_ptr == nullptr || write_mutex_ptr == nullptr
     || interface_state_mutex_ptr == nullptr)
    throw std::invalid_argument {ERROR_STRING("A pointer with a nullptr value was used to construct an FCGIRequest object.")};

  // Update the status of the RequestData object to reflect its use in the
  // construction of an FCGIRequest which will be exposed to the application.

  // NOTE that this constructor should only be called by an
  // FCGIServerInterface object. It is assumed that synchronization is
  // implicit and that acquiring the shared state mutex is not necessary.
  request_data_ptr->request_status_ = fcgi_si::RequestStatus::kRequestAssigned;
}

fcgi_si::FCGIRequest::FCGIRequest(FCGIRequest&& request)
: interface_ptr_             {request.interface_ptr_},
  request_identifier_        {request.request_identifier_},
  environment_map_           {std::move(request.environment_map_)},
  request_stdin_content_     {std::move(request.request_stdin_content_)},
  request_data_content_      {std::move(request.request_data_content_)},
  role_                      {request.role_},
  close_connection_          {request.close_connection_},
  was_aborted_               {request.was_aborted_},
  completed_                 {request.completed_},
  write_mutex_ptr_           {request.write_mutex_ptr_},
  interface_state_mutex_ptr_ {request.interface_state_mutex_ptr_}
{
  request.interface_ptr_ = nullptr;
  request.request_identifier_ = fcgi_si::RequestIdentifier {};
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  request.close_connection_ = false;
  request.completed_ = true;
  request.write_mutex_ptr_ = nullptr;
  request.interface_state_mutex_ptr_ = nullptr;
}

fcgi_si::FCGIRequest& fcgi_si::FCGIRequest::operator=(FCGIRequest&& request)
{
  interface_ptr_ = request.interface_ptr_;
  request_identifier_ = request.request_identifier_;
  environment_map_ = std::move(request.environment_map_);
  request_stdin_content_ = std::move(request.request_stdin_content_);
  request_data_content_ = std::move(request.request_data_content_);
  role_ = request.role_;
  close_connection_ = request.close_connection_;
  was_aborted_ = request.was_aborted_;
  completed_ = request.completed_;
  write_mutex_ptr_ = request.write_mutex_ptr_;
  interface_state_mutex_ptr_ = request.interface_state_mutex_ptr_;

  request.interface_ptr_ = nullptr;
  request.request_identifier_ = fcgi_si::RequestIdentifier {};
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  // request.role_ is unchanged.
  request.close_connection_ = false;
  request.was_aborted_ = false;
  request.completed_ = true;
  request.write_mutex_ptr_ = nullptr;
  request.interface_state_mutex_ptr_ = nullptr;
}

inline const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
fcgi_si::FCGIRequest::get_environment_map() const
{
  return environment_map_;
}

inline const std::vector<uint8_t>& fcgi_si::FCGIRequest::get_STDIN() const
{
  return request_stdin_content_;
}

inline const std::vector<uint8_t>& fcgi_si::FCGIRequest::get_DATA() const
{
  return request_data_content_;
}

bool fcgi_si::FCGIRequest::get_abort()
{
  if(completed_ || was_aborted_)
    return was_aborted_;

  // ACQUIRE interface_state_mutex_ to determine current abort state.
  std::lock_guard<std::mutex> interface_state_lock {*interface_state_mutex_ptr_};
  auto request_map_iter = interface_ptr_->request_map_.find(request_identifier_);
  // Include check for absence of request to prevent undefined method call.
  // This check should always pass.
  if(request_map_iter != interface_ptr_->request_map_.end())
    if(request_map_iter->second.get_abort())
      return was_aborted_ = true;

  return was_aborted_; // i.e. return false.
} // RELEASE interface_state_mutex_

inline uint16_t fcgi_si::FCGIRequest::get_role() const
{
  return role_;
}

inline bool fcgi_si::FCGIRequest::get_completion_status() const
{
  return completed_;
}

inline bool fcgi_si::FCGIRequest::Write(const std::vector<uint8_t>& ref,
  std::vector<uint8_t>::const_iterator begin_iter,
  std::vector<uint8_t>::const_iterator end_iter)
{
  return true; // pass
}

inline bool fcgi_si::FCGIRequest::WriteError(const std::vector<uint8_t>& ref,
  std::vector<uint8_t>::const_iterator begin_iter,
  std::vector<uint8_t>::const_iterator end_iter)
{
  return true; // pass
}

void fcgi_si::FCGIRequest::Complete(int32_t app_status)
{
  // pass
}


// Helper functions



void fcgi_si::FCGIRequest::
PartitionByteSequence(const std::vector<uint8_t>& ref,
  std::vector<uint8_t>::const_iterator begin_iter,
  std::vector<uint8_t>::const_iterator end_iter, fcgi_si::FCGIType type)
{
  if(completed_ || begin_iter == end_iter)
    return;

  auto message_length = std::distance(begin_iter, end_iter);
  auto message_offset = std::distance(ref.begin(), begin_iter);
  const uint8_t* message_ptr = ref.data() + message_offset;

  // Determine the number of full records and the length of a partial record
  // if present. Determine the padding length for the partial record.
  decltype(message_length) full_record_count
    {message_length / fcgi_si::kMaxRecordContentByteLength};
  uint16_t partial_record_length
    {static_cast<uint16_t>(message_length % fcgi_si::kMaxRecordContentByteLength)};
  uint8_t padding_count {(partial_record_length % 8) ?
    static_cast<uint8_t>(8 - (partial_record_length % 8))} :
    static_cast<uint8_t>(0);

  uint8_t padding[8] = {}; // Initialize to all zeroes.

  // Populate header for full records.
  uint8_t header[8];
  header[fcgi_si::kHeaderVersionIndex]         =
    fcgi_si::FCGI_VERSION_1;
  header[fcgi_si::kHeaderTypeIndex]            =
    static_cast<uint8_t>(type);
  header[fcgi_si::kHeaderRequestIDB1Index]     =
    static_cast<uint8_t>(request_identifier_.FCGI_id() >> 8);
  header[fcgi_si::kHeaderRequestIDB0Index]     =
    static_cast<uint8_t>(request_identifier_.FCGI_id());
  header[fcgi_si::kHeaderContentLengthB1Index] =
    0xff; // A full record has a content length value with a bit sequence of 8 ones.
  header[fcgi_si::kHeaderContentLengthB0Index] =
    0xff;
  header[fcgi_si::kHeaderPaddingLengthIndex]   =
    0;
  header[fcgi_si::kHeaderReservedByteIndex]    =
    0;

  // Create a lock for use by WriteHelper.
  std::unique_lock<std::mutex> write_lock {*write_mutex_ptr_, std::defer_lock_t {}};

  if(full_record_count)
  {
    for(decltype(message_length) i {0}; i < full_record_count; i++)
    {
      WriteHelper(&write_lock, header, 8, true, false);
      WriteHelper(&write_lock, message_ptr, fcgi_si::kMaxRecordContentByteLength,
        false, true);
      message_ptr += fcgi_si::kMaxRecordContentByteLength;
    }
  }
  if(partial_record_length)
  {
    // Modify header for partial record.
    header[fcgi_si::kHeaderContentLengthB1Index] =
      static_cast<uint8_t>(partial_record_length >> 8);
    header[fcgi_si::kHeaderContentLengthB0Index] =
      static_cast<uint8_t>(partial_record_length);
    header[fcgi_si::kHeaderPaddingLengthIndex]   =
      padding_count;

    bool send_padding {padding_count > 0};

    WriteHelper(&write_lock, header, 8, true, false);
    WriteHelper(&write_lock, message_ptr, partial_record_length, false, !send_padding);
    if(send_padding)
      WriteHelper(&write_lock, padding, padding_count, false, true);
  }
}

void fcgi_si::FCGIRequest::
WriteHelper(std::unique_lock<std::mutex>* lock_ptr, const uint8_t* message_ptr,
  uint16_t message_length, bool acquire_lock, bool release_lock)
{
  bool acquired_mutex {false};

  int select_descriptor_range {request_identifier_.descriptor() + 1};
  fd_set write_set {};

  // Conditionally ACQUIRE write mutex.
  if(acquire_lock)
    lock_ptr->lock();

  while(message_length > 0)
  {
    // Conditionally ACQUIRE write mutex.
    if(!lock_ptr->owns_lock())
    {
      lock_ptr->lock();
      acquired_mutex = true;
    }
    // *write_mutex_ptr_ is held.
    std::size_t write_return {socket_functions::SocketWrite(
      request_identifier_.descriptor(), message_ptr, message_length)};
    if(write_return == message_length)
      message_length == 0;
    else // write_return < message_length
    {
      // EINTR is handled by SocketWrite
      if(errno == EAGAIN || errno == EWOULDBLOCK)
      {
        if(write_return == 0 && acquired_mutex)
          // Conditionally RELEASE write mutex.
          lock_ptr->unlock();
        else
        {
          message_ptr += write_return;
          message_length -= write_return;
        }
        // Reset flag for mutex acquisition as the loop will iterate.
        acquired_mutex = false;
        // Call select with error handling.
        while(true)
        {
          // The loop exits only when writing won't block or an error is thrown.
          FD_ZERO(&write_set);
          FD_SET(request_identifier_.descriptor(), &write_set);
          if(select(select_descriptor_range, nullptr, &write_set, nullptr,
            nullptr) == -1)
          {
            if(errno != EINTR)
              throw std::runtime_error {ERRNO_ERROR_STRING("select")};
          }
          else
            break;
        }
      }
      else if(errno == EPIPE)
      {
        SetComplete();
        break;
      }
      else // Cannot handle the error.
        throw std::runtime_error {ERRNO_ERROR_STRING("write from a call to socket_functions::SocketWrite")};
    }
  }
  if(release_lock)
    lock_ptr->unlock(); // Conditionally RELEASE write mutex.
}

void fcgi_si::FCGIRequest::SetComplete()
{
  if(!completed_)
  {
    interface_ptr_->RemoveRequest(request_identifier_);
    completed_ = true;
    if(close_connection_)
      interface_ptr_->application_closure_request_set_.insert(
        request_identifier_.descriptor());
  }
}
