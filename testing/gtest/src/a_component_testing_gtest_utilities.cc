#include "gtest/include/a_component_testing_gtest_utilities.h"

#include <stdlib.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "external/googletest/googletest/include/gtest/gtest.h"

namespace a_component {
namespace testing {
namespace gtest {

void GTestFatalCreateBazelTemporaryFile(int* descriptor_ptr)
{
  if(!descriptor_ptr)
    FAIL() << "descriptor_ptr was null.";
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
      '\n' << std::strerror(errno);
  if(unlink(char_array_uptr.get()) < 0)
  {
    // Retrieve the errno error string before calling close.
    std::string errno_message {strerror(errno)};
    close(temp_descriptor);
    FAIL() << "The temporary file could not be unlinked." << '\n' <<
      errno_message;
  }
  *descriptor_ptr = temp_descriptor;
}

void GTestFatalSetSignalDisposition(int sig, CSignalHandlerType handler)
{
  sigset_t sigset {};
  if(sigemptyset(&sigset) == -1)
  {
    FAIL() << "A call to sigempty set from a call to "
      "GTestFatalSetSignalDisposition failed." << '\n' << std::strerror(errno);
  }
  struct sigaction sa {};
  sa.sa_handler = handler;
  sa.sa_mask    = sigset;
  sa.sa_flags   = 0;
  if(sigaction(sig, &sa, nullptr) == -1)
  {
    FAIL() << "A call to sigaction from a call to "
      "GTestFatalSetSignalDisposition failed." << '\n' << std::strerror(errno);
  }
  return;
}

void GTestNonFatalCheckAndReportDescriptorLeaks(
  FileDescriptorLeakChecker* fdlc_ptr, 
  const std::string& test_name)
{
  std::pair<FileDescriptorLeakChecker::const_iterator, 
    FileDescriptorLeakChecker::const_iterator> iter_pair 
    {fdlc_ptr->Check()};
  if(iter_pair.first != iter_pair.second)
  {
    std::string message {"File descriptors were leaked in "};
    message.append(test_name).append(": ");
    for(/*no-op*/; iter_pair.first != iter_pair.second; ++(iter_pair.first))
    {
      message.append(std::to_string(*(iter_pair.first))).append(" ");
    } 
    ADD_FAILURE() << message;
  }
}

bool GTestNonFatalPrepareTemporaryFile(int descriptor)
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

} // namespace gtest
} // namespace testing
} // namespace a_component
