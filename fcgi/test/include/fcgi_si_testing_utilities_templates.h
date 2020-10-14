#ifndef FCGI_SI_TEST_TEST_FCGI_SI_TESTING_UTILITIES_TEMPLATES_H_
#define FCGI_SI_TEST_TEST_FCGI_SI_TESTING_UTILITIES_TEMPLATES_H_

#include "test/include/fcgi_si_testing_utilities.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace a_component {
namespace fcgi {
namespace test {

template<typename It>
std::pair<FileDescriptorLeakChecker::const_iterator, 
          FileDescriptorLeakChecker::const_iterator>
FileDescriptorLeakChecker:: 
Check(It removed_begin, It removed_end, It added_begin, It added_end)
{  
  // Process the removed and added iterator lists.
  std::vector<int> removed {};
  std::vector<int> added   {};
  auto CopySortRemoveDuplicates = [](std::vector<int>* vec_cont, It begin_it,
    It end_it)->void
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

} // namsepace test
} // namespace fcgi
} // namespace a_component

#endif // FCGI_SI_TEST_TEST_FCGI_SI_TESTING_UTILITIES_TEMPLATES_H_
