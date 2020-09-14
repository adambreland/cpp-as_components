#ifndef A_COMPONENT_ID_MANAGER_INCLUDE_ID_MANAGER_TEMPLATE_H_
#define A_COMPONENT_ID_MANAGER_INCLUDE_ID_MANAGER_TEMPLATE_H_

// Class Description:
//    Let I be an integral type. Let I_max be the maximum value of this type.
// The semantics of IdMananger<I> are determined by:
// 1) The notion that a type instance holds a representation of a dynamic set 
//    which is a subset of [1, I_max].
// 2) That an operation void ReleaseId(I) of IdManager<I> satisfies for each
//    value i of I: 
//    a) If at time t, i is in the dynamic set S(t) of a type instance, then,
//       at time t+1, after the invocation of ReleaseId(i) on the type instance,
//       the dynamic set of the instance satisfies S(t+1) = S(t)\{i} (where \ 
//       represents set difference).
//    b) If at time t, i is not in the dynamic set of a type instance, then the
//       invocation ReleaseId(i) on the type instance caused an exception to
//       be thrown. No change to the type instance occurred in this case.
// 3) That an operation I GetId() of IdManager<I> satisfies:
//    a) If the dynamic set of a type instance at time t is empty, then, at
//       time t+1, after the invocation GetId() on the instance, the dynamic
//       set of the instance is equal to {1} and the invocation returned 1.
//    b) If the dynamic set of a type instance at time t is non-empty and equal
//       to S(t), then, at time t+1, after the invocation GetId() on the type
//       instance, the following properties hold: 
//       a) If the set A = [1, Max(S(t))]\S(t) was non-empty, then a value i in
//          A was returned and S(t+1) = S(t) U {i} (where U is set union).
//       b) If the set A defined above was empty and Max(S(t)) was not equal to
//          I_max, then Max(S(t)) + 1 was returned and
//          S(t+1) = S(t) U {Max(S(t)) + 1}.
//       c) If the set A defined above was empty and Max(S(t)) was equal to
//          I_max, then an exception was thrown and the type instance was not
//          changed.
// 4) That an operation I NumberUsedIds() returns the size of the dynamic set
//    held by an instance.
// 5) That an operation bool IsUsed(I) returns the truth value of the
//    membership relation for the set of an instance and each value i of I.
//
//    IdManager can be seen as a specialization of a dynamic set which holds
// integral values.

// Implementation discussion:
//    Instead of using a set data structure whose members are values i of I,
// a set abstract data type whose members are ranges of consecutive values of
// I will be used. This set will be represented by a map data structure whose
// keys are the least integral values of the stored ranges and whose values are
// the maximum values of the stored ranges. In addition, ranges are disjoint.
//    This range-based organization allows the return value of a call to GetId
// to be determined easily. It also significantly reduces the space
// requirement of IdManager for important cases. In particular, if the
// dynamic set of an IdManager instance can be represented by a single
// consecutive range of integral values, only a single item is needed in the
// map data structure of the IdManager instance.
//
//    Formally, the representation of the dynamic set of an IdManager instance
// is the minimal, unique set of consecutive integral ranges of the set.
//    Minimality is defined in terms of range number. Given sets A and B of
// consecutive ranges of integers whose union is each equal to a given set,
// A is less than B if and only if the number of ranges of A is less than that
// of B. This relation is a partial order on the described sets of ranges for
// each given union set.
//    Uniqueness of a minimal set of ranges can be shown with inductive
// contradiction.
//
// Proof:
//   Suppose, for a given set, that A and B are sets of consecutive ranges of
// integral values and that the union of these ranges for each of A and B is
// equal to the given set. Suppose also that A and B are minimal as defined
// above. Finally, suppose that A and B are distinct.
//    Note that the ranges of each of A and B must be disjoint. If this
// were not the case, two ranges which overlapped could be joined and this
// joined set would be a range that could replace the two ranges which were
// used to form it. The resulting set of ranges would then have fewer ranges
// than the original set. This would contradict the minimality of the original
// set.
//    Assume an ordering of the ranges of A and B according to the least
// element of each range. In this order, L(i) < L(i+1), M(i) < L(i+1), and
// M(i) < M(i+1) for all ranges where L(i) is the least value and M(i) is the
// maximum value of range i. 
//    Note that the least value of A is the least value of B and that, as a
// result, the values of L for the first ranges of A and B are the same.
// Suppose that the first ranges of A and B were not identical. Then one of the
// maximum values of the range must be greater than the other. Let M_2 be the
// larger of the two maximum values and M_1 the lesser. Note that M_1 + 1 must
// the first value of the next range of set of M_1 as this value must be in
// the given common set due its presence the set of M_2. But then a joined
// range could be formed and used in a set of ranges which would have a smaller
// number of ranges than that of the set of M_1. This is a contradiction as
// the set of M_1 is minimal. Thus, the first ranges of A and B must be
// identical.
//   Note that the least values of the next ranges for each of A and B must be
// the same. If this were not the case, then one of A or B would not have at
// least one element that the other has. This is a contradiction as the union
// of the ranges of A is the same as the union of the ranges of B and the
// "missing" elements cannot appear later in a range if we order the ranges of
// A and B as described above. A similar argument to the one given above shows
// that the considered range of A and of B are identical. This proves the
// inductive hypothesis which shows that A and B are identical. Since this
// contradicts the assumption that A and B are distinct, we have shown
// that minimal sets of ranges of consecutive integral values are unique.
// 
//    The goal of an implementation of the IdManager class which uses a set of
// consecutive ranges of integral values to represent the dynamic set of a
// class instance is then to implement state transitions between the minimal
// sets of ranges discussed above which follow the semantics of IdManager.

