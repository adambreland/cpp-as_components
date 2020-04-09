// C standard library headers in the C++ standard library.
#include <cstdint>         // For uint16_t.

#include <algorithm>
#include <map>
#include <vector>

#include "include/utility.h"
#include "include/request_data.h"

namespace fcgi_si {

RequestData::RequestData(uint16_t role, bool close_connection)
: role_ {role}, close_connection_ {close_connection}
{}

bool RequestData::ProcessFCGI_PARAMS()
{
  if(FCGI_PARAMS_.size())
  {
    using byte_seq_pair = std::pair<std::vector<uint8_t>, std::vector<uint8_t>>;

    std::vector<byte_seq_pair> name_value_pair_list
      {ExtractBinaryNameValuePairs(FCGI_PARAMS_.data(), FCGI_PARAMS_.size())};

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
        {
          if(current->second == next->second)
            ++next;
          else
            return false;
        }
        else
        {
          environment_map_.emplace_hint(environment_map_.end(),
            std::move(*current));
          current = next;
          ++next;
        }
      }
      environment_map_.emplace_hint(environment_map_.end(), std::move(*current));
    }
    else // ExtractBinaryNameValuePairs found a formatting error.
      return false;
  }

  return true;
}

} // namespace fcgi_si
