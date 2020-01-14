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

#include "include/data_types.h"


fcgi_si::FCGIRequest(fcgi_si::RequestIdentifier request_id,
  fcgi_si::FCGIApplicationInterface* interface_ptr,
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
  // FCGIApplicationInterface object. It is assumed that synchronization is
  // is implicit and that acquiring the shared state mutex is not necessary.
  request_data_ptr->request_status_ = fcgi_si::RequestStatus::kRequestAssigned;
}

fcgi_si::FCGIRequest(FCGIRequest&& request)
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

const std::map<std::vector<uint8_t>, std::vector<uint8_t>>&
fcgi_si::FCGIRequest::get_environment_map() const
{
  return environemnt_map_;
}

const std::vector<uint8_t>& fcgi_si::FCGIRequest::get_STDIN() const
{
  return request_stdin_content_;
}

const std::vector<uint8_t>& fcgi_si::FCGIRequest::get_DATA() const
{
  return request_data_content_;
}

bool fcgi_si::FCGIRequest::get_abort() const
{
  if(completed_ || was_aborted_)
    return was_aborted;

  // ACQUIRE interface_state_mutex_ to determine current abort state.
  std::lock_guard<std::mutex> interface_state_lock {*interface_state_mutex_ptr_};
  auto request_map_iter {interface_ptr->request_map_.find(request_identifier_)};
  // Include check for absence of request to prevent undefined method call.
  // This check should always pass.
  if(request_map_iter != request_map_.end())
    if(request_map_iter->second.get_abort())
      return was_aborted_ = true;

  return was_aborted_; // i.e. return false.
} // RELEASE interface_state_mutex_

uint16_t fcgi_si::FCGIRequest::get_role() const
{
  return role_;
}

bool fcgi_si::FCGIRequest::get_completion_status()
{
  return completed_;
}

void fcgi_si::FCGIRequest::
WriteHelper(const std::vector<uint8_t>& ref,
  std::vector<uint8_t>::const_iterator begin_iter,
  std::vector<uint8_t>::const_iterator end_iter) const
{
  if(completed_ || begin_iter == end_iter)
    return;

  // Initialize state for iterative write.
  auto message_length = std::distance(begin_iter, end_iter);
  auto message_offset = std::distance(ref.begin(), begin_iter);
  const uint8_t* message_ptr = ref.data() + message_offset

  std::unique_lock<std::mutex> write_lock {*write_mutex_ptr_, std::defer_lock_t {}};
  bool acquired_mutex {false};

  int select_descriptor_range {request_identifier_.descriptor() + 1};
  fd_set write_set {};

  while(message_length > 0)
  {
    // Conditionally acquire write mutex.
    if(!write_lock.owns_lock())
    {
      write_lock.lock();
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
          write_lock.unlock();
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
  write_lock.unlock(); // RELEASE *write_mutex_ptr_.
}

void fcgi_si::FCGIRequest::SetComplete()
{
  if(!completed_)
  {
    interface_ptr_->RemoveRequest(request_identifier_);
    completed_ = true;
    if(close_connection_)
      interface_ptr->application_closure_request_set_.insert(
        request_identifier_.descriptor());
  }
}
