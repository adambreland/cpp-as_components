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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "external/a_component_testing/include/a_component_testing_utilities.h"
#include "external/a_component_testing/gtest/include/a_component_testing_gtest_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_protocol_constants.h"
#include "test/include/fcgi_si_testing_utilities.h"

namespace a_component {
namespace fcgi {
namespace test {
namespace test {

namespace {

struct ClientInterfaceConnectionObserverTestValues
{
  int         connection;
  std::size_t total_completed_request_count;
  std::size_t connection_completed_request_count;
  int         connection_count;
  bool        is_connected;
  std::size_t management_request_count;
  std::size_t total_pending_request_count;
  std::size_t connection_pending_request_count;
  std::size_t ready_event_count;
};

void GTestFatalClientInterfaceConnectionObserverCheck(
  const TestFcgiClientInterface& client_inter,
  const struct ClientInterfaceConnectionObserverTestValues& values,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalClientInterfaceConnectionObserverCheck"};

  EXPECT_EQ(values.total_completed_request_count,
    client_inter.CompletedRequestCount());

  std::size_t connection_completed_request_count_return {};
  ASSERT_NO_THROW(connection_completed_request_count_return =
    client_inter.CompletedRequestCount(values.connection));
  EXPECT_EQ(connection_completed_request_count_return,
    values.connection_completed_request_count);

  EXPECT_EQ(client_inter.ConnectionCount(), values.connection_count);

  bool is_connected_return {};
  ASSERT_NO_THROW(is_connected_return =
    client_inter.IsConnected(values.connection));
  EXPECT_EQ(is_connected_return, values.is_connected);

  std::size_t management_request_count_return {};
  ASSERT_NO_THROW(management_request_count_return =
    client_inter.ManagementRequestCount(values.connection));
  EXPECT_EQ(management_request_count_return, values.management_request_count);

  EXPECT_EQ(values.total_pending_request_count,
    client_inter.PendingRequestCount());

  std::size_t connection_pending_request_count_return {};
  ASSERT_NO_THROW(connection_pending_request_count_return =
    client_inter.PendingRequestCount(values.connection));
  EXPECT_EQ(connection_pending_request_count_return,
    values.connection_pending_request_count);

  EXPECT_EQ(client_inter.ReadyEventCount(), values.ready_event_count);
}

std::atomic<bool> test_fcgi_client_interface_fcgi_server_accept_timeout
  {false};

extern "C" void SigAlrmHandler(int sig)
{
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(true);
}

// A Google Test test fixture.
class TestFcgiClientInterfaceTestFixture : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
      __LINE__));
    ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalSetSignalDisposition(
      SIGALRM, &SigAlrmHandler, __LINE__));
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
      "TestFcgiClientInterfaceManagementRequestTests", __LINE__);
    ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGALRM,
      __LINE__));
    ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGPIPE,
      __LINE__));
  }

  std::vector<std::pair<int, const char*>> resource_list_ {};
  testing::FileDescriptorLeakChecker fdlc_ {};
};

constexpr const struct InterfaceCreationArguments default_inter_args
{
  /* domain          = */ AF_UNSPEC,
  /* backlog         = */ 5,
  /* max_connections = */ 10,
  /* max_requests    = */ 100,
  /* app_status      = */ EXIT_FAILURE,
  /* unix_path       = */ nullptr,
};

// Create maps for testing which match the default server interface arguments.
const ParamsMap name_only_map
{
  {FCGI_MAX_CONNS, {}},
  {FCGI_MAX_REQS, {}},
  {FCGI_MPXS_CONNS, {}}
};
const ParamsMap map_with_values
{
  {FCGI_MAX_CONNS,  {'1', '0'}},
  {FCGI_MAX_REQS,   {'1', '0', '0'}},
  {FCGI_MPXS_CONNS, {'1'}}
};
const ParamsMap mpxs_name_map
{
  {FCGI_MPXS_CONNS, {}}
};
const ParamsMap mpxs_map_with_value
{
  {FCGI_MPXS_CONNS, {'1'}}
};

// AF_UNIX files cannot be created in the Bazel temporary file directory
// because its name is too long.
constexpr const char*const unix_path_1
  {"/tmp/TestFcgiClientInterfaceManagementRequests1"};
constexpr const char*const unix_path_2
  {"/tmp/TestFcgiClientInterfaceManagementRequests2"};

