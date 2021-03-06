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

#ifndef AS_COMPONENTS_TESTING_INCLUDE_AS_COMPONENTS_TESTING_UTILITIES_TEMPLATES_H_
#define AS_COMPONENTS_TESTING_INCLUDE_AS_COMPONENTS_TESTING_UTILITIES_TEMPLATES_H_

#include "testing/include/as_components_testing_utilities.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <vector>

namespace as_components {
namespace testing {

template<typename It>
void FileDescriptorLeakChecker::CopySortRemoveDuplicates
(
  std::vector<int>* vec_cont,
  It begin_it,
  It end_it
)
{
  while(begin_it != end_it)
  {
    vec_cont->push_back(*begin_it);
    ++begin_it;
  }
  std::sort(vec_cont->begin(), vec_cont->end());
  std::vector<int>::iterator new_end
    {std::unique(vec_cont->begin(), vec_cont->end())};
  vec_cont->erase(new_end, vec_cont->end());
};

template<typename It1, typename It2>
std::pair<FileDescriptorLeakChecker::const_iterator, 
          FileDescriptorLeakChecker::const_iterator>
FileDescriptorLeakChecker:: 
Check(It1 removed_begin, It1 removed_end, It2 added_begin, It2 added_end)
{
  // Assert that each iterator type refers to int.
  static_assert(
    std::is_same<typename
      std::remove_cv<typename
        std::remove_reference<
          decltype(*removed_begin)
        >::type
      >::type,
      int
    >::value
  );
  static_assert(
    std::is_same<typename
      std::remove_cv<typename
        std::remove_reference<
          decltype(*added_begin)
        >::type
      >::type,
      int
    >::value
  );

  // Process the removed and added iterator lists.
  std::vector<int> removed {};
  std::vector<int> added   {};
  CopySortRemoveDuplicates(&removed, removed_begin, removed_end);
  CopySortRemoveDuplicates(&added, added_begin, added_end);
  std::vector<int> difference_list {};
  std::set_difference(recorded_list_.begin(), recorded_list_.end(),
    removed.begin(), removed.end(), 
    std::back_insert_iterator<std::vector<int>>(difference_list));
  std::vector<int> expected_list {};
  std::set_union(difference_list.begin(), difference_list.end(),
    added.begin(), added.end(), 
    std::back_insert_iterator<std::vector<int>>(expected_list));

  return CheckHelper(expected_list);
}

} // namespace testing
} // namespace as_components

#endif // AS_COMPONENTS_TESTING_INCLUDE_AS_COMPONENTS_TESTING_UTILITIES_TEMPLATES_H_
