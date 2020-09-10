#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

#include "include/id_manager.h"

namespace a_component {

std::map<int, int>::iterator IdManager::FindUsedRange(int id) noexcept
{
  std::map<int, int>::iterator end_iter {used_ranges_.end()};
  if(used_ranges_.size() == 0U)
    return end_iter;
  std::map<int, int>::iterator i_release {used_ranges_.lower_bound(id)};
  std::map<int, int>::iterator i_last {end_iter};
  --i_last;
  if(i_release == end_iter)
  {
    i_release = i_last;
  }
  else
  {
    if(i_release->first != id)
    {
      if(i_release == used_ranges_.cbegin())
      {
        return end_iter;
      }
      --i_release;
    }
  }
  // i_release should now contain id.
  if((i_release->first > id) || (id > i_release->second))
  {
    return end_iter;
  }
  return i_release;
}

std::map<int, int>::const_iterator IdManager::FindUsedRange(int id) const noexcept
{
  std::map<int, int>::const_iterator end_iter {used_ranges_.cend()};
  if(used_ranges_.size() == 0U)
    return end_iter;
  std::map<int, int>::const_iterator i_release {used_ranges_.lower_bound(id)};
  std::map<int, int>::const_iterator i_last {end_iter};
  --i_last;
  if(i_release == end_iter)
  {
    i_release = i_last;
  }
  else
  {
    if(i_release->first != id)
    {
      if(i_release == used_ranges_.cbegin())
      {
        return end_iter;
      }
      --i_release;
    }
  }
  // i_release should now contain id.
  if((i_release->first > id) || (id > i_release->second))
  {
    return end_iter;
  }
  return i_release;
}

int IdManager::GetId()
{
  if(used_ranges_.size() == 0U)
  {
    used_ranges_.insert({1, 1});
    return (number_in_use_ = 1);
  }
  else
  {
    std::map<int, int>::iterator i_min {used_ranges_.begin()};
    int current_least_used {i_min->first};
    if(current_least_used > 1)
    {
      // The new ID is 1.
      if(current_least_used > 2)
      {
        used_ranges_.insert({1, 1});
      }
      else
      {
        used_ranges_.insert({1, i_min->second});
        used_ranges_.erase(i_min);
      }
      ++number_in_use_;
      return 1;
    }
    else
    {
      std::map<int, int>::iterator i_next {i_min};
      ++i_next;
      if(i_next != used_ranges_.end())
      {
        // The new ID cannot be larger than the maximum as other IDs are larger
        // than it is.
        int new_id {i_min->second + 1};
        // Is merging necessary?
        if(new_id == i_next->first)
        {
          i_min->second = i_next->second;
          used_ranges_.erase(i_next);
        }
        else
        {
          i_min->second = new_id;
        }
        ++number_in_use_;
        return new_id;
      }
      else
      {
        // Extension is needed. Is it possible?
        if(i_min->second == std::numeric_limits<int>::max())
        {
          throw std::length_error {"A request for a new ID was made when all "
            "possible IDs had been assigned."};
        }
        else
        {
          ++number_in_use_;
          return ++(i_min->second);
        }
      }
    }
  }
}

void IdManager::ReleaseId(int id)
{
  std::map<int, int>::iterator i_release {FindUsedRange(id)}; 
  if(i_release == used_ranges_.end())
  {
    throw std::logic_error {"Release was requested for an ID which "
      "was not in use."};
  }

  int last_in_i_release {i_release->second};
  if(id == i_release->first)
  {
    // Note that the logic in this block covers the special case for which
    // id == 1.
    if(id == last_in_i_release)
    {
      used_ranges_.erase(i_release);
    }
    else
    {
      used_ranges_.insert({id + 1, last_in_i_release});
      used_ranges_.erase(i_release);
    }
  }
  else if(id == last_in_i_release)
  {
    // Note that id == i_release->first is not possible here as that case
    // is covered above.
    --(i_release->second);
  }
  else
  {
    used_ranges_.insert({id + 1, last_in_i_release});
    i_release->second = id - 1;
  }

  --number_in_use_;
}

} // namespace a_component
