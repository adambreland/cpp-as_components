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

#ifndef AS_COMPONENTS_FCGI_TEST_TEST_INCLUDE_CLIENT_INTERFACE_TESTING_UTILITIES_H_
#define AS_COMPONENTS_FCGI_TEST_TEST_INCLUDE_CLIENT_INTERFACE_TESTING_UTILITIES_H_

#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

#include "googletest/include/gtest/gtest.h"

#include "fcgi/include/fcgi_protocol_constants.h"
#include "fcgi/include/fcgi_request.h"
#include "fcgi/include/fcgi_request_identifier.h"
#include "fcgi/include/fcgi_server_interface.h"
#include "fcgi/include/fcgi_request_identifier.h"
#include "fcgi/test/include/fcgi_si_testing_utilities.h"
#include "fcgi/test/include/test_fcgi_client_interface.h"
#include "testing/include/as_components_testing_utilities.h"

namespace as_components {
namespace fcgi {
namespace test {
namespace test {

// Observer utility functions:
// 1) GTestFatalClientInterfaceConnectionOnlyObserverCheck
// 2) GTestNonFatalClientInterfaceInstanceObserverCheck
// 3) GTestFatalClientInterfaceObserverCheck
//
// The above functions exercise sets of the observers of
// TestFcgiClientInterface. The "ConnectionOnly" function only exercises the
// observers which take an int connection parameter. The "Instance" function
// exercises the observers which take no parameters. These observes are:
// 1) std::size_t CompletedRequestCount()
// 2) int         ConnectionCount()
// 3) std::size_t PendingRequestCount()
// 4) std::size_t ReadyEventCount()
// GTestFatalClientInterfaceObserverCheck invokes both of the previous
// observer utility functions.
//
// These utilities use observer value structures. Instances of these structures
// are initialized in a test and modified as appropriate as the expected state
// of TestFcgiClientInterface instances change.
//
// These functions are useful for a testing strategy where the observable state
// of an interface instance is verified:
// 1) On every occasion when a change in observable state is expected.
// 2) A change in observable state seems possible due to error and state
//    constancy is expected.
struct ClientInterfaceConnectionOnlyObserverValues
{
  int         connection;
  std::size_t connection_completed_request_count;
  bool        is_connected;
  std::size_t management_request_count;
  std::size_t connection_pending_request_count;
};

void GTestFatalClientInterfaceConnectionOnlyObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceConnectionOnlyObserverValues& values,
  int invocation_line);

struct ClientInterfaceInstanceObserverValues
{
  std::size_t total_completed_request_count;
  int         connection_count;
  std::size_t total_pending_request_count;
  std::size_t ready_event_count;
};

void GTestNonFatalClientInterfaceInstanceObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceInstanceObserverValues& values,
  int invocation_line);

struct ClientInterfaceObserverValues
{
  struct ClientInterfaceConnectionOnlyObserverValues co;
  struct ClientInterfaceInstanceObserverValues       in;
};

void GTestFatalClientInterfaceObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceObserverValues& values,
  int invocation_line);

// A Google Test test fixture.
// This test fixture:
// 1) Performs file descriptor leak checking.
// 2) Allows pairs of a socket descriptor and a Unix socket file to be
//    registered for closure and removal, respectively, during clean up.
//    The file is described by a const char* to the file path. A dummy
//    descriptor does not cause an error during closure.
// 3) Sets SIGPIPE to be ignored,
// 4) Establishes SigUsr2Handler for SIGUSR2.
// 5) Restores the default dispositions for SIGPIPE and SIGUSR2 during clean
//    up.
class TestFcgiClientInterfaceTestFixture : public ::testing::Test
{
 protected:
  void SetUp() override;

  void TearDown() override;