void GTestFatalCheckGetValuesResult(const GetValuesResult* gvr_ptr,
  bool corrupt, int connection, const ParamsMap& request_map,
  const ParamsMap& response_map, int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalCheckGetValuesResult"};
  ASSERT_NE(gvr_ptr, nullptr);
  ASSERT_EQ(gvr_ptr->IsCorrupt(), corrupt);
  EXPECT_EQ((FcgiRequestIdentifier {connection, 0U}), gvr_ptr->RequestId());
  EXPECT_EQ(request_map, gvr_ptr->RequestMap());
  EXPECT_EQ(response_map, gvr_ptr->ResponseMap());
}

constexpr const struct itimerval timeout
{
  {0, 0},   // it_interval (don't repeat)
  {0, 2000} // it_value (wait 2 ms)
};

//    The loop until AcceptRequests blocks was found to be needed as a single
// call to AcceptRequests does not necessarily read all of the information
// of a management request. The timer set above and its associated
// SIGALRM signal are used to break out of the blocked AcceptRequests call
// and the loop.
//    Note that this implementation also tests the specified behavior of
// AcceptRequests when it has blocked in an I/O multiplexing call and the
// call fails with errno == EINTR.
void GTestFatalAcceptRequestsExpectNone(FcgiServerInterface* inter_ptr,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalAcceptRequestsExpectNone"};
  std::vector<FcgiRequest> accept_buffer {};
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_buffer = inter_ptr->AcceptRequests());
    EXPECT_EQ(accept_buffer.size(), 0U);
    accept_buffer.clear();
  }
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

void GTestFatalExerciseTestFcgiClientInterface(
  TestFcgiClientInterface* client_inter_ptr,
  struct ClientInterfaceConnectionObserverTestValues* observer_ptr,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestFatalExerciseTestFcgiClientInterface"};
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter_ptr->SendGetValuesRequest(
    observer_ptr->connection, map_with_values));
  EXPECT_TRUE(send_gvr);
  observer_ptr->management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    *client_inter_ptr, *observer_ptr, __LINE__));
  


}

} // namespace

// Connect
// Examined properties:
// 1) Appropriate updates to observable state.
// 2) Proper functioning of the interface for each of AF_INET, AF_INET6, and
//    AF_UNIX. A test for proper functioning for a given domain will examine:
//    a) The ability to connect to the domain.
//    b) The ability to connect more than once to a given server on the domain.
//    c) The ability to perform each of the other major interface actions:
//       1) Transmission of an FCGI_GET_VALUES request through invocation of
//          SendGetValuesRequest.
//       2) Transmission of an unknown management request through invocation of
//          SendBinaryManagementRequest.
//       3) Transmission of several non-empty application requests through
//          invocation of SendRequest.
//       4) Closure of the connection through invocation of CloseConnection.
// 3) The ability to have multiple, simultaneous connections and simultaneous
//    connections to different domains. The properties described above can be
//    tested on a sigle interface with simultaneous connections to each domain
//    to test these abilities.
// 4) The ability to connect correctly when a previous connection to a server
//    which has been closed.
// 5) The detection of connection closure by a server for each domain.
// 6) The non-blocking status of returned file descriptors.
// 7) For internet sockets, proper handling of connection interruption by
//    signal receipt (situations which cause blocking system calls to fail with
//    errno == EINTR).
// 8) Failure cases: an attempt to connect to an address for which there is no
//    listening server.
//    a) For AF_UNIX, a special case is the existence of a file with the
//       connection file path which is not associated with a listening server.
//
// Test cases:
// 1) A server for each of the three supported domains is created in a separate
//    process. These servers call AcceptRequests in a loop. Connections are
//    made to these servers from a single client interface. Properties 1 to 6
//    are tested.
// 2) For each of AF_INET and AF_INET6, a server is created in a separate
//    process. The servers wait to begin processing requests until they receive
//    a signal to do so. A process which holds a client interface sets a timer
//    to interrupt a call to Connect with a signal. It also sets a timer to
//    allow a server to begin processing requests. Proper behavior of the
//    Connect call regarding signal receipt is checked.
// 3) Failure as described above for each of the three domains is checked.
//    For AF_UNIX, failure is checked when the connection path is present and
//    absent in the file system.

