#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/protocol_constants.h"
#include "test/fcgi_si_testing_utilities.h"

// Key:
// BAZEL DEPENDENCY  This marks a feature which is provided by the Bazel
//                   testing run-time environment. 

// Utility functions testing fcgi_si.
namespace fcgi_si_test {

void CreateBazelTemporaryFile(int* descriptor_ptr)
{
  static const char* tmpdir_ptr {std::getenv("TEST_TMPDIR")};
  if(!tmpdir_ptr)
    FAIL() << "The directory for temporary files supplied by Bazel is missing.";
  std::string temp_template {tmpdir_ptr};
  temp_template += "/fcgi_si_TEST_XXXXXX";
  std::unique_ptr<char []> char_array_uptr {new char[temp_template.size() + 1]};
    // Initialize the new character array.
  std::memcpy(char_array_uptr.get(), temp_template.data(), 
    temp_template.size() + 1);
  int temp_descriptor {mkstemp(char_array_uptr.get())};
  if(temp_descriptor < 0)
    FAIL() << "An error occurred while trying to create a temporary file." <<
      '\n' << strerror(errno);
  if(unlink(char_array_uptr.get()) < 0)
  {
    std::string errno_message {strerror(errno)};
    close(temp_descriptor);
    FAIL() << "The temporary file could not be unlinked." << '\n' <<
      errno_message;
  }
  *descriptor_ptr = temp_descriptor;
}

bool PrepareTemporaryFile(int descriptor)
{
  // Truncate the temporary file and seek to the beginning.
  if(ftruncate(descriptor, 0) < 0)
  {
    ADD_FAILURE() << "A call to ftruncate failed.";
    return false;
  }
  off_t lseek_return {lseek(descriptor, 0, SEEK_SET)};
  if(lseek_return < 0)
  {
    ADD_FAILURE() << "A call to lseek failed.";
    return false;
  }
  return true;
}

std::tuple<bool, bool, bool, bool, std::vector<std::uint8_t>>
ExtractContent(int fd, fcgi_si::FCGIType type, std::uint16_t id)
{
  constexpr std::uint16_t buffer_size {1 << 10};
  std::uint8_t byte_buffer[buffer_size];

  std::uint32_t local_offset {0};
  ssize_t number_bytes_read {0};
  std::uint8_t local_header[fcgi_si::FCGI_HEADER_LEN];
  int header_bytes_read {0};
  std::vector<uint8_t> content_bytes {};
  std::uint16_t FCGI_id {};
  std::uint16_t content_length {0};
  std::uint16_t content_bytes_read {0};
  std::uint8_t padding_length {0};
  std::uint8_t padding_bytes_read {0};
  bool read_error {false};
  bool header_error {false};
  bool sequence_terminated {false};
  bool aligned {true};
  int state {0};

  while((number_bytes_read = read(fd, byte_buffer, buffer_size)))
  {
    if(number_bytes_read == -1)
    {
      if(errno == EINTR)
        continue;
      else
      {
        read_error = true;
        break;
      }
    }

    local_offset = 0;
    while(local_offset < number_bytes_read)
    {
      // Note that, below, the condition "section_bytes_read < expected_amount"
      // implies that local_offset == number_bytes_read.
      switch(state) {
        case 0 : {
          if(header_bytes_read < fcgi_si::FCGI_HEADER_LEN)
          {
            // Safe narrowing as this can never exceed FCGI_HEADER_LEN.
            int header_bytes_to_copy(std::min<ssize_t>(fcgi_si::FCGI_HEADER_LEN
              - header_bytes_read, number_bytes_read - local_offset));
            std::memcpy((void*)(local_header + header_bytes_read),
              (void*)(byte_buffer + local_offset), header_bytes_to_copy);
            local_offset += header_bytes_to_copy;
            header_bytes_read += header_bytes_to_copy;
            if(header_bytes_read < fcgi_si::FCGI_HEADER_LEN)
              break;
          }
          // The header is complete and there are some bytes left to process.

          // Extract header information.
          (FCGI_id = local_header[fcgi_si::kHeaderRequestIDB1Index]) <<= 8; 
            // One byte.
          FCGI_id += local_header[fcgi_si::kHeaderRequestIDB0Index];
          (content_length = local_header[fcgi_si::kHeaderContentLengthB1Index]) 
            <<= 8;
          content_length += local_header[fcgi_si::kHeaderContentLengthB0Index];
          padding_length = local_header[fcgi_si::kHeaderPaddingLengthIndex];
          if((content_length + padding_length) % 8 != 0)
            aligned = false;
          // Verify header information.
          if(static_cast<fcgi_si::FCGIType>(
             local_header[fcgi_si::kHeaderTypeIndex]) != type 
             || (FCGI_id != id))
          {
            header_error = true;
            break;
          }
          if(content_length == 0)
          {
            sequence_terminated = true;
            break;
          }
          // Set or reset state.
          header_bytes_read = 0;
          state++;
          if(local_offset == number_bytes_read)
            break;
          // Fall through to start processing content.
        }
        case 1 : {
          if(content_bytes_read < content_length)
          {
            // Safe narrowing as this can never exceed content_length.
            std::uint16_t content_bytes_to_copy(std::min<ssize_t>(
              content_length - content_bytes_read, 
              number_bytes_read - local_offset));
            content_bytes.insert(content_bytes.end(), byte_buffer + local_offset,
              byte_buffer + local_offset + content_bytes_to_copy);
            local_offset += content_bytes_to_copy;
            content_bytes_read += content_bytes_to_copy;
            if(content_bytes_read < content_length)
              break;
          }
          // Set or reset state.
          content_bytes_read = 0;
          state++;
          if(local_offset == number_bytes_read)
            break;
          // Fall through to start processing padding.
        }
        case 2 : {
          if(padding_bytes_read < padding_length)
          {
            // Safe narrowing as this can never exceed padding_length.
            std::uint8_t padding_bytes_to_process(std::min<ssize_t>(
              padding_length - padding_bytes_read, 
              number_bytes_read - local_offset));
            local_offset += padding_bytes_to_process;
            padding_bytes_read += padding_bytes_to_process;
            if(padding_bytes_read < padding_length)
              break;
          }
          padding_bytes_read = 0;
          state = 0;
        }
      }
      if(read_error || header_error || sequence_terminated)
        break;
    }
    if(read_error || header_error || sequence_terminated)
      break;
  }
  // Check for incomplete record sections.
  // Note that, when no error is present and the sequence wasn't terminated
  // by a record with a zero content length, state represents a section which
  // is either incomplete or never began. It is expected that the sequence
  // ends with a header section that "never began."
  bool section_error {false};
  if(!(read_error || header_error || sequence_terminated))
  {
    switch(state) {
      case 0 : {
        if((0 < header_bytes_read) && 
           (header_bytes_read < fcgi_si::FCGI_HEADER_LEN))
          section_error = true;
        break;
      }
      case 1 : {
        if(content_bytes_read != content_length)
          section_error = true;
        break;
      }
      case 2 : {
        if(padding_bytes_read != padding_length)
          section_error = true;
        break;
      }
    }
  }

  return std::make_tuple(
    !read_error,
    !(header_error || section_error),
    sequence_terminated,
    (header_error || section_error) ? false : aligned,
    content_bytes
  );
}

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

} // namespace fcgi_si_test