  std::vector<int>                   descriptor_resource_list_ {};
  std::vector<const char*>           path_resource_list_       {};
  testing::FileDescriptorLeakChecker fdlc_                     {};
};

// Default state for testing:
//    kDefaultInterfaceArguments is used with
// ::as_components::fcgi::test::GTestNonFatalCreateInterface to construct an
// FcgiServerInstance.
//    Note that this constant is meant to be used to initialize an
// InterfaceCreationArguments instance. The domain field of this instance must
// then be set to a specific domain. If that domain is AF_UNIX, the unix_path
// field must be set.
constexpr const struct InterfaceCreationArguments kDefaultInterfaceArguments
{
  /* domain          = */ AF_UNSPEC,
  /* backlog         = */ 5,
  /* max_connections = */ 10,
  /* max_requests    = */ 100,
  /* app_status      = */ EXIT_FAILURE,
  /* unix_path       = */ nullptr,
};

// Common state for testing:
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

// Populate a ManagementRequestData instance with a random byte sequence and
// an unknown management request type. This is used for testing
// SendBinaryManagementRequest.
const struct ManagementRequestData kBinaryRequest
{
  /* type       = */ static_cast<FcgiType>(27U),
  /* params_map = */ {},
  /* data       = */ {59U, 0U, 125U, 26U, 237U}
};

// Shared state for testing:
//    When AF_UNIX is used for an FcgiServerInterface instance, a file is
// needed to allow clients to connect to the interface.
//    The paths are in /tmp as AF_UNIX files cannot be created in the Bazel
// temporary file directory. Its name is too long and causes the file path
// limit enforced by the AF_UNIX socket implementation to be exceeded.
constexpr const char*const kUnixPath1
  {"/tmp/TestFcgiClientInterfaceTesting_AF_UNIX_1"};
constexpr const char*const kUnixPath2
  {"/tmp/TestFcgiClientInterfaceTesting_AF_UNIX_2"};

// Compares a result GetValuesResult instance to expected values for the
// instance.
void GTestFatalCheckGetValuesResult(
  const GetValuesResult* gvr_ptr,
  bool                   corrupt,
  int                    connection,
  const ParamsMap&       request_map,
  const ParamsMap&       response_map,
  int                    invocation_line);

//    This utility allows a child process to establish a timer which will
// cause SIGALRM to be sent to the process after kAlarmSecondLimit seconds.
// The default disposition for SIGALRM is restored so that receipt of
// SIGALRM causes process termination.
//    This utility is meant to be used by every child process forked by the
// main test process. This is done to to prevent process leaks in the case that
// the main test process does not terminate the process or in the case that the
// child process hangs and does not terminate.
constexpr const unsigned int kAlarmSecondLimit {3U};
void ChildServerAlrmRestoreAndSelfKillSet();

// Default state for testing:
// Provides default values for kExerciseDataRef.
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

// Default state for testing:
// Provides a collection of default request values for test requests.
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

// Constant state which is used in the implementation of
// GTestFatalSendExerciseRequests.
constexpr FcgiType kUnknownTypeForExercise {static_cast<FcgiType>(64)};
const std::vector<std::uint8_t> kDataForUnknownBinaryRequest {2U, 4U};

// This utility function is intended to exercise a TestFcgiClientInterface
// instance by sending several kinds of requests.
//
// Parameters:
// client_inter_ptr:          A pointer to the TestFcgiClientInterface instance
//                            to which requests will be sent.
// exercise_data_ref:         The description of the application requests which
//                            will be sent to the interface.
// application_request_count: The number of application requests which will be
//                            sent.
// observer_ptr:              A pointer to the observer of the connection on
//                            which requests will be sent. The connection field
//                            of the pointed to object indicates the connection
//                            to use.
// total_completed_ptr:       A pointer to a buffer for the expected total
//                            number of completed application requests for
//                            the client interface.
// total_pending_ptr:         As for total_completed_ptr but for pending
//                            application requests.
// request_set_ptr:           A pointer to a set of request identifiers which
//                            will be modified as requests are sent.
// invocation_line:           The line at which a client invokes the function.
//
// Effects:
// 1) Observer updates and checks are performed. The objects pointed to by
//    observer_ptr and total_pending_ptr are modified.
// 2) An FCGI_GET_VALUES request is sent by invoking SendGetValuesRequest with
//    kMapWithValues.
// 3) An unknown management request is sent by invoking
//    SendBinaryManagementRequest with kUnknownTypeForExercise and
//    kDataForUnknownBinaryRequest.
// 4) application_request_count application requests are sent by invoking
//    SendRequest with exercise_data_ref.
void GTestFatalSendExerciseRequests(
  TestFcgiClientInterface*                            client_inter_ptr,
  const struct FcgiRequestDataReference&              exercise_data_ref,
  int                                                 application_request_count,
  struct ClientInterfaceConnectionOnlyObserverValues* observer_ptr,
  std::size_t*                                        total_completed_ptr,
  std::size_t*                                        total_pending_ptr,
  std::set<FcgiRequestIdentifier>*                    request_set_ptr,
  int                                                 invocation_line);

// Checks for equality of the byte sequences given by
// [reference_stream_begin, reference_stream_end] and response_stream.
void GTestFatalStreamDataComparison
(
  const std::uint8_t*              reference_stream_begin,
  const std::uint8_t*              reference_stream_end,
  const std::vector<std::uint8_t>& response_stream,
  int                              invocation_line);

//    Compares the observable state of an FcgiResponse instance except for
// that given by RequestId to the corresponding state of an
// FcgiRequestDataReference instance.
//    Comparison is based on:
// 1) The mapping:
//    FCGI_STDIN -> FCGI_STDOUT
//    FCGI_DATA  -> FCGI_STDERR
// 2) app_response_ptr->AppStatus()      == EXIT_SUCCESS
// 3) app_response_ptr->ProtocolStatus() == FCGI_REQUEST_COMPLETE
// 4) app_response_ptr->Request()        == sent_request_ref
//
// Note that this function cannot check that the correct value of the
// FCGI_PARAMS stream was received by the FastCGI server which sent the
// response.
void GTestFatalEchoResponseCompare(
  const struct FcgiRequestDataReference& sent_request_ref,
  FcgiResponse* app_response_ptr,
  int invocation_line);

//    This atomic flag is used to allow a blocked call to
// FcgiServerInterface::AcceptRequests to be broken out of. AcceptRequests may
// be called in a loop which tests the value of the flag. If it is set, then
// a signal handler which sets the flag should have handled a signal. Signal
// receipt can then be arranged to occur when AcceptRequests is blocked.
//    This strategy is used by GTestFatalServerAcceptLoop<Func>.
//
// EXTERNAL This variable is defined in
// //test/test:src/client_interface_testing_utilities_global_variables.cc.
extern std::atomic<bool> server_accept_timeout;

// Sets server_accept_timeout and returns.
extern "C" void SigUsr2Handler(int);

// The default timeout for a blocked call to
// FcgiServerInterface::AcceptRequests. The timeout was made relatively long to
// reduce the probability that the loop on server_accept_timeout has not
// blocked in a call to AcceptRequests when the alarm times out. This may
// happen, for example, due to unusually long scheduling delays. Smaller values
// caused such failures to occur relatively frequently when the test program
// was executed many times in a row (on the order of thousands of executions).
constexpr const struct timespec kNanoTimeout
{
  /* tv_sec  = */ 0,
  /* tv_nsec = */ 10000000 // 10 ms
};
constexpr const struct itimerspec kTimerTimeout
{
  /* it_interval = */ {},
  /* it_value    = */ kNanoTimeout
};
// EXTERNAL This variable is defined in
// //test/test:src/client_interface_testing_utilities_global_variables.cc.
extern struct sigevent sev;

timer_t CreateRealtimePosixTimer(struct sigevent* evp);
// EXTERNAL This variable is defined in
// //test/test:src/client_interface_testing_utilities_global_variables.cc.
extern timer_t accept_requests_block_escape_timer_id;

// The following functions facilitate the processing of management and
// application requests by FastCGI test server instances.
//    The loop until AcceptRequests blocks was found to be needed as a single
// call to AcceptRequests does not necessarily read all of the information
// of a request. A timer and its associated SIGUSR2 signal are used to break
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
  ASSERT_NE(inter_ptr, nullptr);
  std::vector<FcgiRequest> accept_buffer {};
  server_accept_timeout.store(false);