TEST_F(TestFcgiClientInterfaceTestFixture, ConnectCase1)
{
  ASSERT_NO_THROW(resource_list_.push_back({-1, unix_path_1}));
  // All application requests will have shared FCGI_PARAMS values.
  ParamsMap shared_params
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
  constexpr int domain_count {3};
  constexpr int internet_domain_count {domain_count - 1};
  constexpr int domain_array[domain_count] = {AF_INET, AF_INET6, AF_UNIX};
  int pipes[internet_domain_count][2] = {}; // AF_UNIX doesn't need a pipe.
  for(int i {0}; i != internet_domain_count; ++i)
  {
    ASSERT_NE(pipe(pipes[i]), -1) << std::strerror(errno);
  }
  pid_t child_id_array[domain_count] = {};
  int server_index {0};
  for(/*no-op*/; server_index != domain_count; ++server_index)
  {
    child_id_array[server_index] = fork();
    if(-1 == child_id_array[server_index])
    {
      // In parent and an error occurred.
      FAIL() << std::strerror(errno);
    }
    else if(0 == child_id_array[server_index])
    {
      // In child. Break to child server logic.
      break;
    }
    // else, in parent. Loop.
  }
  if(server_index < domain_count) // A child.
  {
    try
    {
      ChildServerAlrmRestoreAndSelfKillSet();

      struct InterfaceCreationArguments inter_args {};
      inter_args.domain          = domain_array[server_index];
      inter_args.backlog         = 5;
      inter_args.max_connections = 10;
      inter_args.max_requests    = 100;
      inter_args.app_status      = EXIT_FAILURE;
      inter_args.unix_path       = unix_path_1; // Ignored for internet servers.
      std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
      inter_return {GTestNonFatalCreateInterface(inter_args, __LINE__)};
      std::unique_ptr<FcgiServerInterface> inter_uptr
        {std::move(std::get<0>(inter_return))};
      if(inter_uptr.get() == nullptr)
      {
        _exit(EXIT_FAILURE);
      }

      // Internet servers should write the ephemeral port back to the parent
      // process.
      if(server_index < internet_domain_count)
      {
        // The port is in network byte order. We can byte-serialize it directly.
        std::uint8_t* port_ptr {static_cast<std::uint8_t*>(static_cast<void*>(
          &std::get<2>(inter_return)))};
        if(socket_functions::SocketWrite(pipes[server_index][1], port_ptr,
          sizeof(in_port_t)) < sizeof(in_port_t))
        {
          _exit(EXIT_FAILURE);
        }
      }
      // Close all of the pipes.
      for(int i {0}; i != internet_domain_count; ++i)
      {
        close(pipes[i][0]);
        close(pipes[i][1]);
      }
      std::vector<FcgiRequest> requests {};
      while(true)
      {
        // A server will block in a call to AcceptRequests before it is killed.
        requests = inter_uptr->AcceptRequests();
        for(std::vector<FcgiRequest>::iterator iter {requests.begin()};
          iter != requests.end(); ++iter)
        {
          if(iter->get_environment_map() != shared_params)
          {
            _exit(EXIT_FAILURE);
          }
          const std::vector<std::uint8_t>& stdin_ref {iter->get_STDIN()};
          const std::vector<std::uint8_t>& data_ref  {iter->get_DATA()};
          int write_count {0};
          write_count += iter->Write(stdin_ref.begin(), stdin_ref.end());
          write_count += iter->WriteError(data_ref.begin(), data_ref.end());
          write_count += iter->Complete(EXIT_SUCCESS);
          if(write_count < 3)
          {
            _exit(EXIT_FAILURE);
          }
        }
      }
    }
    catch(...)
    {
      _exit(EXIT_FAILURE);
    }
  }
  // else, in parent.
  // Wait to receive port values from the internet servers.
  in_port_t ports[internet_domain_count] = {};
  for(int i {0}; i < internet_domain_count; ++i)
  {
    close(pipes[i][1]);
    ASSERT_EQ(
      socket_functions::SocketRead(
        pipes[i][0],
        static_cast<std::uint8_t*>(static_cast<void*>(&ports[i])),
        sizeof(in_port_t)
      ),
      sizeof(in_port_t)
    ) << std::strerror(errno);
    close(pipes[i][0]);
  }
  // Connect to all of the servers twice.
}

