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

#include "fcgi/test/test/include/client_interface_testing_utilities.h"

#include <signal.h>
#include <time.h>

#include <atomic>

namespace as_components {
namespace fcgi {
namespace test {
namespace test {

std::atomic<bool> server_accept_timeout {false};
struct sigevent sev
{
  /* sigev_notify        = */ SIGEV_SIGNAL,
  /* sigev_signo         = */ SIGUSR2,
  /* sigev_value (union) = */ 0,
  /* _sigev_un           = */ 0 // system data set to default
};
timer_t accept_requests_block_escape_timer_id {CreateRealtimePosixTimer(&sev)};

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components
