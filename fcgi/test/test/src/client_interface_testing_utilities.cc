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

#include "test/test/include/client_interface_testing_utilities.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "gtest/include/as_components_testing_gtest_utilities.h"
#include "googletest/include/gtest/gtest.h"
#include "include/socket_functions.h"

#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_request.h"
#include "include/fcgi_request_identifier.h"
#include "include/fcgi_server_interface.h"
#include "include/fcgi_utilities.h"
#include "test/include/test_fcgi_client_interface.h"

namespace as_components {
namespace fcgi {
namespace test {
namespace test {

void GTestFatalClientInterfaceConnectionOnlyObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceConnectionOnlyObserverValues& values,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalClientInterfaceConnectionOnlyObserverCheck"};

  // Fatal checks.
  std::size_t connection_completed_request_count_return {};
  ASSERT_NO_THROW(connection_completed_request_count_return =
    client_inter.CompletedRequestCount(values.connection));
  EXPECT_EQ(connection_completed_request_count_return,
    values.connection_completed_request_count);

  bool is_connected_return {};
  ASSERT_NO_THROW(is_connected_return =
    client_inter.IsConnected(values.connection));
  EXPECT_EQ(is_connected_return, values.is_connected);

  std::size_t management_request_count_return {};
  ASSERT_NO_THROW(management_request_count_return =
    client_inter.ManagementRequestCount(values.connection));
  EXPECT_EQ(management_request_count_return, values.management_request_count);

  std::size_t connection_pending_request_count_return {};
  ASSERT_NO_THROW(connection_pending_request_count_return =
    client_inter.PendingRequestCount(values.connection));
  EXPECT_EQ(connection_pending_request_count_return,
    values.connection_pending_request_count);
}

void GTestNonFatalClientInterfaceInstanceObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceInstanceObserverValues& values,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestNonFatalClientInterfaceInstanceObserverCheck"};

  EXPECT_EQ(values.total_completed_request_count,
    client_inter.CompletedRequestCount());
  EXPECT_EQ(values.connection_count, client_inter.ConnectionCount());
  EXPECT_EQ(values.total_pending_request_count,
    client_inter.PendingRequestCount());
  EXPECT_EQ(values.ready_event_count, client_inter.ReadyEventCount());
}

void GTestFatalClientInterfaceObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceObserverValues& values,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalClientInterfaceObserverCheck"};

  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    values.in, __LINE__);
  GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
    values.co, __LINE__);
}

extern "C" void SigUsr2Handler(int)
{
  server_accept_timeout.store(true);
}

timer_t CreateRealtimePosixTimer(struct sigevent* evp)
{
  timer_t local_timer;
  if(timer_create(CLOCK_REALTIME, evp, &local_timer) == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "Timer initialization failed during an "
      "an invocation of CreateRealtimePosixTimer."};
  }
  return local_timer;
}

void TestFcgiClientInterfaceTestFixture::SetUp()
{
  // Fatal failures here should prevent the test from running. Execution
  // should then go to the next test.
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
    __LINE__));
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalSetSignalDisposition(
    SIGUSR2, &SigUsr2Handler, __LINE__));
  ASSERT_TRUE(server_accept_timeout.is_lock_free());
  // Ensure that the the kTimeout flag is cleared.
  server_accept_timeout.store(false);
}

void TestFcgiClientInterfaceTestFixture::TearDown()
{
  // Resource release is performed before operations which could throw or
  // generate fatal Google Test failures.
  for(std::vector<int>::iterator iter {descriptor_resource_list_.begin()};
    iter != descriptor_resource_list_.end(); ++iter)
  {
    close(*iter);
  }
  for(std::vector<const char*>::iterator iter {path_resource_list_.begin()};
    iter != path_resource_list_.end(); ++iter)
  {
    EXPECT_NE(unlink(*iter), -1) << std::strerror(errno);
  }
  // Clear the kTimeout flag to reset shared state.
  server_accept_timeout.store(false);
  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc_,
    "TestFcgiClientInterfaceTestFixture", __LINE__);
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGUSR2,
    __LINE__));
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGPIPE,
    __LINE__));
}

