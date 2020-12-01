#ifndef A_COMPONENT_FCGI_TEST_TEST_INCLUDE_CLIENT_INTERFACE_TESTING_UTILITIES_H_
#define A_COMPONENT_FCGI_TEST_TEST_INCLUDE_CLIENT_INTERFACE_TESTING_UTILITIES_H_

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "external/a_component_testing/include/a_component_testing_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"

#include "server_interface_combined.h"
#include "test/include/fcgi_si_testing_utilities.h"
#include "test/include/test_fcgi_client_interface.h"

namespace a_component {
namespace fcgi {
namespace test {
namespace test {

struct ClientInterfaceConnectionOnlyObserverValues
{
  int         connection;
  std::size_t connection_completed_request_count;
  int         connection_count;
  bool        is_connected;
  std::size_t management_request_count;
  std::size_t connection_pending_request_count;
  std::size_t ready_event_count;
};

void GTestFatalClientInterfaceConnectionOnlyObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceConnectionOnlyObserverValues& values,
  int invocation_line);

struct ClientInterfaceObserverValues
{
  struct ClientInterfaceConnectionOnlyObserverValues co;
  std::size_t total_completed_request_count;
  std::size_t total_pending_request_count;
};

void GTestFatalClientInterfaceObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceObserverValues& values,
  int invocation_line);

std::atomic<bool> server_accept_timeout
  {false};

// A Google Test test fixture.
// This test fixture:
// 1) Performs file descriptor leak checking.
// 2) Allows pairs of a socket descriptor and a Unix socket file to be
//    registered for closure and removal, respectively, during clean up.
//    The file is described by a const char* to the file path. A dummy
//    descriptor does not cause an error during closure.
// 3) Sets SIGPIPE to be ignored,
// 4) Establishes SigAlrmHandler for SIGALRM.
// 5) Restores the default dispositions for SIGPIPE and SIGALRM during clean
//    up.
class TestFcgiClientInterfaceTestFixture : public ::testing::Test
{
 protected:
  void SetUp() override;

  void TearDown() override;