#include <limits>
#include <map>
#include <type_traits>

namespace a_component {

template<typename I>
class IdManager
{
 public:
  // Returns an unused ID. IDs start at 1. 
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) Exceptions derived from std::exception may be thrown.
  // 2) Strong exception guarantee.
  // 3) If all possible allowed IDs are in use, an exception is thrown.
  //
  // Effects:
  // 1) If no IDs are in use, 1 is returned.
  // 2) If no IDs which are less than the current maximum in-use ID are
  //    available, then the value of the returned ID is one more than the 
  //    current maximum used ID.
  // 3) If IDs which are less than the current maximum used ID are available,
  //    then one of these IDs is returned.
  // 4) The returned ID is regarded as used.
  I GetId();

  // Returns true if the id is in use. Returns false otherwise.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) Exceptions derived from std::exception may be thrown.
  // 2) Strong exception guarantee.
  inline bool IsUsed(I i) const
  {
    return (FindInterval(i) != id_intervals_.cend());
  }

  // Informs the IdManager instance that id should no longer be regarded as
  // used.
  //
  // Preconditions: none.
  //
  // Exceptions:
  // 1) Exceptions derived from std::exception may be thrown.
  // 2) Strong exception guarantee.
  // 3) If id is not currently in use, an exception is thrown.
  //
  // Effects:
  // 1) The IdManager instance recorded that id is no longer in use.
  // 2) Future calls to GetId may return id if it not in use and is not larger
  //    than the maximum in-use id plus one at the time of the call to GetId.
  inline I NumberOfUsedIds() const noexcept
  {
    return size_;
  }

  void ReleaseId(I);

  inline IdManager()
  : size_         {0},
    id_intervals_ {}
  {
    static_assert(std::is_integral<I>::value, "An integral type must be used.");
  }

  IdManager(const IdManager&) = default;
  IdManager(IdManager&&) = default;

  IdManager& operator=(const IdManager&) = default;
  IdManager& operator=(IdManager&&) = default;

  ~IdManager() = default;

 private:
  typename std::map<I, I>::iterator FindInterval(I);
  typename std::map<I, I>::const_iterator FindInterval(I) const;

  I size_;
  std::map<I, I> id_intervals_;
};

template<typename I>
typename std::map<I, I>::iterator 
IdManager<I>::FindInterval(I id)
{
  typename std::map<I, I>::iterator intervals_end
    {id_intervals_.end()};

  auto IsContainedInPreviousIntervalAndGreaterThanItsStart = [id, intervals_end]
  (
    typename std::map<I, I>::iterator iter
  )->typename std::map<I, I>::iterator
  {
    --iter;
    if(id <= iter->second)
    {
      return iter;
    }
    else
    {
      return intervals_end;
    }
  };
  
  if(!(id_intervals_.size()))
  {
    return intervals_end;
  }
  typename std::map<I, I>::iterator lub {id_intervals_.lower_bound(id)};
  if(lub == intervals_end)
  {
    return IsContainedInPreviousIntervalAndGreaterThanItsStart(lub);
  }
  else 
  {
    if(lub->first > id) // And not equal to i.
    {
      if(lub == id_intervals_.begin())
      {
        return intervals_end;
      }
      else 
      {
        // Note that being equal to the start is prohibited as, in that case,
        // a "lower bound" which was equal to i would have been present above.
        return IsContainedInPreviousIntervalAndGreaterThanItsStart(lub);
      }
    }
    else // lub->first == i
    {
      return lub;
    }
  }
}