  ASSERT_NE(timer_settime(accept_requests_block_escape_timer_id, 0,
    &kTimerTimeout, nullptr), -1) << std::strerror(errno);
  while(!(server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_buffer = inter_ptr->AcceptRequests());
    ASSERT_NO_THROW(ASSERT_NO_FATAL_FAILURE(op(&accept_buffer)));
    accept_buffer.clear();
  }
}

// Part of the implementation of GTestFatalAcceptRequestsExpectNone.
void OperationForExpectNone(std::vector<FcgiRequest>* accept_buffer_ptr);

//    A utility which calls inter_ptr->AcceptRequests in a loop on
// server_accept_timeout and expects no FcgiRequest instances to be produced
// by the call. Request which are produced are destroyed.
//    This utility is useful when only management requests are sent to an
// FcgiServerInterface instance as it will handle management requests
// implicitly without the production of FcgiRequest objects.
void GTestFatalAcceptRequestsExpectNone(FcgiServerInterface* inter_ptr,
  int invocation_line);

// Echoes a request. FCGI_STDIN is mapped to FCGI_STDOUT. FCGI_DATA is mapped
// to FCGI_STDERR.
void GTestFatalRequestEcho(FcgiRequest* request_ptr, int invocation_line);

// 1) Iterates over *accept_buffer_ptr and echoes a request back to the client.
//    The response echo is performed by invoking GTestFatalRequestEcho.
// 2) The sent_environ parameter is used to allow the utility to check that the
//    expected FCGI_PARAMS map was received.
// 3) role and keep_conn values are compared to those of the FcgiRequest object.
// 4) *accept_buffer_ptr is not cleared of requests.
void GTestFatalOperationForRequestEcho(
  std::vector<FcgiRequest>* accept_buffer_ptr,
  const ParamsMap& sent_environ,
  std::uint16_t role,
  bool keep_conn,
  int invocation_line);

