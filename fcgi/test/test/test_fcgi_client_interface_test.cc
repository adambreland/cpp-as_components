#include "test/include/test_fcgi_client_interface.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "external/a_component_testing/include/a_component_testing_utilities.h"
#include "external/a_component_testing/gtest/include/a_component_testing_gtest_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/fcgi_protocol_constants.h"
#include "test/include/fcgi_si_testing_utilities.h"

namespace a_component {
namespace fcgi {
namespace test {
namespace test {

namespace {

std::atomic<bool> test_fcgi_client_interface_fcgi_server_accept_timeout
  {false};

extern "C" void SigAlrmHandler(int sig)
{
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(true);
}

struct ClientInterfaceConnectionObserverTestValues
{
  int         connection;
  std::size_t completed_request_count;
  int         connection_count;
  bool        is_connected;
  std::size_t management_request_count;
  std::size_t pending_request_count;
  std::size_t ready_event_count;
};

void GTestFatalClientInterfaceConnectionObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceConnectionObserverTestValues& values,
  int source_line)
{
  constexpr const char* error_prefix
    {"GTestFatalClientInterfaceConnectionObserverCheck, source line: "};

  std::size_t completed_request_count_return {};
  ASSERT_NO_THROW(completed_request_count_return =
    client_inter.CompletedRequestCount(values.connection)) << 
    error_prefix << std::to_string(source_line);
  EXPECT_EQ(completed_request_count_return, values.completed_request_count)
    << error_prefix << std::to_string(source_line);

  EXPECT_EQ(client_inter.ConnectionCount(), values.connection_count) <<
    error_prefix << std::to_string(source_line);

  bool is_connected_return {};
  ASSERT_NO_THROW(is_connected_return =
    client_inter.IsConnected(values.connection)) << error_prefix <<
    std::to_string(source_line);
  EXPECT_EQ(is_connected_return, values.is_connected) << error_prefix <<
    std::to_string(source_line);

  std::size_t management_request_count_return {};
  ASSERT_NO_THROW(management_request_count_return =
    client_inter.ManagementRequestCount(values.connection)) << error_prefix
    << std::to_string(source_line);
  EXPECT_EQ(management_request_count_return, values.management_request_count)
    << error_prefix << std::to_string(source_line);

  std::size_t pending_request_count_return {};
  ASSERT_NO_THROW(pending_request_count_return =
    client_inter.PendingRequestCount(values.connection)) << error_prefix <<
    std::to_string(source_line);
  EXPECT_EQ(pending_request_count_return, values.pending_request_count) <<
    error_prefix << std::to_string(source_line);

  EXPECT_EQ(client_inter.ReadyEventCount(), values.ready_event_count) <<
    error_prefix << std::to_string(source_line);
}

} // namespace

// A Google Test test fixture.
class TestFcgiClientInterfaceManagementRequests : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    testing::gtest::GTestFatalIgnoreSignal(SIGPIPE);
    testing::gtest::GTestFatalSetSignalDisposition(SIGALRM, &SigAlrmHandler);
    ASSERT_TRUE(test_fcgi_client_interface_fcgi_server_accept_timeout.
      is_lock_free());
    // Ensure that the the timeout flag is cleared.
    test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  }

  void TearDown() override
  {
    for(std::vector<std::pair<int, const char*>>::iterator iter
      {resource_list_.begin()}; iter != resource_list_.end(); ++iter)
    {
      close(iter->first);
      EXPECT_NE(unlink(iter->second), -1) << std::strerror(errno);
    }
    // Clear the timeout flag to reset shared state.
    test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
    testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc_,
      "TestFcgiClientInterfaceManagementRequestTests");
    testing::gtest::GTestFatalRestoreSignal(SIGALRM);
    testing::gtest::GTestFatalRestoreSignal(SIGPIPE);
  }

  std::vector<std::pair<int, const char*>> resource_list_ {};
  testing::FileDescriptorLeakChecker fdlc_ {};
};

