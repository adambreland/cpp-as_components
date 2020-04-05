// C standard library headers in the C++ standard library.
#include <cstdint>         // For uint16_t.

#include <algorithm>
#include <map>
#include <vector>

#include "include/utility.h"
#include "include/request_data.h"

namespace fcgi_si {

RequestData::RequestData(uint16_t role, bool close_connection)
: FCGI_PARAMS_complete_ {false}, FCGI_STDIN_complete_ {false},
  FCGI_DATA_complete_ {false}, role_ {role}, client_set_abort_ {false},
  close_connection_ {close_connection},
  request_status_ {RequestStatus::kRequestPending},
  connection_closed_by_interface_ {false}
{}

bool RequestData::ProcessFCGI_PARAMS()
{
  bool result {true};
  if(FCGI_PARAMS_.size())
  {
    using byte_seq_pair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;
    std::vector<byte_seq_pair> name_value_pair_list
      {ProcessBinaryNameValuePairs(FCGI_PARAMS_.data(), FCGI_PARAMS_.size())};
    if(name_value_pair_list.size())
    {
      auto first_component_comp =
        [](const byte_seq_pair& lhs, const byte_seq_pair& rhs)->bool
        {
          return lhs.first < rhs.first;
        };
      std::sort(name_value_pair_list.begin(), name_value_pair_list.end(),
        first_component_comp);
      auto current = name_value_pair_list.begin();
      auto next = ++name_value_pair_list.begin();
      while(next != name_value_pair_list.end())
      {
        if(current->first == next->first)
          if(current->second == next->second)
            ++next;
          else
          {
            result = false;
            break;
          }
        else
        {
          environment_map_.emplace_hint(environment_map_.end(), std::move(*current));
          current = next;
          ++next;
        }
      }
      if(result)
        environment_map_.emplace_hint(environment_map_.end(), std::move(*current));
    }
    else
      result = false;
  }
  else
    result = false;
  return result;
}

} // namespace fcgi_si
