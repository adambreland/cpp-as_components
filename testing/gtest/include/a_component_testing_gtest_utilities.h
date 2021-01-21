#ifndef A_COMPONENT_TESTING_GTEST_INCLUDE_A_COMPONENT_TESTING_GTEST_UTILITIES_H_
#define A_COMPONENT_TESTING_GTEST_INCLUDE_A_COMPONENT_TESTING_GTEST_UTILITIES_H_

#include <sys/signal.h>

#include <string>

#include "external/googletest/googletest/include/gtest/gtest.h"

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
void GTestFatalCreateBazelTemporaryFile(int* descriptor_ptr,
  int invocation_line);

extern "C" using CSignalHandlerType = void (*)(int);

// Note that the special values SIG_IGN (ignore signal) and SIG_DFL (restore
// default disposition) are of type CSignalHandlerType. Convenience wrappers
// are provided for these values below.
void GTestFatalSetSignalDisposition(int sig, CSignalHandlerType handler,
  int invocation_line);

inline void GTestFatalIgnoreSignal(int sig, int invocation_line)
{
  ::testing::ScopedTrace tracer {"", invocation_line,
    "GTestFatalIgnoreSignal"};
  ASSERT_NO_FATAL_FAILURE(GTestFatalSetSignalDisposition(sig, SIG_IGN, __LINE__));
}

inline void GTestFatalRestoreSignal(int sig, int invocation_line)
{
  ::testing::ScopedTrace tracer {"", invocation_line,
    "GTestFatalRestoreSignal"};
  ASSERT_NO_FATAL_FAILURE(GTestFatalSetSignalDisposition(sig, SIG_DFL, __LINE__));
}

void GTestNonFatalCheckAndReportDescriptorLeaks(
  FileDescriptorLeakChecker* fdlc_ptr,  const std::string& test_name,
  int invocation_line);

// Truncate a file to zero length and seek to the beginning.
//
// Failures are reported as Google Test non-fatal failures.
bool GTestNonFatalPrepareTemporaryFile(int descriptor, int invocation_line);

} // namespace gtest
} // namespace testing
} // namespace a_component

#endif // A_COMPONENT_TESTING_GTEST_INCLUDE_A_COMPONENT_TESTING_GTEST_UTILITIES_H_
