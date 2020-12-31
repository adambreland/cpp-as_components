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
#include "external/socket_functions/include/socket_functions.h"

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
      EXPECT_EQ(expected_mismatch_ends, std::mismatch(end_content.begin(),
        end_content.end(), expected_values.content_buffer_ptr));
    }
    EXPECT_EQ(invalid_record_ptr->PaddingLength(),
      expected_values.padding_length);
    EXPECT_EQ(invalid_record_ptr->RequestId(), expected_values.id);
    EXPECT_EQ(invalid_record_ptr->Type(), expected_values.type);
    EXPECT_EQ(invalid_record_ptr->Version(), expected_values.version);
  }
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
