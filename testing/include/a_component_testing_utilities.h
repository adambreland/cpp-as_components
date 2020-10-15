#ifndef A_COMPONENT_TESTING_INCLUDE_A_COMPONENT_TESTING_UTILITIES_H_
#define A_COMPONENT_TESTING_INCLUDE_A_COMPONENT_TESTING_UTILITIES_H_

#include <dirent.h>

#include <utility>
#include <vector>

namespace a_component {
namespace testing {

class FileDescriptorLeakChecker
{
 public:
  using iterator       = std::vector<int>::iterator;
  using const_iterator = std::vector<int>::const_iterator;

  void Reinitialize();

  inline std::pair<FileDescriptorLeakChecker::const_iterator, 
    FileDescriptorLeakChecker::const_iterator> Check()
  {
    return CheckHelper(recorded_list_);
  }

  template<typename It>
  std::pair<FileDescriptorLeakChecker::const_iterator, 
  FileDescriptorLeakChecker::const_iterator> Check(It removed_begin, 
    It removed_end, It added_begin, It added_end);

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
  void CleanUp(DIR* dir_stream_ptr);
  DIR* CreateDirectoryStream();
  void RecordDescriptorList(DIR* dir_stream_ptr, std::vector<int>* list_ptr);
  std::pair<FileDescriptorLeakChecker::const_iterator, 
    FileDescriptorLeakChecker::const_iterator> 
    CheckHelper(const std::vector<int>& expected_list);

  // After construction, a sorted list of unique integers.
  std::vector<int> recorded_list_ {};
  std::vector<int> leak_list_ {};
};

} // namespace testing
} // namespace a_component

#include "include/a_component_testing_utilities_templates.h"

#endif // A_COMPONENT_TESTING_INCLUDE_A_COMPONENT_TESTING_UTILITIES_H_
