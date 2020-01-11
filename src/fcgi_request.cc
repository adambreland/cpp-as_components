#include <utility>
#include <stdexcept>

#include "include/data_types.h"

fcgi_si::FCGIRequest(fcgi_si::RequestIdentifier request_id,
  fcgi_si::RequestData* request_data_ptr, std::mutex* write_mutex_ptr,
  std::mutex* interface_state_mutex_ptr)
: request_identifier_        {request_id},
  environment_map_           {std::move(request_data_ptr->environment_map_)},
  request_stdin_content_     {std::move(request_data_ptr->FCGI_STDIN_)},
  request_data_content_      {std::move(request_data_ptr->FCGI_DATA_)},
  role_                      {request_data_ptr->role_},
  close_connection_          {request_data_ptr->close_connection_},
  completed_                 {false},
  write_mutex_ptr_           {write_mutex_ptr},
  interface_state_mutex_ptr_ {interface_state_mutex_ptr}
{
  if(request_data_ptr == nullptr || write_mutex_ptr == nullptr
     || interface_state_mutex_ptr == nullptr)
    throw std::invalid_argument {"A pointer with a nullptr value was used to construct an FCGIRequest object."};

  // Update the status of the RequestData object to reflect its use in the
  // construction of an FCGIRequest which will be exposed to the application.
  request_data_ptr->request_status_ = fcgi_si::RequestStatus::kRequestAssigned;
}

fcgi_si::FCGIRequest(FCGIRequest&& request)
: request_identifier_ {request.request_identifier_},
  environment_map_ {std::move(request.environment_map_)},
  request_stdin_content_ {std::move(request.request_stdin_content_)},
  request_data_content_ {std::move(request.request_data_content_)},
  role_ {request.role_},
  close_connection_ {request.close_connection_},
  completed_ {request.completed_},
  write_mutex_ptr_ {request.write_mutex_ptr_},
  interface_state_mutex_ptr_ {request.interface_state_mutex_ptr_}
{
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
  request_identifier_ = request.request_identifier_;
  environment_map_ = std::move(request.environment_map_);
  request_stdin_content_ = std::move(request.request_stdin_content_);
  request_data_content_ = std::move(request.request_data_content_);
  role_ = request.role_;
  close_connection_ = request.close_connection_;
  completed_ = request.completed_;
  write_mutex_ptr_ = request.write_mutex_ptr_;
  interface_state_mutex_ptr_ = request.interface_state_mutex_ptr_;

  request.request_identifier_ = fcgi_si::RequestIdentifier {};
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  request.close_connection_ = false;
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
  if(completed_)
    return false;

  // Acquire interface_state_mutex_ to determine current abort state.
  std::lock_guard<std::mutex> interface_state_lock {*interface_state_mutex_ptr_};
  request
}

uint16_t fcgi_si::FCGIRequest::get_role() const
{
  return role_;
}
