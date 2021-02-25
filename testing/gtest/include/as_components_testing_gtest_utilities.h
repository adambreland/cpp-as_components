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

#ifndef AS_COMPONENTS_TESTING_GTEST_INCLUDE_AS_COMPONENTS_TESTING_GTEST_UTILITIES_H_
#define AS_COMPONENTS_TESTING_GTEST_INCLUDE_AS_COMPONENTS_TESTING_GTEST_UTILITIES_H_

#include <sys/signal.h>

#include <string>

#include "googletest/include/gtest/gtest.h"

#include "include/as_components_testing_utilities.h"

namespace as_components {
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
} // namespace as_components

#endif // AS_COMPONENTS_TESTING_GTEST_INCLUDE_AS_COMPONENTS_TESTING_GTEST_UTILITIES_H_
