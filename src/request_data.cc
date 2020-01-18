// C standard library headers in the C++ standard library.
#include <cstdlib>         // For uint16_t.

#include "include/pair_processing.h"
#include "include/request_data.h"

fcgi_si::RequestData::
RequestData(uint16_t role, bool close_connection)
: FCGI_PARAMS_complete_ {false}, FCGI_STDIN_complete_ {false},
  FCGI_DATA_complete_ {false}, role_ {role}, abort_ {false},
  close_connection_ {close_connection},
  request_status_ {fcgi_si::RequestStatus::kRequestPending}
{}

void fcgi_si::RequestData::
AppendToPARAMS(const uint8_t* buffer_ptr, size count)
{
  for(int i {0}; i < count; i++)
    FCGI_PARAMS_.push_back(*(buffer_ptr + i));
}

void fcgi_si::RequestData::
AppendToSTDIN(const uint8_t* buffer_ptr, size count)
{
  for(int i {0}; i < count; i++)
    FCGI_STDIN_.push_back(*(buffer_ptr + i));
}

void fcgi_si::RequestData::
AppendToDATA(const uint8_t* buffer_ptr, size count)
{
  for(int i {0}; i < count; i++)
    FCGI_DATA_.push_back(*(buffer_ptr + i));
}

bool fcgi_si::RequestData::ProcessFCGI_PARAMS()
{
  bool result {true};
  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
    name_value_pair_list {};
  if(FCGI_PARAMS_.size())
  {
    name_value_pair_list = fcgi_si::ProcessBinaryNameValuePairs(FCGI_PARAMS_.size(),
      FCGI_PARAMS_.data());
  }
  if(name_value_pair_list.size())
  {
    // Sort the pairs according to the name which will become the map key.
    // Sort, but then handle duplicate names during the insertion step.
    // Duplicate names are accepted if there values are identical. If not,
    // the parameter list is rejected and the request is rejected.
  }
  else
    result = false;
}