void GTestFatalCheckGetValuesResult(
  const GetValuesResult* gvr_ptr,
  bool                   corrupt,
  int                    connection,
  const ParamsMap&       request_map,
  const ParamsMap&       response_map,
  int                    invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalCheckGetValuesResult"};
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(gvr_ptr->IsCorrupt(), corrupt);
  EXPECT_EQ(
    (FcgiRequestIdentifier {connection, FCGI_NULL_REQUEST_ID}),
    gvr_ptr->RequestId());
  EXPECT_EQ(request_map, gvr_ptr->RequestMap());
  EXPECT_EQ(response_map, gvr_ptr->ResponseMap());
}

void ChildServerAlrmRestoreAndSelfKillSet()
{
  // Restore the default behavior for reception of SIGALRM for the self-kill
  // timer below.
  sigset_t sigset {};
  if(sigemptyset(&sigset) == -1)
  {
    _exit(EXIT_FAILURE);
  }
  struct sigaction sa {};
  sa.sa_handler = SIG_DFL;
  sa.sa_mask    = sigset;
  sa.sa_flags   = 0;
  if(sigaction(SIGALRM, &sa, nullptr) == -1)
  {
    _exit(EXIT_FAILURE);
  }
  // Establish a self-kill timer to ensure process termination.
  alarm(kAlarmSecondLimit);
}

// This function is used in the implementation of ConnectCase1.
void GTestFatalSendExerciseRequests(
  TestFcgiClientInterface*                            client_inter_ptr,
  const struct FcgiRequestDataReference&              exercise_data_ref,
  int                                                 application_request_count,
  struct ClientInterfaceConnectionOnlyObserverValues* observer_ptr,
  std::size_t*                                        total_completed_ptr,
  std::size_t*                                        total_pending_ptr,
  std::set<FcgiRequestIdentifier>*                    request_set_ptr,
  int                                                 invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalExerciseTestFcgiClientInterface"};
  // Check for and reject nullptr values.
  ASSERT_NE(client_inter_ptr, nullptr);
  ASSERT_NE(observer_ptr, nullptr);
  ASSERT_NE(total_completed_ptr, nullptr);
  ASSERT_NE(total_pending_ptr, nullptr);
  ASSERT_NE(request_set_ptr, nullptr);
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter_ptr->SendGetValuesRequest(
    observer_ptr->connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  ++(observer_ptr->management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    *client_inter_ptr, *observer_ptr, __LINE__));
  bool send_binary {false};
  // Random values are used for the binary management request.
  ASSERT_NO_THROW(send_binary = client_inter_ptr->SendBinaryManagementRequest(
    observer_ptr->connection, kUnknownTypeForExercise,
    kDataForUnknownBinaryRequest.data(),
    kDataForUnknownBinaryRequest.data() +
      kDataForUnknownBinaryRequest.size()));
  ASSERT_TRUE(send_binary);
  ++(observer_ptr->management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    *client_inter_ptr, *observer_ptr, __LINE__));
  FcgiRequestIdentifier identifier_buffer {};
  // Send application requests.
  // This loop modifies the pointed-to request set.
  for(int i {0}; i != application_request_count; ++i)
  {
    std::string iteration_count_string {"iteration count: "};
    iteration_count_string.append(std::to_string(i));
    ::testing::ScopedTrace iteration_tracer {"", __LINE__,
      iteration_count_string.data()};
    ASSERT_NO_THROW(identifier_buffer = client_inter_ptr->SendRequest(
      observer_ptr->connection, exercise_data_ref));
    ASSERT_NE(identifier_buffer, FcgiRequestIdentifier {});
    ASSERT_EQ(identifier_buffer.descriptor(), observer_ptr->connection);
    ASSERT_NE(identifier_buffer.Fcgi_id(), FCGI_NULL_REQUEST_ID);
    ASSERT_TRUE(request_set_ptr->insert(identifier_buffer).second);
  }
  // Update the pointed-to total pending request count.
  observer_ptr->connection_pending_request_count += application_request_count;
  *total_pending_ptr   += application_request_count;
  EXPECT_EQ(*total_completed_ptr, client_inter_ptr->CompletedRequestCount());
  EXPECT_EQ(*total_pending_ptr, client_inter_ptr->PendingRequestCount());
  GTestFatalClientInterfaceConnectionOnlyObserverCheck(*client_inter_ptr,
    *observer_ptr, __LINE__);
}