template<typename I>
typename std::map<I, I>::const_iterator 
IdManager<I>::FindInterval(I id) const
{
  typename std::map<I, I>::const_iterator intervals_end {id_intervals_.cend()};

  auto IsContainedInPreviousIntervalAndGreaterThanItsStart = [id, intervals_end]
  (
    typename std::map<I, I>::const_iterator iter
  )->typename std::map<I, I>::const_iterator
  {
    --iter;
    if(id <= iter->second)
    {
      return iter;
    }
    else
    {
      return intervals_end;
    }
  };
  
  if(!(id_intervals_.size()))
  {
    return intervals_end;
  }
  typename std::map<I, I>::const_iterator lub {id_intervals_.lower_bound(id)};
  if(lub == intervals_end)
  {
    return IsContainedInPreviousIntervalAndGreaterThanItsStart(lub);
  }
  else 
  {
    if(lub->first > id) // And not equal to i.
    {
      if(lub == id_intervals_.cbegin())
      {
        return intervals_end;
      }
      else 
      {
        // Note that being equal to the start is prohibited as, in that case,
        // a "lower bound" which was equal to i would have been present above.
        return IsContainedInPreviousIntervalAndGreaterThanItsStart(lub);
      }
    }
    else // lub->first == i
    {
      return lub;
    }
  }
}

template<typename I>
I IdManager<I>::GetId()
{
  if(!(id_intervals_.size()))
  {
    id_intervals_.insert({1, 1});
    return (size_ = 1);
  }
  else
  {
    typename std::map<I, I>::iterator i_min {id_intervals_.begin()};
    I current_least_used {i_min->first};
    if(current_least_used > 1)
    {
      // The new ID is 1.
      if(current_least_used > 2)
      {
        // The least interval cannot be extended (by decrementing the least
        // value).
        id_intervals_.insert({1, 1});
      }
      else
      {
        // Decrement the least value of the least interval.
        // Order insertion and erasure to meet the strong exception guarantee.
        id_intervals_.insert({1, i_min->second});
        id_intervals_.erase(i_min);
      }
      ++size_;
      return 1;
    }
    else
    {
      typename std::map<I, I>::iterator i_next {i_min};
      ++i_next;
      if(i_next != id_intervals_.end())
      {
        // The new ID cannot be larger than the maximum as other IDs are larger
        // than it is.
        I new_id {(i_min->second) + 1};
        // Is merging necessary?
        if((new_id + 1) == (i_next->first))
        {
          i_min->second = i_next->second;
          id_intervals_.erase(i_next);
        }
        else
        {
          i_min->second = new_id;
        }
        ++size_;
        return new_id;
      }
      else
      {
        // Extension is needed. Is it possible?
        if(i_min->second == std::numeric_limits<I>::max())
        {
          throw std::length_error {"A request for a new ID was made when all "
            "possible IDs had been assigned."};
        }
        else
        {
          ++size_;
          return ++(i_min->second);
        }
      }
    }
  }
}

template<typename I>
void IdManager<I>::ReleaseId(I id)
{
  typename std::map<I, I>::iterator i_release {FindInterval(id)};

  if(i_release == id_intervals_.end())
  {
    throw std::logic_error {"Release was requested for an ID which "
      "was not in use."};
  }

  I last_in_i_release {i_release->second};
  if(id == i_release->first)
  {
    // Note that the logic in this block covers the special case for which
    // id == 1.
    if(id == last_in_i_release)
    {
      id_intervals_.erase(i_release);
    }
    else
    {
      id_intervals_.insert({id + 1, last_in_i_release});
      id_intervals_.erase(i_release);
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
    // Split.
    id_intervals_.insert({(id + 1), last_in_i_release});
    i_release->second = (id - 1);
  }
  --size_;
}

} // namespace a_component

#endif // A_COMPONENT_ID_MANAGER_INCLUDE_ID_MANAGER_TEMPLATE_H_