// Management request testing discussion:
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
// 1) A request of each of the following kinds is made with the copy overload:
//    a) An FCGI_GET_VALUES request.
//    b) A request with a type which is not FCGI_GET_VALUES.
//    The FCGI_GET_VALUES request should be recognized as such even though it
//    was not made with SendGetValuesRequest. The unknown request should cause
//    an FCGI_UNKNOWN_TYPE request to be sent.
// 2) As 1, but the move overload is used.
// 3)    An FCGI_UNKNOWN_TYPE record is received in response to a management
//    request with an unknown type sent by SendBinaryManagementRequest. The
//    record is malformed. It is recognized as such and an InvalidRecord
//    instance is generated upon its receipt.
//       Two kinds of malformations may be distinguished which are specific to
//    FCGI_UNKNOWN_TYPE records:
//    a) The FCGI_UNKNOWN_TYPE record has no content (content length is zero).
//    b) The FCGI_UNKNOWN_TYPE record has a content length which is non-zero
//       and not equal to 8.
// 4) (Failure case) A call is made with a value for connection which does not
//    refer to a connection which is managed by the interface.
// 5) (Failure case) A call is made with a binary sequence length which is too
//    long to be sent in a single FastCGI record.
// 6) (Failure case) A call is made when the server has closed the connection.
//    Detection of closure and enqueuement of an appropriate ConnectionClosure
//    instance is verified.
// Note: Tests which are related to general management request handling, such
// as correct management queue use and correct management request queue update
// upon connection closure, are not tested here. Such tests are present for
// SendGetValuesResult. Similar tests for SendBinaryManagementRequest may be
// implemented at a later time if it is deemed worthwhile.
TEST_F(TestFcgiClientInterfaceTestFixture, SendBinaryManagementRequest)
{
  
}

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
//     closure by the client interface user in the case completed and
//     unreleased requests are present, an FCGI_GET_VALUES request is made when
//     such an application request is present. Then the connection is closed by
//     the user. A new connection with the same descriptor value is made. Then
//     a management request is made which should have a response which is
//     distinct from the response that would have been returned for the
//     previous request. It is verified that the correct response it returned.
// 11) As 10, but connection closure is performed by the server and detected
//     by the client interface.
//
// Test case set 3:
// 12) An FCGI_GET_VALUES request is made and a response with a FastCGI
//     name-value pair encoding error is sent. The detection of this error and
//     the specified behavior of the GetValuesResult instance are verified.
//     In particular, for GetValuesResult instance gvr, gvr.IsCorrupt() == true
//     and gvr.ResponseMap() == ParamsMap {}.
//
// Modules and features which testing depends on:
//  1) The immediate detection of peer closure by the implementation of
//     local (AF_UNIX) sockets.
//  2) The immediate success or failure of connection to a listening socket
//     by the implementation of local (AF_UNIX) sockets. In particular, that a
//     call to accept in the server process is not required for the call of
//     connect by the client to return. This allows testing to be performed in
//     a single process without (inappropriately) forcing the implementation
//     of Connect to make the would-be connected socket non-blocking before the
//     connection is attempted.
//  Server interface behavior:
//  3) Receipt of and response to FCGI_GET_VALUES requests by
//     ::a_component::fcgi::FcgiServerInterface.
//  4) FcgiRequest object production through calls to
//     ::a_component::fcgi::FcgiServerInterface::AcceptRequests
//     and correct transmission of terminal stream records and an
//     FCGI_END_REQUEST record by ::a_component::fcgi::FcgiRequest::Complete.
// Other modules whose testing depends on this module: none.