void GTestFatalStreamDataComparison
(
  const std::uint8_t*              reference_stream_begin,
  const std::uint8_t*              reference_stream_end,
  const std::vector<std::uint8_t>& response_stream,
  int                              invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalStreamDataComparison"};
  ASSERT_TRUE(((reference_stream_begin == nullptr) &&
               (reference_stream_end   == nullptr))    ||
              ((reference_stream_begin != nullptr) &&
               (reference_stream_end   != nullptr)));

  using IterPair = std::pair<const std::uint8_t*,
                             std::vector<std::uint8_t>::const_iterator>;

  std::size_t reference_stream_length
    {static_cast<std::size_t>(std::distance(
      reference_stream_begin, reference_stream_end))};
  if(reference_stream_length == response_stream.size())
  {
    IterPair first_diff {std::mismatch(
      reference_stream_begin,
      reference_stream_end,
      response_stream.begin())};
    IterPair ends {
      reference_stream_end,
      response_stream.end()};
    EXPECT_EQ(first_diff, ends);
  }
  else
  {
    ADD_FAILURE() << "Stream length mismatch.";
  }
};

void GTestFatalEchoResponseCompare(
  const struct FcgiRequestDataReference& sent_request_ref,
  FcgiResponse* app_response_ptr,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalExerciseResponseCompare"};
  ASSERT_NE(app_response_ptr, nullptr);
  // Returned FcgiRequestDataReference instance
  const struct FcgiRequestDataReference& request_ref
    {app_response_ptr->Request()};
  EXPECT_EQ(request_ref, sent_request_ref);
  // Application status
  EXPECT_EQ(app_response_ptr->AppStatus(), EXIT_SUCCESS);
  // Protocol status
  EXPECT_EQ(app_response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
  // FCGI_STDERR and FCGI_STDOUT
  ASSERT_NO_FATAL_FAILURE(GTestFatalStreamDataComparison(
    sent_request_ref.data_begin, sent_request_ref.data_end,
    app_response_ptr->FcgiStderr(), __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalStreamDataComparison(
    sent_request_ref.stdin_begin, sent_request_ref.stdin_end,
    app_response_ptr->FcgiStdout(), __LINE__));
}

void OperationForExpectNone(std::vector<FcgiRequest>* accept_buffer_ptr)
{
  EXPECT_EQ(accept_buffer_ptr->size(), 0U);
  accept_buffer_ptr->clear();
}

void GTestFatalAcceptRequestsExpectNone(FcgiServerInterface* inter_ptr,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalAcceptRequestsExpectNone"};
  ASSERT_NE(inter_ptr, nullptr);
  ASSERT_NO_FATAL_FAILURE(GTestFatalServerAcceptLoop(inter_ptr,
    &OperationForExpectNone, __LINE__));
}

void GTestFatalRequestEcho(FcgiRequest* request_ptr, int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalRequestEcho"};
  ASSERT_NE(request_ptr, nullptr);
  const std::vector<std::uint8_t>& stdin_ref {request_ptr->get_STDIN()};
  const std::vector<std::uint8_t>& data_ref  {request_ptr->get_DATA()};
  int write_count {0};
  // Convert the returned boolean write status to an integer.
  // Check that all writes were successful.
  ASSERT_NO_THROW
  (
    write_count += request_ptr->Write(stdin_ref.begin(), stdin_ref.end());
    write_count += request_ptr->WriteError(data_ref.begin(), data_ref.end());
    write_count += request_ptr->Complete(EXIT_SUCCESS);
  );
  ASSERT_EQ(write_count, 3);
}

void GTestFatalOperationForRequestEcho(
  std::vector<FcgiRequest>* accept_buffer_ptr,
  const ParamsMap& sent_environ,
  std::uint16_t role,
  bool keep_conn,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalOperationForRequestEcho"};
  ASSERT_NE(accept_buffer_ptr, nullptr);
  for(std::vector<FcgiRequest>::iterator iter {accept_buffer_ptr->begin()};
    iter != accept_buffer_ptr->end(); ++iter)
  {
    EXPECT_EQ(iter->get_environment_map(), sent_environ);
    EXPECT_EQ(iter->get_role(), role);
    EXPECT_EQ(iter->get_keep_conn(), keep_conn);
    ASSERT_NO_FATAL_FAILURE(GTestFatalRequestEcho(&(*iter), __LINE__));
  }
};

void GTestFatalAcceptRequestsRequestEcho(FcgiServerInterface* inter_ptr,
  const ParamsMap& sent_environ, std::uint16_t role, bool keep_conn,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalAcceptRequestsExerciseRequestEcho"};
  ASSERT_NE(inter_ptr, nullptr);
  // Use std::bind to remove sent_environ from the parameter list of
  // OperationForRequestEcho so that the result can be used in a call to
  // GTestFatalServerAcceptLoop.
  auto bound_operation {std::bind(GTestFatalOperationForRequestEcho,
    std::placeholders::_1, std::cref(sent_environ), role, keep_conn, __LINE__)};
  std::function<void(std::vector<FcgiRequest>*)> local_op
    {bound_operation};
  GTestFatalServerAcceptLoop(inter_ptr, local_op, __LINE__);
}

// A helper function to reap a child process which was created by the test
// process during testing.
void GTestFatalTerminateChild(pid_t child_id, int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalTerminateChild"};

  int status {0};
  // Poll for termination of the process given by child_id. It is expected that
  // previous termination of the child was due to an error.
  int waitpid_return {waitpid(child_id, &status, WNOHANG)};
  if(waitpid_return != -1)
  {
    if(waitpid_return != 0)
    {
      ADD_FAILURE() << "The child changed state unexpectedly.";
    }
    // If the child hasn't changed state or has changed state but hasn't
    // terminated, then kill the child.
    if((waitpid_return == 0) || (!WIFEXITED(status) && !WIFSIGNALED(status)))
    {
      if(kill(child_id, SIGKILL) == -1)
      {
        FAIL() << std::strerror(errno);
      }
      // Reap the child once it terminates.
      while(((waitpid_return = waitpid(child_id, nullptr, 0)) == -1) &&
            (errno == EINTR))
        continue;
      ASSERT_NE(waitpid_return, -1) << std::strerror(errno);
    }
  }
  else
  {
    FAIL() << std::strerror(errno);
  }
}

void GTestFatalSendRecordAndExpectInvalidRecord(
  TestFcgiClientInterface*                  client_interface_ptr,
  int                                       server_connection,
  std::uint8_t*                             record_buffer,
  std::size_t                               record_length,
  const struct ExpectedInvalidRecordValues& expected_values,
  int                                       invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalSendRecordAndExpectInvalidRecord"};

  ASSERT_NE(client_interface_ptr, nullptr);
  ASSERT_NE(record_buffer, nullptr);
  ASSERT_GT(record_length, 0U);
  if(expected_values.content_length)
  {
    ASSERT_NE(expected_values.content_buffer_ptr, nullptr);
  }

  ASSERT_EQ(socket_functions::SocketWrite(server_connection, record_buffer,
    record_length), record_length) << std::strerror(errno);
  std::unique_ptr<ServerEvent> event_uptr {};
  ASSERT_NO_THROW(event_uptr = client_interface_ptr->RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  InvalidRecord* invalid_record_ptr {dynamic_cast<InvalidRecord*>(
    event_uptr.get())};
  EXPECT_NE(invalid_record_ptr, nullptr);
  if(invalid_record_ptr)
  {
    const std::vector<std::uint8_t>& end_content {invalid_record_ptr->Content()};
    bool correct_content_length {end_content.size() ==
      expected_values.content_length};
    EXPECT_TRUE(correct_content_length);
    if(correct_content_length && expected_values.content_length)
    {
      std::pair<std::vector<std::uint8_t>::const_iterator, std::uint8_t*>
      expected_mismatch_ends {end_content.cend(),
        expected_values.content_buffer_ptr + expected_values.content_length};
      std::pair<std::vector<std::uint8_t>::const_iterator, std::uint8_t*>
      mismatch_ends {std::mismatch(end_content.cbegin(), end_content.cend(),
        expected_values.content_buffer_ptr)};
      EXPECT_EQ(expected_mismatch_ends.first, mismatch_ends.first);
      EXPECT_EQ(expected_mismatch_ends.second, mismatch_ends.second);
      if(expected_mismatch_ends.first != mismatch_ends.first)
      {
        std::cout << "Reported content: ";
        std::vector<std::uint8_t>::const_iterator content_end_iter
          {expected_mismatch_ends.first};
        for(std::vector<std::uint8_t>::const_iterator content_iter
          {end_content.begin()};
          content_iter != content_end_iter;
          ++content_iter)
        {
          std::cout << int(*content_iter) << " ";
        }
        std::cout << '\n';
        std::cout << "Actual content: ";
        const std::uint8_t* expected_end_ptr {expected_mismatch_ends.second};
        for(const std::uint8_t* expected_content_ptr
          {expected_values.content_buffer_ptr};
          expected_content_ptr != expected_end_ptr;
          ++expected_content_ptr)
        {
          std::cout << int(*expected_content_ptr) << " ";
        }
        std::cout << '\n';
      }
    }
    EXPECT_EQ(invalid_record_ptr->PaddingLength(),
      expected_values.padding_length);
    EXPECT_EQ(invalid_record_ptr->RequestId(), expected_values.id);
    EXPECT_EQ(invalid_record_ptr->Type(), expected_values.type);
    EXPECT_EQ(invalid_record_ptr->Version(), expected_values.version);
  }
}

void GTestFatalConnectionClosureCheck(
  const char*                address,
  in_port_t                  network_port,
  TestFcgiClientInterface*   client_interface_ptr,
  FcgiServerInterface*       server_interface_ptr,
  DisconnectWithServerReturn disconnect_with_server_return,
  int                        invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalConnectionClosureCheck"};

  // Creates a non-local server that can be used to ensure proper processing
  // of the partial records which will be received by *client_interface_ptr.
  // Proper processing is ensured by sending a response after an appreciable
  // time delay has elapsed from the time of partial record receipt. Doing so
  // allows RetrieveServerEvent to return after the client interface processed
  // the partial record.
  int sv[2] = {};
  ASSERT_NE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), -1) <<
    std::strerror(errno);
  pid_t fork_return {fork()};
  if(fork_return == -1)
  {
    // Error. In parent.
    close(sv[0]);
    close(sv[1]);
    FAIL() << std::strerror(errno);
  }
  else if(fork_return == 0)
  {
    // In the child.
    close(sv[0]);
    int socket_to_parent {sv[1]};
    ChildServerAlrmRestoreAndSelfKillSet(); // Uses SIGALRM and the itimer API
    // wall-time timer.
    struct sigevent child_sev
    {
      /* sigev_notify        = */ SIGEV_SIGNAL,
      /* sigev_signo         = */ SIGUSR2,
      /* sigev_value (union) = */ 0,
      /* _sigev_un           = */ 0 // system data set to default
    };
    // Destroy the inherited FcgiServerInstance to allow another to be created.
    // (The process will be exited before the C++ destructors of any resource
    // management classes of instances inherited from the parent process can be
    // called.)
    delete(server_interface_ptr);
    struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
    inter_args.domain          = AF_UNIX;
    inter_args.unix_path       = kUnixPath2;
    std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
      inter_return {};
    ASSERT_NO_THROW(inter_return =
      GTestNonFatalCreateInterface(inter_args, __LINE__));
    std::unique_ptr<FcgiServerInterface>& inter_uptr
      {std::get<0>(inter_return)};
    if(inter_uptr.get() == nullptr)
    {
      _exit(EXIT_FAILURE);
    }
    // Informs the parent of interface creation.
    std::uint8_t child_byte_buffer[1U] = {1U};
    if(write(socket_to_parent, child_byte_buffer, 1U) != 1U)
    {
      _exit(EXIT_FAILURE);
    }
    timer_t accept_requests_break_timer_id
      {CreateRealtimePosixTimer(&child_sev)};
    std::vector<FcgiRequest> accept_buffer {};
    while(true)
    {
      // Waits for the parent to signal.
      if(read(socket_to_parent, child_byte_buffer, 1U) != 1U)
      {
        _exit(EXIT_FAILURE);
      }
      // Sleeps, then accepts and responds to management requests.
      struct timespec requested {kNanoTimeout};
      struct timespec remaining {};
      // A simple sleep loop which is acceptable for testing code.
      int sleep_return {};
      while(((sleep_return = nanosleep(&requested, &remaining)) == -1)
            && (errno == EINTR))
      {
        requested = remaining;
      }
      if(sleep_return == -1)
      {
        _exit(EXIT_FAILURE);
      }
      server_accept_timeout.store(false);
      // This timer uses SIGUSR2.
      if(timer_settime(accept_requests_break_timer_id, 0, &kTimerTimeout,
        nullptr) == -1)
      {
        _exit(EXIT_FAILURE);
      }
      while(!(server_accept_timeout.load()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
        accept_buffer.clear();
      }
    }
  }
  // else, in parent.
  close(sv[1]);
  int non_local_server_socket {sv[0]};

  // A resource management class for the resources of the child process.
  // Terminates the child during normal execution.
  struct Terminator
  {
    ~Terminator()
    {
      try
      {
        // Kill the child process before closing the socket. The other order
        // allows the child to detect socket closure and terminate. Such
        // termination would then be registered as an error when the parent
        // process tries to terminate the child.
        GTestFatalTerminateChild(child_id, __LINE__);
        close(descriptor);
        unlink(kUnixPath2);
      }
      catch(std::exception& e)
      {
        ADD_FAILURE() << e.what();
      }
      catch(...)
      {
        ADD_FAILURE();
      }
    }

    int descriptor;
    pid_t child_id;
  };
  struct Terminator child_terminator {non_local_server_socket, fork_return};

  // Waits for the non-local server to confirm interface creation.
  std::uint8_t byte_buffer[1U];
  ASSERT_EQ(read(non_local_server_socket, byte_buffer, 1U), 1U) <<
    std::strerror(errno);
  // Creates a connection to the non-local server.
  int non_local_server_connection {};
  ASSERT_NO_THROW(ASSERT_NE(non_local_server_connection =
    client_interface_ptr->Connect(kUnixPath2, 0U), -1) << std::strerror(errno));

  // Common state and data for partial records.
  std::uint8_t stdout_header[FCGI_HEADER_LEN] = {};
  std::vector<std::uint8_t>::size_type stdin_size
    {kStdinDataForClientExercise.size()};
  ASSERT_LE(stdin_size, std::numeric_limits<std::uint16_t>::max());
  std::uint8_t padding_mod {static_cast<std::uint8_t>(stdin_size % 8U)};
  std::uint8_t padding_length {static_cast<std::uint8_t>((padding_mod) ?
    8U - padding_mod : 0U)};

  // Establishes the connection which will be used for testing behavior upon
  // connection closure.
  int new_connection {};
  ASSERT_NO_THROW(ASSERT_NE(new_connection =
    client_interface_ptr->Connect(address, network_port), -1));
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
      /* connection                         = */ new_connection,
      /* connection_completed_request_count = */ 0U,
      /* is_connected                       = */ true,
      /* management_request_count           = */ 0U,
      /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */
        client_interface_ptr->CompletedRequestCount(),
      /* connection_count              = */
        client_interface_ptr->ConnectionCount(),
      /* total_pending_request_count   = */
        client_interface_ptr->PendingRequestCount(),
      /* ready_event_count             = */ 0U
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    *client_interface_ptr, observer, __LINE__));
  // Allows the server to process the connection and create
  // the descriptor with value new_connection + 1.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
    server_interface_ptr, __LINE__));
  // The second iteration uses the fact that a completed application request
  // is present from the first iteration for new_connection.
  constexpr const int kCaseCount {2};
  for(int i {0}; i < kCaseCount; ++i)
  {
    ::testing::ScopedTrace loop_tracer {__FILE__, __LINE__, std::to_string(i)};
    // Establishes pending management and application requests.
    ASSERT_NO_THROW(ASSERT_TRUE(client_interface_ptr->SendGetValuesRequest(
      new_connection, kMpxsMapWithValue)));
    ++(observer.co.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    FcgiRequestIdentifier pending_app_request_id {};
    ASSERT_NO_THROW(pending_app_request_id = client_interface_ptr->SendRequest(
      new_connection, kExerciseDataRef));
    ASSERT_NE(pending_app_request_id, FcgiRequestIdentifier {});
    ++(observer.co.connection_pending_request_count);
    ++(observer.in.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    // Writes a partial record to the connection. A record for the response to
    // the pending application request is written.
    PopulateHeader(stdout_header, FcgiType::kFCGI_STDOUT,
      pending_app_request_id.Fcgi_id(), stdin_size, padding_length);
    ASSERT_EQ(socket_functions::SocketWrite(new_connection + 1, stdout_header,
      FCGI_HEADER_LEN), static_cast<unsigned int>(FCGI_HEADER_LEN)) <<
        std::strerror(errno);
    // Ensures that the partial record has been processed by the client
    // interface. See the discussion at the start of the function body on this
    // point.
    ASSERT_NO_THROW(ASSERT_TRUE(client_interface_ptr->SendGetValuesRequest(
      non_local_server_connection, kMpxsNameMap)));
    // Signals the child that a request was made.
    ASSERT_EQ(write(non_local_server_socket, byte_buffer, 1U), 1U) <<
      std::strerror(errno);
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_interface_ptr->RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    GetValuesResult* non_local_gvr_ptr
      {dynamic_cast<GetValuesResult*>(event_uptr.get())};
    ASSERT_NE(non_local_gvr_ptr, nullptr) << typeid(*(event_uptr.get())).name();
    // Calls the function which will cause the connection to become
    // disconnected and which will return a pointer to the server interface
    // which will be connected to in order to verify appropriate client
    // interface state update.
    server_interface_ptr = nullptr;
    ASSERT_NO_THROW(disconnect_with_server_return(new_connection,
      pending_app_request_id, &server_interface_ptr, __LINE__));
    ASSERT_NE(server_interface_ptr, nullptr);
    // Updates and verifies directly observable interface state after
    // connection closure.
    observer.in.total_pending_request_count -=
      observer.co.connection_pending_request_count;
    observer.co.connection_pending_request_count = 0U;
    observer.co.is_connected                     = false;
    observer.co.management_request_count         = 0U;
    --(observer.in.connection_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));

    // Verifies client interface state update through request-response cycles.
    // Makes a connection to the server.
    int second_connection {};
    ASSERT_NO_THROW(ASSERT_NE(second_connection =
      client_interface_ptr->Connect(address, network_port), -1));
    ASSERT_EQ(second_connection, new_connection);
    observer.co.is_connected = true;
    ++(observer.in.connection_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    ASSERT_NO_THROW(ASSERT_TRUE(client_interface_ptr->SendGetValuesRequest(
      new_connection, kMapWithValues)));
    ++(observer.co.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    // Allows the server to process the management request.
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
      server_interface_ptr, __LINE__));
    // Retrieves the response to the management request.
    ASSERT_NO_THROW(event_uptr = client_interface_ptr->RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    GetValuesResult* gvr_ptr {dynamic_cast<GetValuesResult*>(event_uptr.get())};
    ASSERT_NE(gvr_ptr, nullptr);
    ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
      new_connection, kNameOnlyMap, kMapWithValues, __LINE__));
    --(observer.co.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    // Sends an application request, retrieves the response, and verifies the
    // response.
    FcgiRequestIdentifier check_request_id {};
    ASSERT_NO_THROW(ASSERT_NE(check_request_id =
      client_interface_ptr->SendRequest(new_connection, kExerciseDataRef),
      FcgiRequestIdentifier {}));
    ++(observer.co.connection_pending_request_count);
    ++(observer.in.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsRequestEcho(
      server_interface_ptr, kSharedExerciseParams, kExerciseDataRef.role,
      kExerciseDataRef.keep_conn, __LINE__));
    ASSERT_NO_THROW(event_uptr = client_interface_ptr->RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    --(observer.co.connection_pending_request_count);
    --(observer.in.total_pending_request_count);
    ++(observer.co.connection_completed_request_count);
    ++(observer.in.total_completed_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      *client_interface_ptr, observer, __LINE__));
    EXPECT_EQ(response_ptr->RequestId(), check_request_id);
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
      response_ptr, __LINE__));
  }
}