// A utility which calls inter_ptr->AcceptRequests in a loop on
// server_accept_timeout and echoes the content of a request in the response to
// the request. GTestFatalOperationForRequestEcho is used to echo the request.
void GTestFatalAcceptRequestsRequestEcho(FcgiServerInterface* inter_ptr,
  const ParamsMap& sent_environ, std::uint16_t role, bool keep_conn,
  int invocation_line);

// A helper function to terminate and reap a child process which was created by
// the test process during testing. It is expected that the child has not
// terminated or otherwise changed state. The state of the child is checked. If
// the child has not terminated, it is terminated with SIGKILL. The child is
// then reaped.
void GTestFatalTerminateChild(pid_t child_id, int invocation_line);

struct ExpectedInvalidRecordValues
{
  std::uint8_t*         content_buffer_ptr;
  std::uint16_t         content_length;
  std::uint8_t          padding_length;
  FcgiRequestIdentifier id;
  FcgiType              type;
  std::uint8_t          version;
};

void GTestFatalSendRecordAndExpectInvalidRecord(
  TestFcgiClientInterface*                  client_interface_ptr,
  int                                       server_connection,
  std::uint8_t*                             record_buffer,
  std::size_t                               record_length,
  const struct ExpectedInvalidRecordValues& expected_values,
  int                                       invocation_line);

using DisconnectWithServerReturn =
  std::function<void(int, FcgiRequestIdentifier, FcgiServerInterface**, int)>;

