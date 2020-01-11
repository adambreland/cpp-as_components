#include <utility>

#include "include/data_types.h"

fcgi_si::FCGIRequest(fcgi_si::RequestIdentifier request_id,
  fcgi_si::Request_Data* request_data_ptr, std::mutex* write_mutex_ptr,
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
  request_status_ = fcgi_si::RequestStatus::kRequestAssigned;
}