namespace {
  // AF_UNIX files cannot be created in the Bazel temporary file directory
  // because its name is too long.
  const char* unix_path_1 {"/tmp/TestFcgiClientInterfaceManagementRequests1"};
  const char* unix_path_2 {"/tmp/TestFcgiClientInterfaceManagementRequests2"};
} // namespace

// Management request tests
//  1) Management requests and responses each use a single FastCGI record.
//     Because of this, tests which examine correct interface behavior when
//     a response is received over multiple records are not relevant.
//  2) Behavior which may depend on interface socket domain is not tested for
//     management requests. Such behavior is tested in the tests for Connect.
//  3) All management requests on a connection are associated with the
//     management request queue of the connection. Management request tests can
//     help to answer these questions:
//     a) Does the interface send a management request over the proper
//        connection and, hence, to the proper server?
//     b) Does the interface update the proper management request queue?
//     c) Does the interface preserve the order of management requests? This
//        question involves the transmission of multiple requests and the
//        reception of multiple responses over a given connection.
//     d) Can the interface handle distinct management requests over separate
//        connections when those connections connect to the same server?
//     e) Can the interface handle distinct management requests over separate
//        connections when those connections connect to distinct servers?
//  4)    Connection closure is specified to imply certain actions by an
//     interface. In particular, pending management requests on a connection
//     should be cleared when the connection is either found to be closed or is
//     closed by the application.
//        One situation requires information associated with a connection to be
//     saved across a connection closure: the presence of unreleased, completed
//     requests on the connection. To ensure that the process used to save
//     this information does not erroneously influence how management requests
//     are handled, the following test should be implemented.
//        A management request is made on a connection with an unreleased,
//     completed request. The connection is closed. A new connection is made
//     which uses the same socket descriptor as the prior connection. A
//     management request which should elicit a distinct response from that of
//     the prior request is made. The server is allowed to respond to the
//     request. The validity of the response instance is checked.

// SendBinaryManagementRequest
// Test cases:
//

// SendGetValuesRequest
// Test cases:
// Test case set 1:
//  1) A single request is made with the copy overload. The response is
//     retrieved and verified. To test the specified behavior for non-empty
//     input map values, such a value is present in the input map.
//  2) As 1, but the move overload is used.
//  3) To ensure that the client interface correctly handles request order,
//     two distinct requests are made in series of the same server interface.
//     The server interface is then allowed to process the requests. It is
//     verified that the client interface returns the correct responses in the
//     order in which the requests were enqueued.
//  4) The client interface must correctly handle the presence of multiple
//     connections to the same server. Two connections are made to the same
//     server. Distinct requests are sent to each connection. The appropriate
//     responses are verified.
//  5) At least one call is made. Before the response to the call is retrieved,
//     the connection over which the call was made is close by the application
//     from a call to CloseConnection. The clearance of the management request
//     queue for the connection is verified.
//  6) The client interface must correctly handle the presence of multiple,
//     distinct server interfaces. Two server interfaces are created and each
//     is sent a request with distinct content. The appropriate responses are
//     verified.
//  7) (Failure case) A call is made with a value for connection which does not
//     refer to a connection which is managed by the interface.
//  8) (Failure case) A call is made with names which result in FCGI_GET_VALUES
//     record content which cannot be encoded in a single FCGI_GET_VALUES
//     record. A single large name is used for simplicity.
//  9) (Failure case) A call is made after the peer closed the connection.
//     Correct detection of connection closure and correct interface state
//     update, which includes clearing the queue of unanswered requests and the
//     enqueuement of an appropriate ConnectionClosure instance, are verified.
//
// Test case set 2:
// 10) To ensure that the client interface correctly handles connection
//     closure by the client interface user in the case that FastCGI
//     identifier values are allocated for a connection, an FCGI_GET_VALUES
//     request is made when a pending application request is present. Then
//     the connection is closed by the user. A new connection with the same
//     descriptor value is made. Then a management request is made which
//     should have a response which is distinct from the response that would
//     have been returned for the previous request. It is verified that the
//     correct response it returned.
//
// Test case set 3:
// 11) An FCGI_GET_VALUES request is made and a response with a FastCGI
//     name-value pair encoding error is sent. The detection of this error and
//     the specified behavior of the GetValuesResult instance are verified.
//     In particular, for GetValuesResult instance gvr, gvr.IsCorrupt() == true
//     and gvr.ResponseMap() == ParamsMap {}.
//
// Modules which testing depends on:
//  1) Receipt of and response to FCGI_GET_VALUES requests by
//     fcgi_si::FcgiServerInterface.
//  2) The immediate detection of peer closure by the implementation of
//     local (AF_UNIX) sockets. (The Linux internet socket implementations do
//     not currently (October 2020) possess this property.)
//  3) The immediate success or failure of connection to a listening socket
//     by the implementation of local (AF_UNIX) sockets. In particular, that a
//     call to accept in the server process is not required for the call of
//     connect by the client to return. This allows testing to be performed in
//     a single process without (inappropriately) forcing the implementation
//     of Connect to make the would-be connected socket non-blocking before the
//     connection is attempted.
// Other modules whose testing depends on this module: none.

