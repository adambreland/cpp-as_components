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

#include <exception>
#include <limits>
#include <set>
#include <stdexcept>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/id_manager_template.h"

namespace {

class IdTracker
{
 public:
  bool RegisterAndCheckNewID(int id);
  void RegisterReleasedId(int id);

  inline int NumberInUse() const
  {
    return number_in_use_;
  }

  inline IdTracker()
  : number_in_use_ {0},
    used_ids_      {},
    available_ids_ {}
  {}

  IdTracker(const IdTracker&) = default;
  IdTracker(IdTracker&&) = default;

  IdTracker& operator=(const IdTracker&) = default;
  IdTracker& operator=(IdTracker&&) = default;

  ~IdTracker() = default;

 private:
  int number_in_use_;
  std::set<int> used_ids_;
  std::set<int> available_ids_;
};

} // namespace

bool IdTracker::RegisterAndCheckNewID(int id)
{
  // Is the ID already in use?
  if(used_ids_.find(id) != used_ids_.end())
    return false;
  
  // Are there no available IDs?
  if(available_ids_.size() == 0U)
  {
    // The returned ID should be equal to (in-use maximum + 1) when there
    // is a maximum. Otherwise, it should be equal to 1.
    int new_id {};
    if(used_ids_.size() == 0U)
    {
      new_id = 1;
    }
    else
    {
      int used_max {*(used_ids_.rbegin())};
      if(used_max == std::numeric_limits<int>::max())
        return false;
      new_id = (used_max + 1);
    }
    if(id == new_id)
    {
      used_ids_.insert(id);
      ++number_in_use_;
      return true;
    }
    else
    {
      return false;
    }
  }
  else // Available IDs are present.
  {
    // Is the ID one of the available IDs?
    std::set<int>::iterator available_iter {available_ids_.find(id)};
    if(available_iter == available_ids_.end())
    {
      return false;
    }
    else
    {
      available_ids_.erase(available_iter);
      used_ids_.insert(id);
      ++number_in_use_;
      return true;
    }
  }
}

void IdTracker::RegisterReleasedId(int id)
{
  std::set<int>::iterator released_iter {used_ids_.find(id)};
  if(released_iter == used_ids_.end())
  {
    throw std::logic_error {"A call was made to register that an ID was "
      "released when the ID was not in use according to the IdTracker."};
  }
  used_ids_.erase(released_iter);
  available_ids_.insert(id);

  // Remove excess available IDs.
  std::set<int>::reverse_iterator current_max_iter {used_ids_.rbegin()};
  if(current_max_iter != used_ids_.rend())
  {
    std::set<int>::iterator excess_available_iter 
      {available_ids_.upper_bound(*current_max_iter)};
    while(excess_available_iter != available_ids_.end())
    {
      // Copy for safe erasure.
      std::set<int>::iterator erase_iter {excess_available_iter};
      ++excess_available_iter;
      available_ids_.erase(erase_iter);
    }
  }
  else
  {
    available_ids_.clear();
  }

  --number_in_use_;
}

// Test explanation
// Examined properties:
// Explicit specification properties:
// 1) IDs start at 1:
//    a) When an IdManager instance is newly constructed.
//    b) After arbitrary use when the number of used IDs reaches zero.
// 2) An exception is thrown if all possible IDs are in use and a call to
//    GetId is made.
// 3) An exception is thrown if a call to ReleaseId is made with an ID
//    argument which is not in use.
// 4) A call to GetId never returns an ID which is larger than the maximum
//    used ID if IDs exist which are less than the maximum used ID and which
//    are not in use.
// 5) A call to GetId returns (in-use maximum ID + 1) if no IDs exist which
//    are less than the in-use maximum ID and which are not in use.
//
// Implicit specification properties:
// 1) A call to GetId never returns an ID which is in use. The value of the
//    predicate "in use" for an ID is determined by the history of IDs returned
//    by calls to GetId and the history of the ID arguments provided to calls
//    to ReleaseID.
//
// Test Cases:
// 1) IsUsed behaves properly for a newly constructed object on the special
//    values -1, 0, 1, and std::numeric_limits<int>::max().
// 2) New instance. A call to GetID returns 1. The call ReleaseID(1) does not
//    throw an exception. A call to GetID returns 1. Throughout, IsUsed
//    behaves as specified.
// 3) New instance. Arbitrary, valid calls to GetId and ReleaseId are made.
//    The calls are arranged so that the used set becomes empty. Upon becoming
//    empty, a call to GetID returns 1. Throughout, calls to IsUsed behave as
//    expected.
// 4) (Commented out during routine testing as it has a running time on the
//    order of 10 minutes.) An exception is thrown when all possible IDs are in
//    use.
//
// Note for future testing: A class which takes a GetId and ReleaseID ratio,
// either a random or a duration-in-use distribution for arguments to
// ReleaseID, and only calls ReleaseID if IDs are available could be used
// for replicate testing. This class would perform a specified number of
// GetId or ReleaseId calls based on the provided distributions and would
// track used and unused IDs to validate the behavior of IdManager.
// E.g. A GetId/ReleaseId ratio of 0 means that an ID is released as soon as
//      it is returned. The sequence is then G1, R1, G1, R1, ... .
// E.g. A GetId/ReleaseId ratio of 1 would allow some random fluctuations
//      to occur. A large number of replicates where each replciate uses a
//      large operation count would provide a more thorough test.