  std::vector<std::pair<int, const char*>> resource_list_ {};
  testing::FileDescriptorLeakChecker fdlc_ {};
};

constexpr const struct InterfaceCreationArguments kDefaultInterfaceArguments
{
  /* domain          = */ AF_UNSPEC,
  /* backlog         = */ 5,
  /* max_connections = */ 10,
  /* max_requests    = */ 100,
  /* app_status      = */ EXIT_FAILURE,
  /* unix_path       = */ nullptr,
};

// Maps for testing which match the default server interface arguments.
const ParamsMap kNameOnlyMap
{
  {FCGI_MAX_CONNS, {}},
  {FCGI_MAX_REQS, {}},
  {FCGI_MPXS_CONNS, {}}
};
const ParamsMap kMapWithValues
{
  {FCGI_MAX_CONNS,  {'1', '0'}},
  {FCGI_MAX_REQS,   {'1', '0', '0'}},
  {FCGI_MPXS_CONNS, {'1'}}
};
const ParamsMap kMpxsNameMap
{
  {FCGI_MPXS_CONNS, {}}
};
const ParamsMap kMpxsMapWithValue
{
  {FCGI_MPXS_CONNS, {'1'}}
};

// AF_UNIX files cannot be created in the Bazel temporary file directory
// because its name is too long.
constexpr const char*const kUnixPath1
  {"/tmp/TestFcgiClientInterfaceTesting_AF_UNIX_1"};
constexpr const char*const kUnixPath2
  {"/tmp/TestFcgiClientInterfaceTesting_AF_UNIX_2"};

void GTestFatalCheckGetValuesResult(
  const GetValuesResult* gvr_ptr,
  bool                   corrupt,
  int                    connection,
  const ParamsMap&       request_map,
  const ParamsMap&       response_map,
  int                    invocation_line);

void ChildServerAlrmRestoreAndSelfKillSet();

// All exercise application requests will have shared FCGI_PARAMS values.
// The values are arbitrary.
const ParamsMap kSharedExerciseParams
{
  {
    {'A'}, {'1'}
  },
  {
    {'B'}, {'2'}
  },
  {
    {'C'}, {'3'}
  }
};

const std::vector<std::uint8_t> kStdinDataForClientExercise {0, 1, 2, 3, 4};
const std::vector<std::uint8_t> kFcgiDataForClientExercise  {5, 6, 7, 8, 9};

const struct FcgiRequestDataReference kExerciseDataRef
{
  /* role           = */  FCGI_RESPONDER,
  /* keep_conn      = */  true,
  /* params_map_ptr = */ &kSharedExerciseParams,
  /* stdin_begin    = */  kStdinDataForClientExercise.data(),
  /* stdin_end      = */  kStdinDataForClientExercise.data() +
                          kStdinDataForClientExercise.size(),
  /* data_begin     = */  kFcgiDataForClientExercise.data(),
  /* data_end       = */  kFcgiDataForClientExercise.data() +
                          kFcgiDataForClientExercise.size()
};

constexpr FcgiType kUnknownTypeForExercise {static_cast<FcgiType>(64)};
const std::vector<std::uint8_t> kDataForUnknownBinaryRequest {2U, 4U};

// This function is used in the implementation of ConnectCase1.
void GTestFatalSendExerciseRequests(
  TestFcgiClientInterface*                            client_inter_ptr,
  const struct FcgiRequestDataReference&              exercise_data_ref,
  int                                                 application_request_count,
  struct ClientInterfaceConnectionOnlyObserverValues* observer_ptr,
  std::size_t*                                        total_completed_ptr,
  std::size_t*                                        total_pending_ptr,
  std::set<FcgiRequestIdentifier>*                    request_set_ptr,
  int                                                 invocation_line);

void GTestNonFatalStreamDataComparison
(
  const std::uint8_t*              reference_stream_begin,
  const std::uint8_t*              reference_stream_end,
  const std::vector<std::uint8_t>& response_stream,
  int                              invocation_line);

void GTestNonFatalExerciseResponseCompare(
  const struct FcgiRequestDataReference sent_request_ref,
  FcgiResponse* app_response_ptr,
  int invocation_line);

constexpr const struct itimerval kTimeout
{
  {0, 0},   // it_interval (don't repeat)
  {0, 10000} // it_value (wait 10 ms)
};

// The following functions facilitate the processing of management and
// application requests by FastCGI test server instances.
//    The loop until AcceptRequests blocks was found to be needed as a single
// call to AcceptRequests does not necessarily read all of the information
// of a request. A timer and its associated SIGALRM signal are used to break
// out of the blocked AcceptRequests call and the loop.
//    Note that this implementation also tests the specified behavior of
// AcceptRequests when it has blocked in an I/O multiplexing call and the
// call fails with errno == EINTR.
//    Any FcgiRequest objects which remain after op is called are cleared
// before another call to AcceptRequests is made.

// Parameters
// op: a) The template parameter op is a callable entity which takes
//        std::vector<FcgiRequest>* and returns void.
//     b) If a lambda function is used, its type should be hidden by being
//        wrapped in std::function<void(std::vector<FcgiRequest>*)>.
template <typename Func>
void GTestFatalServerAcceptLoop(FcgiServerInterface* inter_ptr,
  Func op, int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalServerAcceptLoop"};
  std::vector<FcgiRequest> accept_buffer {};
  server_accept_timeout.store(false);
  ASSERT_NE(setitimer(ITIMER_REAL, &kTimeout, nullptr), -1) <<
    std::strerror(errno);
  while(!(server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_buffer = inter_ptr->AcceptRequests());
    ASSERT_NO_THROW(ASSERT_NO_FATAL_FAILURE(op(&accept_buffer)));
    accept_buffer.clear();
  }
}

void OperationForExpectNone(std::vector<FcgiRequest>* accept_buffer_ptr);

void GTestFatalAcceptRequestsExpectNone(FcgiServerInterface* inter_ptr,
  int invocation_line);

void GTestFatalAcceptRequestsRequestEcho(FcgiServerInterface* inter_ptr,
  const ParamsMap& sent_environ, int invocation_line);

// A helper function to reap a child process which was created by the test
// process during testing.
//
//
void GTestFatalTerminateChild(pid_t child_id, int invocation_line);

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component

#endif // A_COMPONENT_FCGI_TEST_TEST_INCLUDE_CLIENT_INTERFACE_TESTING_UTILITIES_H_