namespace {

void GTestFatalCheckGetValuesResult(const GetValuesResult* gvr_ptr,
  int connection, const ParamsMap& request_map, const ParamsMap& response_map,
  int source_line)
{
  constexpr const char* error_prefix
    {"GTestFatalCheckGetValuesResult, source line : "};
  ASSERT_NE(gvr_ptr, nullptr) << error_prefix << std::to_string(source_line);
  ASSERT_FALSE(gvr_ptr->IsCorrupt()) << error_prefix <<
    std::to_string(source_line);
  EXPECT_EQ((FcgiRequestIdentifier {connection, 0U}), gvr_ptr->RequestId()) <<
    error_prefix << std::to_string(source_line);
  EXPECT_EQ(request_map, gvr_ptr->RequestMap()) << error_prefix <<
    std::to_string(source_line);
  EXPECT_EQ(response_map, gvr_ptr->ResponseMap()) << error_prefix <<
    std::to_string(source_line);
}

//    The loop until AcceptRequests blocks was found to be needed as a single
// call to AcceptRequests does not necessarily read all of the information
// of a management request. The timer set above and its associated
// SIGALRM signal are used to break out of the blocked AcceptRequests call
// and the loop.
//    Note that this implementation also tests the specified behavior of
// AcceptRequests when it has blocked in an I/O multiplexing call and the
// call fails with errno == EINTR.
void GTestFatalAcceptRequestsExpectNone(FcgiServerInterface* inter_ptr,
  int source_line)
{
  constexpr const struct itimerval timeout
  {
    {0, 0},   // it_interval (don't repeat)
    {0, 2000} // it_value (wait 2 ms)
  };
  constexpr const char* error_prefix
    {"GTestFatalAcceptRequestsExpectNone, source line: "};
  std::vector<FcgiRequest> accept_buffer {};

  test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno) << '\n' << error_prefix << std::to_string(source_line);
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_buffer = inter_ptr->AcceptRequests()) <<
      error_prefix << std::to_string(source_line);
    EXPECT_EQ(accept_buffer.size(), 0U) <<
      error_prefix << std::to_string(source_line);
    accept_buffer.clear();
  }
}

std::atomic<bool> hangup_received_from_parent_for_get_values_requests {false};

extern "C" void SigChildHangUp(int sig)
{
  hangup_received_from_parent_for_get_values_requests.store(true);
}

} // namespace

class SendGetValuesRequestTestCaseSet1 : public
  TestFcgiClientInterfaceManagementRequests
{
 protected:
  void SetUp() override
  {
    TestFcgiClientInterfaceManagementRequests::SetUp();
    testing::gtest::GTestFatalSetSignalDisposition(SIGHUP, &SigChildHangUp);
  }

  void TearDown() override
  {
    TestFcgiClientInterfaceManagementRequests::TearDown();
    testing::gtest::GTestFatalRestoreSignal(SIGHUP);
  }
};