// SendGetValuesRequest: Test case set 1.
TEST_F(TestFcgiClientInterfaceTestFixture, SendGetValuesRequestTestCaseSet1)
{
  //    Create server interfaces to respond to FCGI_GET_VALUES requests sent
  // by a client interface.
  //    The child process is forked to house the second interface. The child
  // is killed when the second interface is no longer needed. SIGKILL is sent
  // by the parent. This behavior does not allow the cleanup mechanism of
  // Google Test test fixtures to be performed. The AF_UNIX file descriptor
  // of the child server interface is scheduled for release in the parent
  // process as a result. A dummy file descriptor is used (-1) as the listening
  // socket descriptor of the second server interface is closed when the child
  // process is killed.
  ASSERT_NO_THROW(resource_list_.push_back({-1, unix_path_2}));
  pid_t fork_return {fork()};
  if(fork_return == 0) // child
  {
    ChildServerAlrmRestoreAndSelfKillSet();

    struct InterfaceCreationArguments second_inter_args {default_inter_args};
    second_inter_args.domain          = AF_UNIX;
    second_inter_args.unix_path       = unix_path_2;

    std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    creation_return {};
    try
    {
      creation_return = GTestNonFatalCreateInterface(second_inter_args,
        __LINE__);
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
      // The child will block in a call to AcceptRequests before it is killed
      // by the parent.
      request_buffer = remote_inter_uptr->AcceptRequests();
    }
  }
  else if(fork_return == -1) // Can't be in the child here.
  {
    FAIL() << std::strerror(errno);
  }
  // else parent.
  struct InterfaceCreationArguments inter_args {default_inter_args};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = unix_path_1;

  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(inter_return),
    unix_path_1}));

  // Create a client interface and check its initial observable state.
  TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.CompletedRequestCount(), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(),       0);
  EXPECT_EQ(client_inter.ReadyEventCount(),       0U);

  // TEST CASE 1
  // Test the copy overload first.
  int local_socket {};
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(unix_path_1, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  struct ClientInterfaceConnectionObserverTestValues observer_values {};
  observer_values.connection                         = local_socket;
  observer_values.total_completed_request_count      = 0U;
  observer_values.connection_completed_request_count = 0U;
  observer_values.connection_count                   = 1;
  observer_values.is_connected                       = true;
  observer_values.management_request_count           = 0U;
  observer_values.total_pending_request_count        = 0U;
  observer_values.connection_pending_request_count   = 0U;
  observer_values.ready_event_count                  = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  std::unique_ptr<ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  GetValuesResult* gvr_ptr
    {dynamic_cast<GetValuesResult*>(result_uptr.get())};
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, name_only_map, map_with_values, __LINE__));

  // TEST CASE 2
  // Start testing the move overload.
  ParamsMap value_map_copy {map_with_values};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, std::move(value_map_copy)));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, name_only_map, map_with_values, __LINE__));

  // TEST CASE 3
  // Send two requests.
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, mpxs_map_with_value));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.management_request_count = 2U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  // Allow the interface to process the requests.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  // Retrieve the responses and check for proper response ordering.
  // All management requests should have been processed by the server interface
  // when AcceptRequests was called in the loop above. All data sent by the
  // server interface should have been processed by the above call to
  // RetrieveServerEvent. As a result, no management requests should be
  // regarded as pending for local_socket.
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  observer_values.ready_event_count        = 1U;  
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, mpxs_name_map, mpxs_map_with_value, __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.ready_event_count        = 0U;  
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, name_only_map, map_with_values, __LINE__));

  // TEST CASE 4
  int second_local_socket {};
  ASSERT_NO_THROW(second_local_socket = client_inter.Connect(unix_path_1, 0U));
  ASSERT_NE(second_local_socket, -1) << std::strerror(errno);
  observer_values.connection_count         = 2;
  struct ClientInterfaceConnectionObserverTestValues second_observer {};
  second_observer.connection                         = second_local_socket;
  second_observer.total_completed_request_count      = 0U;
  second_observer.connection_completed_request_count = 0U;
  second_observer.connection_count                   = 2;
  second_observer.is_connected                       = true;
  second_observer.management_request_count           = 0U;
  second_observer.total_pending_request_count        = 0U;
  second_observer.connection_pending_request_count   = 0U;
  second_observer.ready_event_count                  = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, second_observer, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    map_with_values));
  ASSERT_TRUE(send_gvr);
  observer_values.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    second_local_socket, mpxs_map_with_value));
  ASSERT_TRUE(send_gvr);
  second_observer.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, second_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
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
      ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
        client_inter, *observer_ptr, line));
      ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
        connection, name_only_map, map_with_values, line));
    }
    else
    {
      observer_ptr->management_request_count = 0U;
      ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
        client_inter, *observer_ptr, line));
      ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
        connection, mpxs_name_map, mpxs_map_with_value, line));
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, second_observer, __LINE__));
  bool connection_closure {};
  ASSERT_NO_THROW(connection_closure =
    client_inter.CloseConnection(second_local_socket));
  ASSERT_TRUE(connection_closure);
  observer_values.connection_count         = 1;
  second_observer.connection_count         = 1;
  second_observer.is_connected             = false;
  second_observer.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, second_observer, __LINE__));
  
  // TEST CASE 6  
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(unix_path_2, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  observer_values.connection_count         = 2;
  struct ClientInterfaceConnectionObserverTestValues new_observer {};
  new_observer.connection                         = new_connection;
  new_observer.total_completed_request_count      = 0U;
  new_observer.connection_completed_request_count = 0U;
  new_observer.connection_count                   = 2;
  new_observer.is_connected                       = true;
  new_observer.management_request_count           = 0U;
  new_observer.total_pending_request_count        = 0U;
  new_observer.connection_pending_request_count   = 0U;
  new_observer.ready_event_count                  = 0U;
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    map_with_values));
  ASSERT_TRUE(send_gvr);
  observer_values.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    mpxs_map_with_value));
  ASSERT_TRUE(send_gvr);
  new_observer.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  // Allow the servers to process the requests. The remote server should
  // automatically process the request which was sent to it. The 2 ms wait
  // during the invocation of GTestFatalAcceptRequestsExpectNone should allow
  // the server interface in the child process to process its request.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  int status {0};
  int waitpid_return {waitpid(fork_return, nullptr, WNOHANG)};
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
      if(kill(fork_return, SIGKILL) == -1)
      {
        FAIL() << std::strerror(errno);
      }
      // Reap the child once it terminates.
      while(((waitpid_return = waitpid(fork_return, nullptr, 0)) == -1) &&
            (errno == EINTR))
        continue;
      ASSERT_NE(waitpid_return, -1) << std::strerror(errno);
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  // Test the move overload.
  ParamsMap name_only_copy {name_only_map};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    std::move(name_only_copy)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));

  // TEST CASE 8 (Failure case)
  // Check for a return of false when a call is made with a map that cannot
  // be encoded in a single record.
  std::vector<std::uint8_t>
    large_name(kMaxRecordContentByteLength + 1, 1U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
    large_name_map {{std::move(large_name), {1U}}};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    large_name_map));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  // Test the move overload.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));

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
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    name_only_map));
  EXPECT_FALSE(send_gvr);
  observer_values.connection_count         = 0;
  observer_values.is_connected             = false;
  observer_values.management_request_count = 0U;
  observer_values.ready_event_count        = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ConnectionClosure* closure_ptr
    {dynamic_cast<ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  observer_values.ready_event_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer_values, __LINE__));
}

