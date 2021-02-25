#include "test/test/include/client_interface_testing_utilities.h"

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
