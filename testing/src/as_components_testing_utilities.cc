#include "include/as_components_testing_utilities.h"

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <iterator>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace as_components {
namespace testing {

static_assert(std::is_nothrow_swappable<std::vector<int>>::value);

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
    // and descriptors which are not present when they are expected (unexpected
    // closures.)
    RecordDescriptorList(dir_stream_ptr, &current_list);
    std::set_symmetric_difference(expected_list.begin(), expected_list.end(),
      current_list.begin(), current_list.end(), 
      std::back_insert_iterator<std::vector<int>>(new_leak_list));
  }
  catch(...)
  {
    closedir(dir_stream_ptr);
    throw;
  }
  closedir(dir_stream_ptr);
  new_leak_list.swap(leak_list_);
  return std::make_pair(leak_list_.begin(), leak_list_.end());
}

DIR* FileDescriptorLeakChecker::CreateDirectoryStream()
{
  // Retrieve the process ID to identify the correct folder in the proc
  // filesystem. This value can't be stored as the process ID may change due to
  // a fork.
  pid_t pid {getpid()};
  std::string descriptor_path {"/proc/"};
  descriptor_path.append(std::to_string(pid)).append("/fd");
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
  // The resulting descriptor list is sorted, duplicates are removed, and the
  // descriptor for the directory stream is removed.
  std::vector<int>::iterator begin_iter     {list_ptr->begin()};
  std::vector<int>::iterator first_end_iter {list_ptr->end()};
  std::sort(begin_iter, first_end_iter);
  std::vector<int>::iterator new_end
    {std::unique(begin_iter, first_end_iter)};
  list_ptr->erase(new_end, first_end_iter); // new_end is invalidated.
  if(!list_ptr->size())
  {
    throw std::logic_error {"An error occurred while processing descriptors "
      "in a call to a method of FileDescriptorLeakChecker. None was present."};
  }
  // Remove the descriptor from the directory stream.
  int directory_fd {dirfd(dir_stream_ptr)};
  if(directory_fd == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "dirfd"};
  }
  // begin_iter must be valid here as size() != 0.
  std::vector<int>::iterator second_end_iter {list_ptr->end()};
  std::vector<int>::iterator directory_fd_iter
    {std::lower_bound(begin_iter, second_end_iter, directory_fd)};
  if((directory_fd_iter  == second_end_iter) ||
     (*directory_fd_iter != directory_fd))
  {
    throw std::logic_error {"The descriptor for the internal directory "
      "stream was not found in a call to a method of "
      "FileDescriptorLeakChecker."};
  }
  list_ptr->erase(directory_fd_iter);
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
    // Since dir_stream_ptr was just opened above and a throw would have
    // occurred if an error occurred during stream opening, it is assumed
    // here that closure cannot fail.
    closedir(dir_stream_ptr);
    throw;
  }
  closedir(dir_stream_ptr); // The same assumption is made here as above.
  recorded_list_.swap(new_list);
  leak_list_.swap(new_leak_list);
}

} // namespace testing
} // namespace as_components
