#include "include/a_component_testing_utilities.h"

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace a_component {
namespace testing {

std::pair<FileDescriptorLeakChecker::const_iterator, 
          FileDescriptorLeakChecker::const_iterator> 
FileDescriptorLeakChecker::
CheckHelper(const std::vector<int>& expected_list)
{
  std::vector<int> current_list {};
  std::vector<int> new_leak_list {};
  DIR* dir_stream_ptr {CreateDirectoryStream()};
  try
  {
    // Add the symmetric difference of expected_list and current_list
    // to new_leak_list. The symmetric difference is partitioned into
    // descriptors which are present when they are not expected (leaks)
    // and descriptors which are not present when they are expected (spurious
    // closures.)
    RecordDescriptorList(dir_stream_ptr, &current_list);
    std::set_symmetric_difference(expected_list.begin(), expected_list.end(),
      current_list.begin(), current_list.end(), 
      std::back_insert_iterator<std::vector<int>>(new_leak_list));
  }
  catch(...)
  {
    CleanUp(dir_stream_ptr);
    throw;
  }
  CleanUp(dir_stream_ptr);
  new_leak_list.swap(leak_list_);
  return std::make_pair(leak_list_.begin(), leak_list_.end());
}

void FileDescriptorLeakChecker::CleanUp(DIR* dir_stream_ptr)
{
  if(closedir(dir_stream_ptr) == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "closedir"};
  }
}

DIR* FileDescriptorLeakChecker::CreateDirectoryStream()
{
  // Retrieve the process ID to identify the correct folder in the proc
  // filesystem.
  pid_t pid {getpid()};
  std::string descriptor_path {"/proc/"};
  (descriptor_path += std::to_string(pid)) += "/fd";
  DIR* dir_stream_ptr {opendir(descriptor_path.data())};
  if(!dir_stream_ptr)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "opendir"};
  }
  return dir_stream_ptr;
}

void FileDescriptorLeakChecker::RecordDescriptorList(DIR* dir_stream_ptr,
  std::vector<int>* list_ptr)
{
  errno = 0;
  while(struct dirent* entry_ptr {readdir(dir_stream_ptr)})
    list_ptr->push_back(std::atoi(entry_ptr->d_name));
  if(errno != 0)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "readdir"};
  }
  std::sort(list_ptr->begin(), list_ptr->end());
  // Integer uniqueness is assumed based on the organization of /proc/<PID>/fd.
}

void FileDescriptorLeakChecker::Reinitialize()
{
  std::vector<int> new_list {};
  std::vector<int> new_leak_list {};
  DIR* dir_stream_ptr {CreateDirectoryStream()}; // Always non-null.
  try
  {
    RecordDescriptorList(dir_stream_ptr, &new_list);
  }
  catch(...)
  {
    CleanUp(dir_stream_ptr);
    throw;
  }
  CleanUp(dir_stream_ptr);
  recorded_list_.swap(new_list);
  leak_list_.swap(new_leak_list);
}

} // namespace testing
} // namespace a_component