// SendGetValuesRequest: Test case set 1.
TEST_F(SendGetValuesRequestTestCaseSet1,
  SendGetValuesRequestTestCaseSet1)
{
  //    Create server interfaces to respond to FCGI_GET_VALUES requests sent
  // by a client interface.
  //    Forking seems to prevent the normal TearDown process from working.
  // The AF_UNIX file descriptor is scheduled for release in the parent process
  // as a result. A dummy file descriptor is used (-1).
  resource_list_.push_back({-1, unix_path_2});
  pid_t fork_return {fork()};
  if(fork_return == 0) // child
  {
    // Create a second server interface in a separate process before the
    // "first" interface is created.
    struct InterfaceCreationArguments second_inter_args {};
    second_inter_args.domain          = AF_UNIX;
    second_inter_args.backlog         = 5;
    second_inter_args.max_connections = 10;
    second_inter_args.max_requests    = 100;
    second_inter_args.app_status      = EXIT_FAILURE;
    second_inter_args.unix_path       = unix_path_2;

    std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    creation_return {};
    try
    {
      creation_return = GTestNonFatalCreateInterface(second_inter_args);
    }
    catch(...)
    {
      _exit(EXIT_FAILURE);
    }
    std::unique_ptr<FcgiServerInterface> remote_inter_uptr
      {std::move(std::get<0>(creation_return))};
    if(remote_inter_uptr == std::unique_ptr<FcgiServerInterface> {})
    {
      _exit(EXIT_FAILURE);
    }
    std::vector<FcgiRequest> request_buffer {};
    while(true)
    {
      request_buffer = remote_inter_uptr->AcceptRequests();
      if(hangup_received_from_parent_for_get_values_requests.load())
      {
        break;
      }
    }
    _exit(EXIT_SUCCESS);
  }
  else if(fork_return == -1)
  {
    FAIL() << std::strerror(errno); // Can't be in the child here.
  }
  // else parent.

  struct InterfaceCreationArguments inter_args {};
  inter_args.domain          = AF_UNIX;
  inter_args.backlog         = 5;
  inter_args.max_connections = 10;
  inter_args.max_requests    = 100;
  inter_args.app_status      = EXIT_FAILURE;
  inter_args.unix_path       = unix_path_1;

  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  resource_list_.push_back({std::get<1>(inter_return), unix_path_1});

  // Create a client interface and check its initial observable state.
  TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.CompletedRequestCount(), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(),       0);
  EXPECT_EQ(client_inter.ReadyEventCount(),       0U);

  // Create maps for testing.
  ParamsMap name_only_map
  {
    {FCGI_MAX_CONNS, {}},
    {FCGI_MAX_REQS, {}},
    {FCGI_MPXS_CONNS, {}}
  };
  ParamsMap map_with_values {name_only_map};
  map_with_values[FCGI_MAX_CONNS]  = std::vector<std::uint8_t> {'1', '0'};
  map_with_values[FCGI_MAX_REQS]   = std::vector<std::uint8_t> {'1', '0', '0'};
  map_with_values[FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};
  ParamsMap mpxs_name_map
  {
    {FCGI_MPXS_CONNS, {}}
  };
  ParamsMap mpxs_map_with_value {mpxs_name_map};
  mpxs_map_with_value[FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};

  // TEST CASE 1
  // Test the copy overload first.
  int local_socket {};
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(unix_path_1, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  struct ClientInterfaceConnectionObserverTestValues observer_values {};
  observer_values.connection               = local_socket;
  observer_values.completed_request_count  = 0U;
  observer_values.connection_count         = 1;
  observer_values.is_connected             = true;
  observer_values.management_request_count = 0U;
  observer_values.pending_request_count    = 0U;
  observer_values.ready_event_count        = 0U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GTestFatalAcceptRequestsExpectNone(inter_uptr.get(), __LINE__);
  std::unique_ptr<ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  observer_values.ready_event_count        = 0U; // 1-1 (the returned req) = 0
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GetValuesResult* gvr_ptr
    {dynamic_cast<GetValuesResult*>(result_uptr.get())};
  GTestFatalCheckGetValuesResult(gvr_ptr, local_socket, name_only_map,
    map_with_values, __LINE__);

  // TEST CASE 2
  // Start testing the move overload.
  ParamsMap value_map_copy {map_with_values};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, std::move(value_map_copy)));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GTestFatalAcceptRequestsExpectNone(inter_uptr.get(), __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  observer_values.ready_event_count        = 0U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, local_socket, name_only_map,
    map_with_values, __LINE__);

  // TEST CASE 3
  // Send two requests.
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, mpxs_map_with_value));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 2U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  // Allow the interface to process the requests.
  GTestFatalAcceptRequestsExpectNone(inter_uptr.get(), __LINE__);
  // Retrieve the responses and check for proper response ordering.
  // All management requests should have been processed by the server interface
  // when AcceptRequests was called in the loop above. All data sent by the
  // server interface should have been processed by the above call to
  // RetrieveServerEvent. As a result, no management requests should be
  // regarded as pending for local_socket.
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  observer_values.ready_event_count        = 1U;  
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, local_socket, mpxs_name_map,
    mpxs_map_with_value, __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.ready_event_count        = 0U;  
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, local_socket, name_only_map,
    map_with_values, __LINE__);

  // TEST CASE 4
  int second_local_socket {};
  ASSERT_NO_THROW(second_local_socket = client_inter.Connect(unix_path_1, 0U));
  ASSERT_NE(second_local_socket, -1) << std::strerror(errno);
  observer_values.connection_count         = 2;
  struct ClientInterfaceConnectionObserverTestValues second_observer {};
  second_observer.connection               = second_local_socket;
  second_observer.completed_request_count  = 0U;
  second_observer.connection_count         = 2;
  second_observer.is_connected             = true;
  second_observer.management_request_count = 0U;
  second_observer.pending_request_count    = 0U;
  second_observer.ready_event_count        = 0U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    second_observer, __LINE__);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    map_with_values));
  ASSERT_TRUE(send_gvr);
  observer_values.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    second_local_socket, mpxs_map_with_value));
  ASSERT_TRUE(send_gvr);
  second_observer.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    second_observer, __LINE__);
  GTestFatalAcceptRequestsExpectNone(inter_uptr.get(), __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr, std::unique_ptr<ServerEvent> {});
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  int first_retrieved {gvr_ptr->RequestId().descriptor()};
  bool first_is_first_local {first_retrieved == local_socket};
  auto CyclicCheck = [&]
  (
    bool first,
    int connection,
    struct ClientInterfaceConnectionObserverTestValues* observer_ptr,
    int line
  )->void
  {
    if(first)
    {
      observer_ptr->management_request_count = 0U;
      GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
        *observer_ptr, line);
      GTestFatalCheckGetValuesResult(gvr_ptr, connection, name_only_map,
        map_with_values, line);
    }
    else
    {
      observer_ptr->management_request_count = 0U;
      GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
        *observer_ptr, line);
      GTestFatalCheckGetValuesResult(gvr_ptr, connection, mpxs_name_map,
        mpxs_map_with_value, line);
    }
  };
  if(first_is_first_local)
  {
    CyclicCheck(true, local_socket, &observer_values, __LINE__);
  }
  else
  {
    CyclicCheck(false, second_local_socket, &second_observer, __LINE__);
  }
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr, std::unique_ptr<ServerEvent> {});
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  if(first_is_first_local)
  {
    CyclicCheck(false, second_local_socket, &second_observer, __LINE__);
  }
  else
  {
    CyclicCheck(true, local_socket, &observer_values, __LINE__);
  }
  
  // TEST CASE 5
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(second_local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  second_observer.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    second_observer, __LINE__);
  bool connection_closure {};
  ASSERT_NO_THROW(connection_closure =
    client_inter.CloseConnection(second_local_socket));
  ASSERT_TRUE(connection_closure);
  observer_values.connection_count         = 1;
  second_observer.connection_count         = 1;
  second_observer.is_connected             = false;
  second_observer.management_request_count = 0U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    second_observer, __LINE__);
  
  // TEST CASE 6  
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(unix_path_2, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  observer_values.connection_count         = 2;
  struct ClientInterfaceConnectionObserverTestValues new_observer {};
  new_observer.connection               = new_connection;
  new_observer.completed_request_count  = 0U;
  new_observer.connection_count         = 2;
  new_observer.is_connected             = true;
  new_observer.management_request_count = 0U;
  new_observer.pending_request_count    = 0U;
  new_observer.ready_event_count        = 0U;
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    map_with_values));
  ASSERT_TRUE(send_gvr);
  observer_values.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    mpxs_map_with_value));
  ASSERT_TRUE(send_gvr);
  new_observer.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    new_observer, __LINE__);
  // Allow the servers to process the requests. The remote server should
  // automatically process the request which was sent to it. The 2 ms wait
  // during the invocation of GTestFatalAcceptRequestsExpectNone should allow
  // the server interface in the child process to process its request.
  GTestFatalAcceptRequestsExpectNone(inter_uptr.get(), __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr, std::unique_ptr<ServerEvent> {});
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  int retrieved_socket {gvr_ptr->RequestId().descriptor()};
  bool first_inter_first {retrieved_socket == local_socket};
  if(first_inter_first)
  {
    CyclicCheck(true, local_socket, &observer_values, __LINE__);
  }
  else
  {
    CyclicCheck(false, new_connection, &new_observer, __LINE__);
  }
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr, std::unique_ptr<ServerEvent> {});
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  if(first_inter_first)
  {
    CyclicCheck(false, new_connection, &new_observer, __LINE__);
  }
  else
  {
    CyclicCheck(true, local_socket, &observer_values, __LINE__);
  }
  // Close the connection to second server.
  ASSERT_NO_THROW(client_inter.CloseConnection(new_connection));
  new_observer.connection_count         = 1;
  new_observer.is_connected             = false;
  new_observer.management_request_count = 0U;
  observer_values.connection_count      = 1;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    new_observer, __LINE__);
  int waitpid_return {waitpid(fork_return, nullptr, WNOHANG)};
  if((waitpid_return != -1))
  {
    if(waitpid_return == 0)
    {
      if(kill(fork_return, SIGHUP) == -1)
      {
        FAIL() << std::strerror(errno);
      }
    }
    else
    {
      ADD_FAILURE() << "The child aborted unexpectedly.";
    }
  }
  else
  {
    FAIL() << std::strerror(errno);
  }

  // TEST CASE 7 (Failure case)
  // Check for a return of false when a call is made for a non-existent
  // connection.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    name_only_map));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);

  // TEST CASE 8 (Failure case)
  // Check for a return of false when a call is made with a map that cannot
  // be encoded in a single record.
  std::vector<std::uint8_t>
    large_name(kMaxRecordContentByteLength + 1, 1U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
    large_name_map {{std::move(large_name), {1U}}};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);

  // TEST CASE 9 (Failure case)
  //    Check for a return of false when a call is made and it is detected that
  // the peer closed the connection.
  //    Destroy the FcgiServerInterface instance to cause the peer connection
  // to be closed.
  //    Before destroying the interface, make a request to allow a check that
  // the count is cleared upon the detection of destruction to be performed.
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Check for constancy.
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    name_only_map));
  EXPECT_FALSE(send_gvr);
  observer_values.connection_count         = 0;
  observer_values.is_connected             = false;
  observer_values.management_request_count = 0U;
  observer_values.ready_event_count        = 1U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ConnectionClosure* closure_ptr
    {dynamic_cast<ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  observer_values.ready_event_count = 0U;
  GTestFatalClientInterfaceConnectionObserverCheck(client_inter,
    observer_values, __LINE__);
}

// SendGetValuesRequest: Test case set 2
TEST_F(TestFcgiClientInterfaceManagementRequests,
  SendGetValuesRequestTestCaseSet2)
{

}

// SendGetValuesRequest: Test case set 3
TEST_F(TestFcgiClientInterfaceManagementRequests,
  SendGetValuesRequestTestCaseSet3)
{

}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
