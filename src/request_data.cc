// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint16_t.

#include "include/data_types.h"

fcgi_synchronous_interface::RequestData::
RequestData(uint16_t role, bool close_connection)
: FCGI_PARAMS_complete_ {false}, FCGI_STDIN_complete_ {false},
  FCGI_DATA_complete_ {false}, role_ {role}, abort_ {false},
  close_connection_ {close_connection},
  request_status_ {fcgi_synchronous_interface::RequestStatus::kRequestPending}
{}

inline bool fcgi_synchronous_interface::RequestData::get_abort() const
{
  return abort_;
}

inline void fcgi_synchronous_interface::RequestData::set_abort()
{
  abort_ = true;
}

inline bool fcgi_synchronous_interface::RequestData::get_close_connection() const
{
  return close_connection_;
}

inline uint16_t fcgi_synchronous_interface::RequestData::get_role() const
{
  return role_;
}

inline bool fcgi_synchronous_interface::RequestData::IsRequestComplete() const
{
  return FCGI_PARAMS_complete_ && FCGI_STDIN_complete_ && FCGI_DATA_complete_;
}

// PARAMS

inline bool fcgi_synchronous_interface::RequestData::get_PARAMS_completion() const
{
  return FCGI_PARAMS_complete_;
}

inline void fcgi_synchronous_interface::RequestData::CompletePARAMS()
{
  FCGI_PARAMS_complete_ = true;
}

void fcgi_synchronous_interface::RequestData::
AppendToPARAMS(const uint8_t* buffer_ptr, size count)
{
  FCGI_PARAMS_.append(buffer_ptr, count);
}

// STDIN

inline bool fcgi_synchronous_interface::RequestData::get_STDIN_completion() const
{
  return FCGI_STDIN_complete_;
}

inline void fcgi_synchronous_interface::RequestData::CompleteSTDIN()
{
  FCGI_STDIN_complete_ = true;
}

void fcgi_synchronous_interface::RequestData::
AppendToSTDIN(const uint8_t* buffer_ptr, size count)
{
  FCGI_STDIN_.append(buffer_ptr, count);
}

// DATA

inline bool fcgi_synchronous_interface::RequestData::get_DATA_completion() const
{
  return FCGI_DATA_complete_;
}

inline void fcgi_synchronous_interface::RequestData::CompleteDATA()
{
  FCGI_DATA_complete_ = true;
}

void fcgi_synchronous_interface::RequestData::
AppendToDATA(const uint8_t* buffer_ptr, size count)
{
  FCGI_DATA_.append(buffer_ptr, count);
}