TEST(IdManager, NewInstanceIsUsed)
{
  as_components::IdManager<int> id_manager {};

  EXPECT_EQ(id_manager.NumberOfUsedIds(), 0);
  EXPECT_FALSE(id_manager.IsUsed(-1));
  EXPECT_FALSE(id_manager.IsUsed(0));
  EXPECT_FALSE(id_manager.IsUsed(1));
  EXPECT_FALSE(id_manager.IsUsed(std::numeric_limits<int>::max()));
}

TEST(IdManager, NewInstanceMinimalUse)
{
  as_components::IdManager<int> id_manager {};

  int new_id {};
  ASSERT_NO_THROW(new_id = id_manager.GetId());
  EXPECT_EQ(new_id, 1);
  EXPECT_TRUE(id_manager.IsUsed(1));
  EXPECT_EQ(id_manager.NumberOfUsedIds(), 1);

  ASSERT_NO_THROW(id_manager.ReleaseId(new_id));
  EXPECT_FALSE(id_manager.IsUsed(1));
  EXPECT_EQ(id_manager.NumberOfUsedIds(), 0);

  EXPECT_EQ(id_manager.GetId(), 1);
  EXPECT_TRUE(id_manager.IsUsed(1));
  EXPECT_EQ(id_manager.NumberOfUsedIds(), 1);
}

TEST(IdManager, NewInstanceUseAndEmpty)
{
  std::vector<int> get_returns {};
  IdTracker id_tracker {};
  as_components::IdManager<int> id_manager {};

  auto GTestFatalGetCheckRecord = [&get_returns, &id_tracker, &id_manager]
  (
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalGetCheckRecord"};
    int new_id {id_manager.GetId()};
    bool valid_id {id_tracker.RegisterAndCheckNewID(new_id)};
    EXPECT_TRUE(id_manager.IsUsed(new_id));
    EXPECT_EQ(id_manager.NumberOfUsedIds(), id_tracker.NumberInUse());
    ASSERT_TRUE(valid_id);
    get_returns.push_back(new_id);
  };

  auto GTestFatalReleaseRecord = [&get_returns, &id_tracker, &id_manager]
  (
    std::vector<int>::size_type index,
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalReleaseRecord"};
    int to_release {get_returns[index]};
    get_returns.erase(get_returns.begin() + index);
    ASSERT_NO_THROW(id_manager.ReleaseId(to_release));
    EXPECT_FALSE(id_manager.IsUsed(to_release));
    ASSERT_NO_THROW(id_tracker.RegisterReleasedId(to_release));
    EXPECT_EQ(id_manager.NumberOfUsedIds(), id_tracker.NumberInUse());
  };

  for(int i {0}; i < 10; ++i)
  {
    ASSERT_NO_FATAL_FAILURE(GTestFatalGetCheckRecord(__LINE__)) <<
      "iteration count: " << i;
    EXPECT_EQ(get_returns.back(), i + 1);
  }

  EXPECT_FALSE(id_manager.IsUsed(11));
  EXPECT_FALSE(id_manager.IsUsed(0));
  EXPECT_FALSE(id_manager.IsUsed(-1));
  
                                                                  // 10 {[1,10]}
  // Action                                                       // Number of items after action, list when known.
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(2U, __LINE__)); // 9 {[1,2],[4,10]}
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(4U, __LINE__)); // 8 {[1,2],[4,5],[7,10]}
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(5U, __LINE__)); // 7 {[1,2],[4,5],[7,7],[9,10]}
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(0U, __LINE__)); // 6 {[2,2],[4,5],[7,7],[9,10]}
  ASSERT_NO_FATAL_FAILURE(GTestFatalGetCheckRecord(__LINE__));    // 7 If 1 is returned, back to the previous.
  ASSERT_NO_FATAL_FAILURE(GTestFatalGetCheckRecord(__LINE__));    // 8 If 3 is returned, merge [1,2] and [4,5].
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(2U, __LINE__)); // 7 If as above, split what was just merged.
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(3U, __LINE__)); // 6
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(1U, __LINE__)); // 5
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(0U, __LINE__)); // 4
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(0U, __LINE__)); // 3
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(2U, __LINE__)); // 2
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(1U, __LINE__)); // 1
  ASSERT_NO_FATAL_FAILURE(GTestFatalReleaseRecord(0U, __LINE__)); // 0
  ASSERT_NO_FATAL_FAILURE(GTestFatalGetCheckRecord(__LINE__));
}

// TEST(IdManager, MaxIdException)
// {
//   as_components::IdManager<int> id_manager {};
//   for(int i {0}; i < std::numeric_limits<int>::max(); ++i)
//   {
//     id_manager.GetId();
//     EXPECT_EQ(id_manager.NumberOfUsedIds(), i + 1);
//   }
//   EXPECT_THROW(id_manager.GetId(), std::exception);
// }
