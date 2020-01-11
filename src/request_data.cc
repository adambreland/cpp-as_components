// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint16_t.

#include "include/data_types.h"

fcgi_si::RequestData::
RequestData(uint16_t role, bool close_connection)
: FCGI_PARAMS_complete_ {false}, FCGI_STDIN_complete_ {false},
  FCGI_DATA_complete_ {false}, role_ {role}, abort_ {false},
  close_connection_ {close_connection},
  request_status_ {fcgi_si::RequestStatus::kRequestPending}
{}

inline bool fcgi_si::RequestData::get_abort() const
{
  return abort_;
}

inline void fcgi_si::RequestData::set_abort()
{
  abort_ = true;
}

inline bool fcgi_si::RequestData::get_close_connection() const
{
  return close_connection_;
}

inline uint16_t fcgi_si::RequestData::get_role() const
{
  return role_;
}

inline bool fcgi_si::RequestData::IsRequestComplete() const
{
  return FCGI_PARAMS_complete_ && FCGI_STDIN_complete_ && FCGI_DATA_complete_;
}

// PARAMS

inline bool fcgi_si::RequestData::get_PARAMS_completion() const
{
  return FCGI_PARAMS_complete_;
}

inline void fcgi_si::RequestData::CompletePARAMS()
{
  FCGI_PARAMS_complete_ = true;
}

void fcgi_si::RequestData::
AppendToPARAMS(const uint8_t* buffer_ptr, size count)
{
  for(int i {0}; i < count; i++)
    FCGI_PARAMS_.push_back(*(buffer_ptr + i));
}

// STDIN

inline bool fcgi_si::RequestData::get_STDIN_completion() const
{
  return FCGI_STDIN_complete_;
}

inline void fcgi_si::RequestData::CompleteSTDIN()
{
  FCGI_STDIN_complete_ = true;
}

void fcgi_si::RequestData::
AppendToSTDIN(const uint8_t* buffer_ptr, size count)
{
  for(int i {0}; i < count; i++)
    FCGI_STDIN_.push_back(*(buffer_ptr + i));
}

// DATA

inline bool fcgi_si::RequestData::get_DATA_completion() const
{
  return FCGI_DATA_complete_;
}

inline void fcgi_si::RequestData::CompleteDATA()
{
  FCGI_DATA_complete_ = true;
}

void fcgi_si::RequestData::
AppendToDATA(const uint8_t* buffer_ptr, size count)
{
  for(int i {0}; i < count; i++)
    FCGI_DATA_.push_back(*(buffer_ptr + i));
}
