#ifndef A_COMPONENT_TESTING_INCLUDE_A_COMPONENT_TESTING_UTILITIES_H_
#define A_COMPONENT_TESTING_INCLUDE_A_COMPONENT_TESTING_UTILITIES_H_

#include <dirent.h>

#include <utility>
#include <vector>

namespace a_component {
namespace testing {

// FileDescriptorLeakChecker allows a recorded set of open file descriptors
// to be compared to the current set of open descriptors. Descriptors which are
// present when not expected (leaks) or which are not present when they are
// expected (unexpected closures) are reported. This functionality is useful
// when testing modules for file descriptor leaks.
//
// FileDescriptorLeakChecker offers:
// 1) The ability to reinitialize the recorded set of descriptors.
// 2) The ability to specify descriptors which were closed and which were opened
//    during execution. Specified descriptors will not be counted as leaks when
//    the appropriate call to Check is made.
// 3) Safe use across forks. After a fork, checker instances share no state.
//
// Limitations:
// 1) File description identity is not taken into account when checking for
//    leaks. If a recorded file descriptor is closed and a subsequent open
//    operation has reused the descriptor when a check is performed, then a
//    leak is not reported.
// 2) FileDescriptorLeakChecker depends on the /proc/<process id>/fd
//    directories.
// 3) FileDescriptorLeakChecker is not suitable for use in situations where
//    file descriptors may be opened or closed by external modules during the
//    execution of its methods. As such, it is likely unsuitable for use
//    in multithreaded programs.
class FileDescriptorLeakChecker
{
 public:
  using iterator       = std::vector<int>::iterator;
  using const_iterator = std::vector<int>::const_iterator;

  // Attempts to discards the previously recorded set of open descriptors and
  // record the current set.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Throws std::system_error if a directory stream could not be opened by
  //    a call to opendir.
  // 3) Strong exception guarantee.
  void Reinitialize();

  // Compares the recorded set of open descriptors to the current set.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Throws std::system_error if a directory stream could not be opened by
  //    a call to opendir.
  // 3) The recorded descriptor set is not modified in the event of a throw.
  //
  // Effects:
  // 1) Returns a pair p of iterators to the list of leaked descriptors found
  //    during the check. p.first refers to the start of the list. p.second
  //    refers to one past the last item of the list.
  // 2) A descriptor is regarded as leaked if it is currently present and
  //    was not present when the saved set was recorded or if it was present
  //    when the saved set was recorded and is not currently present.
  inline std::pair<const_iterator, const_iterator> Check()
  {
    return CheckHelper(recorded_list_);
  }

  // As for Check, but removed and added descriptors can be specified.
  //
  // Parameters:
  // [removed_begin, removed_end) and [added_begin, added_end) are ranges of
  // int values (regardless of cv-qualifiers or reference types). The ranges
  // need neither be ordered nor free of duplicates.
  //
  // Preconditions:
  // 1) [removed_begin, removed_end) and [added_begin, added_end) are valid
  //    ranges.
  //
  // Exceptions: as for Check.
  //
  // Effects:
  // 1) As for Check, except that descriptors are regarded as leaked
  //    according to the following semantic procedure:
  //    a) A copy C of the record descriptor set is made.
  //    b) The descriptors given by [removed_begin, removed_end) are removed
  //       from C (set minus).
  //    c) The descriptors given by [added_begin, added_end) are added to C
  //       (set union).
  //    d) C is compared to the recorded descriptor set as if it was the
  //       current descriptor set given the semantics of Check.
  template<typename It1, typename It2>
  std::pair<const_iterator, const_iterator>
  Check(It1 removed_begin, It1 removed_end, It2 added_begin, It2 added_end);

  // Records the set of file descriptors which are open during construction.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Throws std::system_error if a directory stream could not be opened by
  //    a call to opendir.
  inline FileDescriptorLeakChecker()
  {
    Reinitialize();
  }

  FileDescriptorLeakChecker(const FileDescriptorLeakChecker&) = default;
  FileDescriptorLeakChecker(FileDescriptorLeakChecker&&) = default;

  FileDescriptorLeakChecker& operator=(const FileDescriptorLeakChecker&)
    = default;
  FileDescriptorLeakChecker& operator=(FileDescriptorLeakChecker&&)
    = default;

  ~FileDescriptorLeakChecker() = default;

 private:
  template<typename It>
  void CopySortRemoveDuplicates(std::vector<int>* vec_cont, It begin_it,
    It end_it);
  DIR* CreateDirectoryStream();
  void RecordDescriptorList(DIR* dir_stream_ptr, std::vector<int>* list_ptr);
  std::pair<const_iterator, const_iterator>
  CheckHelper(const std::vector<int>& expected_list);

  // After construction, a sorted list of unique integers.
  std::vector<int> recorded_list_ {};
  // leak_list_ is used as a location to store the leaked descriptors
  // so that iterators can be returned by Check.
  std::vector<int> leak_list_     {};
};

} // namespace testing
} // namespace a_component

#include "include/a_component_testing_utilities_templates.h"

#endif // A_COMPONENT_TESTING_INCLUDE_A_COMPONENT_TESTING_UTILITIES_H_
