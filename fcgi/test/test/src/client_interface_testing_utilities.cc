#include "test/test/include/client_interface_testing_utilities.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "external/a_component_testing/gtest/include/a_component_testing_gtest_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"

#include "server_interface_combined.h"
#include "test/include/test_fcgi_client_interface.h"

namespace a_component {
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
  // Nonfatal checks.
  EXPECT_EQ(client_inter.ConnectionCount(), values.connection_count);
  EXPECT_EQ(client_inter.ReadyEventCount(), values.ready_event_count);

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

void GTestFatalClientInterfaceObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceObserverValues& values,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalClientInterfaceObserverCheck"};

  EXPECT_EQ(values.total_completed_request_count,
    client_inter.CompletedRequestCount());
  EXPECT_EQ(values.total_pending_request_count,
    client_inter.PendingRequestCount());

  GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
    values.co, __LINE__);
}

extern "C" void SigAlrmHandler(int)
{
  server_accept_timeout.store(true);
}

void TestFcgiClientInterfaceTestFixture::SetUp()
{
  // Fatal failures here should prevent the test from running. Execution
  // should then go to the next test.
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
    __LINE__));
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalSetSignalDisposition(
    SIGALRM, &SigAlrmHandler, __LINE__));
  ASSERT_TRUE(server_accept_timeout.is_lock_free());
  // Ensure that the the kTimeout flag is cleared.
  server_accept_timeout.store(false);
}

void TestFcgiClientInterfaceTestFixture::TearDown()
{
  // Resource release is performed before operations which could throw or
  // generate fatal Google Test failures.
  for(std::vector<std::pair<int, const char*>>::iterator iter
    {resource_list_.begin()}; iter != resource_list_.end(); ++iter)
  {
    close(iter->first);
    EXPECT_NE(unlink(iter->second), -1) << std::strerror(errno);
  }
  // Clear the kTimeout flag to reset shared state.
  server_accept_timeout.store(false);
  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc_,
    "TestFcgiClientInterfaceTestFixture", __LINE__);
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGALRM,
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
  // Establish a self-kill timer to ensure process termination regardless of
  // potential errors in the parent.
  alarm(3U);
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
  // Actions:
  // 0) Perform observer updates checks throughout.
  // 1) Send an FCGI_GET_VALUES request through SendGetValuesRequest.
  // 2) Send an unknown mangement request through SendBinaryManagementRequest.
  // 3) Send three application requests.
  // 4) Update pointed-to state.
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter_ptr->SendGetValuesRequest(
    observer_ptr->connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  observer_ptr->management_request_count += 1U;
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
  observer_ptr->management_request_count += 1U;
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
  struct ClientInterfaceObserverValues total_observer
  {
    *observer_ptr,
    *total_completed_ptr,
    *total_pending_ptr
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    *client_inter_ptr, total_observer, __LINE__));
}

void GTestNonFatalStreamDataComparison
(
  const std::uint8_t*              reference_stream_begin,
  const std::uint8_t*              reference_stream_end,
  const std::vector<std::uint8_t>& response_stream,
  int                              invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestNonFatalStreamDataComparison"};
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
    ADD_FAILURE() << "Stream length mismatch";
  }
};

void GTestNonFatalExerciseResponseCompare(
  const struct FcgiRequestDataReference sent_request_ref,
  FcgiResponse* app_response_ptr,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestNonFatalExerciseResponseCompare"};

  // Returned FcgiRequestDataReference instance
  const struct FcgiRequestDataReference& request_ref
    {app_response_ptr->Request()};
  EXPECT_EQ(request_ref.role,           sent_request_ref.role);
  EXPECT_EQ(request_ref.keep_conn,      sent_request_ref.keep_conn);
  EXPECT_EQ(request_ref.params_map_ptr, sent_request_ref.params_map_ptr);
  EXPECT_EQ(request_ref.stdin_begin,    sent_request_ref.stdin_begin);
  EXPECT_EQ(request_ref.stdin_end,      sent_request_ref.stdin_end);
  EXPECT_EQ(request_ref.data_begin,     sent_request_ref.data_begin);
  EXPECT_EQ(request_ref.data_end,       sent_request_ref.data_end);
  // Application status
  EXPECT_EQ(app_response_ptr->AppStatus(), EXIT_SUCCESS);
  // Protocol status
  EXPECT_EQ(app_response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
  // FCGI_STDERR and FCGI_STDOUT
  GTestNonFatalStreamDataComparison(sent_request_ref.data_begin,
    sent_request_ref.data_end, app_response_ptr->FcgiStderr(), __LINE__);
  GTestNonFatalStreamDataComparison(sent_request_ref.stdin_begin,
    sent_request_ref.stdin_end, app_response_ptr->FcgiStdout(), __LINE__);
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalServerAcceptLoop(inter_ptr,
    &OperationForExpectNone, __LINE__));
}

void GTestFatalAcceptRequestsRequestEcho(FcgiServerInterface* inter_ptr,
  const ParamsMap& sent_environ, int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalAcceptRequestsExerciseRequestEcho"};

  auto OperationForRequestEcho = [&sent_environ]
  (
    std::vector<FcgiRequest>* accept_buffer_ptr
  )->void
  {
    for(std::vector<FcgiRequest>::iterator iter {accept_buffer_ptr->begin()};
      iter != accept_buffer_ptr->end(); ++iter)
    {
      EXPECT_EQ(iter->get_environment_map(), sent_environ);
      const std::vector<std::uint8_t>& stdin_ref {iter->get_STDIN()};
      const std::vector<std::uint8_t>& data_ref  {iter->get_DATA()};
      int write_count {0};
      // Convert the returned boolean write status to an integer.
      // Check that all writes were successful.
      write_count += iter->Write(stdin_ref.begin(), stdin_ref.end());
      write_count += iter->WriteError(data_ref.begin(), data_ref.end());
      write_count += iter->Complete(EXIT_SUCCESS);
      ASSERT_EQ(write_count, 3);
    }
  };
  std::function<void(std::vector<FcgiRequest>*)> local_op
    {OperationForRequestEcho};
  GTestFatalServerAcceptLoop(inter_ptr, local_op, __LINE__);
}

// A helper function to reap a child process which was created by the test
// process during testing.
//
//
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

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
