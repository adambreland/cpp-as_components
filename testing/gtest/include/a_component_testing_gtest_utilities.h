#ifndef A_COMPONENT_TESTING_GTEST_INCLUDE_A_COMPONENT_TESTING_GTEST_UTILITIES_H_
#define A_COMPONENT_TESTING_GTEST_INCLUDE_A_COMPONENT_TESTING_GTEST_UTILITIES_H_

#include <sys/signal.h>

#include <string>

#include "include/a_component_testing_utilities.h"

namespace a_component {
namespace testing {
namespace gtest {

//    Creates a temporary file in the temporary directory offered by Bazel
// and returns a descriptor for the file. The file was unlinked after being
// created. Failures are reported as Google Test fatal failures. In this case,
// the function does not return.
//    Note that a pointer is used as functions which may produce fatal Google
// Test failures must return void.
//
// BAZEL DEPENDENCY: TEST_TMPDIR environment variable.
void GTestFatalCreateBazelTemporaryFile(int* descriptor_ptr);

extern "C" using CSignalHandlerType = void (*)(int);

void GTestFatalSetSignalDisposition(int sig, CSignalHandlerType handler);

inline void GTestFatalIgnoreSignal(int sig)
{
  return GTestFatalSetSignalDisposition(sig, SIG_IGN);
}

inline void GTestFatalRestoreSignal(int sig)
{
  return GTestFatalSetSignalDisposition(sig, SIG_DFL);
}

void GTestNonFatalCheckAndReportDescriptorLeaks(
  FileDescriptorLeakChecker* fdlc_ptr,  const std::string& test_name);

// Truncate a file to zero length and seek to the beginning.
//
// Failures are reported as Google Test non-fatal failures.
bool GTestNonFatalPrepareTemporaryFile(int descriptor);

} // namespace gtest
} // namespace testing
} // namespace a_component

#endif // A_COMPONENT_TESTING_GTEST_INCLUDE_A_COMPONENT_TESTING_GTEST_UTILITIES_H_