// SendGetValuesRequest: Test case set 2
TEST_F(TestFcgiClientInterfaceTestFixture, SendGetValuesRequestTestCaseSet2)
{
  // TEST CASE 10
  struct InterfaceCreationArguments inter_args {default_inter_args};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = unix_path_1;
  // Create the server interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(inter_return),
    unix_path_1}));
  // Create a client interface and check its initial observable state.
  TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.CompletedRequestCount(), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(),       0);
  EXPECT_EQ(client_inter.ReadyEventCount(),       0U);
  // Connect to the server.
  int local_connection {};
  ASSERT_NO_THROW(local_connection = client_inter.Connect(unix_path_1, 0U));
  ASSERT_NE(local_connection, -1) << std::strerror(errno);
  struct ClientInterfaceConnectionObserverTestValues observer {};
  observer.connection                         = local_connection;
  observer.total_completed_request_count      = 0U;
  observer.connection_completed_request_count = 0U;
  observer.connection_count                   = 1;
  observer.is_connected                       = true;
  observer.management_request_count           = 0U;
  observer.total_pending_request_count        = 0U;
  observer.connection_pending_request_count   = 0U;
  observer.ready_event_count                  = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Send an application request.
  struct FcgiRequestDataReference request_data_ref {};
  request_data_ref.role           = FCGI_RESPONDER;
  request_data_ref.keep_conn      = true;
  request_data_ref.params_map_ptr = nullptr;
  request_data_ref.stdin_begin    = nullptr;
  request_data_ref.stdin_end      = nullptr;
  request_data_ref.data_begin     = nullptr;
  request_data_ref.data_end       = nullptr;
  FcgiRequestIdentifier app_req_id {};
  ASSERT_NO_THROW(app_req_id = client_inter.SendRequest(local_connection,
    request_data_ref));
  ASSERT_NE(app_req_id, FcgiRequestIdentifier {});
  EXPECT_EQ(app_req_id.descriptor(), local_connection);
  observer.total_pending_request_count       = 1U;
  observer.connection_pending_request_count  = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Allow the server to accept the request and generate an FcgiResponse object.
  std::vector<FcgiRequest> accept_buffer {};
  while(accept_buffer.size() == 0U)
  {
    ASSERT_NO_THROW(accept_buffer = inter_uptr->AcceptRequests());
  }
  EXPECT_EQ(accept_buffer.size(), 1U);
  bool complete_return {false};
  ASSERT_NO_THROW(complete_return = accept_buffer[0U].Complete(EXIT_SUCCESS));
  ASSERT_TRUE(complete_return);
  // Allow the client interface to process the response.
  std::unique_ptr<ServerEvent> response_uptr {};
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  FcgiResponse* fcgi_response_ptr {dynamic_cast<FcgiResponse*>(
      response_uptr.get())};
  ASSERT_NE(fcgi_response_ptr, nullptr);
  EXPECT_EQ(fcgi_response_ptr->AppStatus(), EXIT_SUCCESS);
  EXPECT_EQ(fcgi_response_ptr->FcgiStderr().size(), 0U);
  EXPECT_EQ(fcgi_response_ptr->FcgiStdout().size(), 0U);
  EXPECT_EQ(fcgi_response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
  // Check each field of FcgiRequestDataReference.
  EXPECT_EQ(fcgi_response_ptr->Request().role,
    request_data_ref.role);
  EXPECT_EQ(fcgi_response_ptr->Request().keep_conn,
    request_data_ref.keep_conn);
  EXPECT_EQ(fcgi_response_ptr->Request().params_map_ptr,
    request_data_ref.params_map_ptr);
  EXPECT_EQ(fcgi_response_ptr->Request().stdin_begin,
    request_data_ref.stdin_begin);
  EXPECT_EQ(fcgi_response_ptr->Request().stdin_end,
    request_data_ref.stdin_end);
  EXPECT_EQ(fcgi_response_ptr->Request().data_begin,
    request_data_ref.data_begin);
  EXPECT_EQ(fcgi_response_ptr->Request().data_end,
    request_data_ref.data_end);
  EXPECT_EQ(fcgi_response_ptr->RequestId(), app_req_id);
  observer.total_completed_request_count      = 1U;
  observer.connection_completed_request_count = 1U;
  observer.total_pending_request_count        = 0U;
  observer.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Send an FCGI_GET_VALUES request which will not be answered.
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    local_connection, map_with_values));
  ASSERT_TRUE(send_gvr);
  observer.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Send an application request which will not be answered.
  FcgiRequestIdentifier second_req_id {};
  ASSERT_NO_THROW(second_req_id = client_inter.SendRequest(local_connection,
    request_data_ref));
  ASSERT_NE(second_req_id, FcgiRequestIdentifier {});
  EXPECT_EQ(second_req_id.descriptor(), local_connection);
  EXPECT_NE(second_req_id.Fcgi_id(), app_req_id.Fcgi_id());
  observer.total_pending_request_count        = 1U;
  observer.connection_pending_request_count   = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Close the connection with a completed, unreleased request.
  bool close_return {false};
  ASSERT_NO_THROW(close_return =
    client_inter.CloseConnection(local_connection));
  ASSERT_TRUE(close_return);
  observer.connection_count                   = 0U;
  observer.is_connected                       = false;
  observer.management_request_count           = 0U;
  observer.total_pending_request_count        = 0U;
  observer.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Establish a new connection to the server. The same descriptor value should
  // be reused.
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(unix_path_1, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  ASSERT_EQ(new_connection, local_connection);
  struct ClientInterfaceConnectionObserverTestValues new_observer {};
  new_observer.connection                         = new_connection;
  new_observer.total_completed_request_count      = 1U;
  new_observer.connection_completed_request_count = 1U;
  new_observer.connection_count                   = 1;
  new_observer.is_connected                       = true;
  new_observer.management_request_count           = 0U;
  new_observer.total_pending_request_count        = 0U;
  new_observer.connection_pending_request_count   = 0U;
  new_observer.ready_event_count                  = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  // Send a management request which is distinct from the last one.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    mpxs_map_with_value));
  ASSERT_TRUE(send_gvr);
  new_observer.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  // Allow the server to process requests. An FcgiRequest object should not be
  // generated this time.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  // Allow the client to process the response.
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  GetValuesResult* gvr_ptr
    {dynamic_cast<GetValuesResult*>(response_uptr.get())};
  ASSERT_NE(gvr_ptr, nullptr);
  new_observer.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    new_connection, mpxs_name_map, mpxs_map_with_value, __LINE__));

  // TEST CASE 11
  // Send another application request and FCGI_GET_VALUES request which will
  // not be answered.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    new_connection, map_with_values));
  ASSERT_TRUE(send_gvr);
  new_observer.management_request_count = 1U;
  FcgiRequestIdentifier third_req_id {};
  ASSERT_NO_THROW(third_req_id = client_inter.SendRequest(new_connection,
    request_data_ref));
  ASSERT_NE(third_req_id, FcgiRequestIdentifier {});
  EXPECT_EQ(third_req_id.descriptor(), new_connection);
  EXPECT_NE(third_req_id.Fcgi_id(), app_req_id.Fcgi_id());
  new_observer.total_pending_request_count        = 1U;
  new_observer.connection_pending_request_count   = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  // Destroy the server.
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Allow the client to process server destruction.
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  ConnectionClosure* connection_closure_ptr {dynamic_cast<ConnectionClosure*>(
    response_uptr.get())};
  ASSERT_NE(connection_closure_ptr, nullptr);
  new_observer.connection_count                   = 0U;
  new_observer.is_connected                       = false;
  new_observer.management_request_count           = 0U;
  new_observer.total_pending_request_count        = 0U;
  new_observer.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, new_observer, __LINE__));
  EXPECT_EQ(connection_closure_ptr->RequestId().descriptor(), new_connection);
  // Create a new server interface.
  // Close the listening socket descriptor of the first server.
  close(resource_list_[0].first);
  resource_list_[0].first = -1;
  inter_args.unix_path = unix_path_2;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  new_inter_return {};
  ASSERT_NO_THROW(new_inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& new_inter_uptr
    {std::get<0>(new_inter_return)};
  ASSERT_NE(new_inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(new_inter_return),
    unix_path_2}));
  // Connect to the server.
  int third_connection {};
  ASSERT_NO_THROW(third_connection = client_inter.Connect(unix_path_2, 0U));
  ASSERT_NE(third_connection, -1) << std::strerror(errno);
  ASSERT_EQ(third_connection, local_connection);
  struct ClientInterfaceConnectionObserverTestValues third_observer {};
  third_observer.connection                         = third_connection;
  third_observer.total_completed_request_count      = 1U;
  third_observer.connection_completed_request_count = 1U;
  third_observer.connection_count                   = 1U;
  third_observer.is_connected                       = true;
  third_observer.management_request_count           = 0U;
  third_observer.total_pending_request_count        = 0U;
  third_observer.connection_pending_request_count   = 0U;
  third_observer.ready_event_count                  = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, third_observer, __LINE__));
  // Send a new management request and allow the interfaces to act.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(third_connection,
    mpxs_map_with_value));
  ASSERT_TRUE(send_gvr);
  third_observer.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, third_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
    new_inter_uptr.get(), __LINE__));
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  gvr_ptr = dynamic_cast<GetValuesResult*>(response_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  third_observer.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, third_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    new_connection, mpxs_name_map, mpxs_map_with_value, __LINE__));
  // Release the completed request.
  bool release_return {false};
  ASSERT_NO_THROW(release_return = client_inter.ReleaseId(app_req_id));
  EXPECT_TRUE(release_return);
  third_observer.total_completed_request_count        = 0U;
  third_observer.connection_completed_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, third_observer, __LINE__));
}