//    Tests the behavior of *client_inteface_ptr when a new connection is closed
// by the action of a call to disconnect_with_server_return. Two cases are
// tested: when at least one completed and unreleased application request is
// present for the new connection, and when no completed and unreleased
// application requests are present for the new connection.
// disconnect_with_server_return is called twice during the iterative
// testing of *client_inteface_ptr over the two cases.
//    This function is designed to exercise a client interface as per the
// discussion of connection closure handling at the beginning of
// test_fcgi_client_interface_test.cc.
//
// Parameters:
// address:                       The textual representation of the address
//                                used by an FcgiServerInterface instance
//                                without a port if one is applicable.
// network_port:                  The port associated with address.
// client_interface_ptr:          A pointer to the TestFcgiClientInterface
//                                whose behavior on connection closure will be
//                                tested.
// server_interface_ptr:          A pointer to the initial FcgiServerInterface
//                                which is associated with address.
// disconnect_with_server_return: A function which will cause connection
//                                closure and which will provide a pointer to
//                                the next FcgiServerInterface which will be
//                                used for request-response cycles.
// invocation_line:               The line at which the function is invoked.
//
// Preconditions:
// General:
// 1) kUnixPath2 must be available for use by an AF_UNIX server.
// disconnect_with_server_return:
// 1) The FcgiServerInterface* instance associated with the result parameter
//    with type FcgiServerInterface** of DisconnectWithServerReturnType must
//    not be null after each invocation of disconnect_with_server_return. In
//    other words, a call to disconnect_with_server_return must return an
//    appropriate pointer value through the return parameter.
// 2) The value of the FcgiServerInterface* instance pointed to by the
//    FcgiServerInterface** argument must be ignored during invocation.
// 3) disconnect_with_server_return must be able to be called twice.
// 4) disconnect_with_server_return must handle all resource management of
//    objects created on the heap during its execution.
// 5) The lifetime of the FcgiServerInterface instance pointed to by the value
//    returned through the return parameter must extend from the time of the
//    instance's creation to the time of the next call to
//    disconnect_with_server_return.
// 6) In addition to causing connection closure, invocation of
//    disconnect_with_server_return must cause:
//    1) *client_interface_ptr to become aware of connection closure.
//    2) client_interface_ptr->ReadyEventCount() == 0U by the time of the
//       return of the invocation.
// 7) disconnect_with_server_return must follow the conventions for reporting
//    Google Test assertion failures. In particular, fatal failures may be
//    reported with fatal assertions, and the terminal int parameter value must
//    be interpreted as an invocation line number.
// Partial record delivery:
// 1) Let new_connection be the value of the descriptor of the connection
//    created by GTestFatalConnectionClosureCheck to test the client interface
//    for one of the two cases of connection closure. new_connection + 1 must
//    be the descriptor created by the FcgiServerInterface argument to handle
//    new_connection. GTestFatalConnectionClosureCheck will write through this
//    descriptor to send a partial record to the client interface.
// *client_interface_ptr
// 1) client_interface_ptr->ReadyEventCount() == 0U upon invocation of
//    GTestFatalConnectionClosureCheck.
//
// Implied limitations:
// 1) The FcgiServerInterface instances used during execution must be present
//    in the process which houses *client_interface_ptr.
void GTestFatalConnectionClosureCheck(
  const char*                address,
  in_port_t                  network_port,
  TestFcgiClientInterface*   client_interface_ptr,
  FcgiServerInterface*       server_interface_ptr,
  DisconnectWithServerReturn disconnect_with_server_return,
  int                        invocation_line);

using ClosureDetector =
  std::function<void(TestFcgiClientInterface*, FcgiRequestIdentifier)>;

// A metafunction which is used in the implementation of test cases which
// use GTestFatalConnectionClosureCheck. The first five parameters are
// intended to be appropriately bound to generate a function which can be
// passed as the argument to the parameter of GTestFatalConnectionClosureCheck
// with type DisconnectWithServerReturnType.
void GTestFatalServerDestructionClosureMeta(
  struct InterfaceCreationArguments*    inter_args_ptr,
  std::unique_ptr<FcgiServerInterface>* server_uptr_ptr,
  TestFcgiClientInterface*              client_inter_ptr,
  ClosureDetector                       closure_detector,
  std::vector<int>*                     descriptor_list_ptr,
  int                                   connection,
  FcgiRequestIdentifier                 pending_application_request,
  FcgiServerInterface**                 server_interface_ptr_ptr,
  int                                   invocation_line);

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components

#endif // AS_COMPONENTS_FCGI_TEST_TEST_INCLUDE_CLIENT_INTERFACE_TESTING_UTILITIES_H_
