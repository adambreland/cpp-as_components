// MIT License
//
// Copyright (c) 2021 Adam J. Breland
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "fcgi/include/fcgi_server_interface.h"
#include "fcgi/include/fcgi_utilities.h"

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