// SendGetValuesRequest: Test case set 3
TEST_F(TestFcgiClientInterfaceTestFixture, SendGetValuesRequestTestCaseSet3)
{
  // TEST CASE 12
  // The connected descriptor of the interface is used to allow an erroneous
  // response to be sent to the client interface.
  struct InterfaceCreationArguments inter_args {default_inter_args};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = unix_path_1;
  // Create the server interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(inter_return),
    unix_path_1}));
  TestFcgiClientInterface client_inter {};
  int local_connection {};
  ASSERT_NO_THROW(local_connection = client_inter.Connect(unix_path_1, 0U));
  ASSERT_NE(local_connection, -1) << std::strerror(errno);
  struct ClientInterfaceConnectionObserverTestValues observer {};
  observer.connection                         = local_connection;
  observer.total_completed_request_count      = 0U;
  observer.connection_completed_request_count = 0U;
  observer.connection_count                   = 1;
  observer.is_connected                       = true;
  observer.management_request_count           = 0U;
  observer.total_pending_request_count        = 0U;
  observer.connection_pending_request_count   = 0U;
  observer.ready_event_count                  = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Allow the server to process the connection before sending a management
  // request.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    local_connection, map_with_values));
  ASSERT_TRUE(send_gvr);
  observer.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  // Construct and write an erroneous response.
  // A header, 2 bytes for name and value lenths, and 14 bytes for the
  // name FCGI_MAX_CONNS. This gives 24 bytes. No padding is required.
  constexpr std::size_t buffer_length {static_cast<std::size_t>(
    FCGI_HEADER_LEN + 2 + 14)};
  std::uint8_t response_buffer[buffer_length] = {};
  PopulateHeader(response_buffer, FcgiType::kFCGI_GET_VALUES_RESULT, 0U,
    16U, 0U);
  response_buffer[FCGI_HEADER_LEN]      = FCGI_MAX_CONNS.size();
  response_buffer[FCGI_HEADER_LEN + 1U] = 100U; // erroneous value length
  std::memcpy(response_buffer + FCGI_HEADER_LEN + 2U, FCGI_MAX_CONNS.data(),
    14);
  // Access the descriptor which must be used for the connected socket which
  // is managed by the server.
  ASSERT_EQ(socket_functions::SocketWrite(local_connection + 1,
    response_buffer, buffer_length), buffer_length);
  // Allow the client to process the response.
  std::unique_ptr<ServerEvent> response_uptr {};
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  GetValuesResult* gvr_ptr {dynamic_cast<GetValuesResult*>(
    response_uptr.get())};
  ASSERT_NE(gvr_ptr, nullptr);
  observer.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, true,
    local_connection, name_only_map, ParamsMap {}, __LINE__));
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
