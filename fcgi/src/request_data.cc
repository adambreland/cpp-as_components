#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "include/fcgi_server_interface.h"
#include "include/fcgi_utilities.h"

namespace as_components {
namespace fcgi {

FcgiServerInterface::RequestData::
RequestData(uint16_t role, bool close_connection)
: role_ {role}, close_connection_ {close_connection}
{}

bool FcgiServerInterface::RequestData::
CheckRequestCompletionWithConditionalUpdate() noexcept
{
  if(role_ == FCGI_RESPONDER)
  {
    bool completed {FCGI_PARAMS_complete_ && FCGI_STDIN_complete_ &&
      ((FCGI_DATA_.size() == 0) || FCGI_DATA_complete_)};
    if(completed)
    {
      if(!FCGI_DATA_complete_)
      {
        FCGI_DATA_complete_ = true;
      }
    }
    return completed;
  }
  else if(role_ == FCGI_AUTHORIZER)
  {
    bool completed {FCGI_PARAMS_complete_                 &&
      ((FCGI_STDIN_.size() == 0) || FCGI_STDIN_complete_) &&
      ((FCGI_DATA_.size()  == 0) || FCGI_DATA_complete_)};
    if(completed)
    {
      if(!FCGI_STDIN_complete_)
      {
        FCGI_STDIN_complete_ = true;
      }
      if(!FCGI_DATA_complete_)
      {
        FCGI_DATA_complete_ = true;
      }
    }
    return completed;
  }
  else
  {
    return FCGI_PARAMS_complete_ && FCGI_STDIN_complete_ && FCGI_DATA_complete_;
  }
}

bool FcgiServerInterface::RequestData::ProcessFCGI_PARAMS()
{
  try
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
        auto next = ++(name_value_pair_list.begin());

        while(next != name_value_pair_list.end())
        {
          if(current->first == next->first)
          {
            if(current->second == next->second)
              ++next;
            else
              return false; 
              // A list of environment variables was sent which had distinct
              // definitions for the same variable. Regard the list as corrupt.
          }
          else
          {
            environment_map_.emplace_hint(environment_map_.end(),
              std::move(*current));
            current = next;
            ++next;
          }
        }
        environment_map_.emplace_hint(environment_map_.end(), 
          std::move(*current));
      }
      else // ExtractBinaryNameValuePairs found a formatting error.
        return false;
    }

    return true;
  }
  catch(...)
  {
    environment_map_.clear();
    throw;
  }
}

} // namespace fcgi
} // namespace as_components
