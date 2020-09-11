// Properties of the algorithm implemented by IdManager:
// 1) Conceptually, the algorithm tracks used and unused IDs. As IDs
//    are requested and released by the application, the set of used IDs and
//    the set of unused IDs are updated.
// 2) After any update, all unused IDs are less than the maximum used ID.
//    Before and after each update, each ID can then be regarded as belonging
//    to one of three possible categories of IDs: used, unused, and not
//    tracked.
// 3) IDs start at 1. The maximum ID is determined by the upper limit of the
//    fixed-width integer type used for the IDs.
// 4) If the set of unused IDs is non-empty, then a request for an ID will
//    be fulfilled with one of the unused IDs. If the set of unused IDs is
//    empty, then the request will be fulfilled with the current maximum used
//    ID plus one when this is possible. If this is not possible because all
//    possible IDs are in use, then an exception is thrown.

#include <map>

namespace a_component {

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
  int GetId();

  // Returns true if the id is in use. Returns false otherwise.
  //
  // Preconditions: none.
  //
  // Exceptions: noexcept. 
  inline bool IsUsed(int id) const noexcept
  {
    return FindUsedRange(id) != used_ranges_.cend();
  }

  inline int NumberUsedIds() const noexcept
  {
    return number_in_use_;
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
  void ReleaseId(int id);

  inline IdManager()
  : number_in_use_ {0},
    used_ranges_   {}
  {}

  IdManager(const IdManager&) = default;
  IdManager(IdManager&&) = default;

  IdManager& operator=(const IdManager&) = default;
  IdManager& operator=(IdManager&&) = default;

  ~IdManager() = default;

 private:
  // Both functions return an iterator to the used interval which contains
  // id if an interval which contains id exists. If no such interval exists,
  // then the past-the-last iterator is returned. Both functions do not
  // mutate the IdManager instance. The non-const overload is present to
  // allow the result of FindUsedRange to be used in the mutating
  // implementation of ReleaseId.
  std::map<int, int>::iterator FindUsedRange(int id) noexcept;
  std::map<int, int>::const_iterator FindUsedRange(int id) const noexcept;

  int number_in_use_;
  std::map<int, int> used_ranges_;
};

} // namespace a_component