void GTestFatalServerDestructionClosureMeta(
  struct InterfaceCreationArguments*    inter_args_ptr,
  std::unique_ptr<FcgiServerInterface>* server_uptr_ptr,
  TestFcgiClientInterface*              client_inter_ptr,
  ClosureDetector                       closure_detector,
  std::vector<int>*                     descriptor_list_ptr,
  int                                   connection,
  FcgiRequestIdentifier                 pending_application_request,
  FcgiServerInterface**                 server_interface_ptr_ptr,
  int                                   invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "GTestFatalServerDestructionClosureMeta"};
  // Ensure that ReadyEventCount is zero.
  ASSERT_EQ(client_inter_ptr->ReadyEventCount(), 0U);
  // Destroys the server to cause the client interface to later detect
  // connection closure.
  delete(server_uptr_ptr->release());
  ASSERT_NE(unlink(inter_args_ptr->unix_path), -1) << std::strerror(errno);
  close((*descriptor_list_ptr)[0]);
  descriptor_list_ptr->clear();
  std::unique_ptr<ServerEvent> event_uptr {};
  // Detection.
  ASSERT_NO_THROW(ASSERT_NO_FATAL_FAILURE(closure_detector(client_inter_ptr,
    pending_application_request)));
  // ConnectionClosure retrieval.
  ASSERT_NO_THROW(event_uptr = client_inter_ptr->RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  ConnectionClosure* closure_ptr
    {dynamic_cast<ConnectionClosure*>(event_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId(),
    (FcgiRequestIdentifier {connection, 0U}));
  // Creates a new FcgiServerInterface per the specification of
  // GTestFatalSendRecordAndExpectInvalidRecord.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  next_server_creation_return {};
  ASSERT_NO_THROW(next_server_creation_return =
    GTestNonFatalCreateInterface(*inter_args_ptr, __LINE__));
  ASSERT_NO_THROW(descriptor_list_ptr->push_back(
    std::get<1>(next_server_creation_return)));
  FcgiServerInterface* next_server_ptr
    {std::get<0>(next_server_creation_return).get()};
  *server_uptr_ptr = std::move(std::get<0>(next_server_creation_return));
  ASSERT_NE(next_server_ptr, nullptr);
  ASSERT_EQ(client_inter_ptr->ReadyEventCount(), 0U);
  *server_interface_ptr_ptr = next_server_ptr;
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components
