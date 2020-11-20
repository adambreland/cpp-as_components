#include "test/include/test_fcgi_client_interface.h"

#include <arpa/inet.h>
#include <fcntl.h>
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
#include <iterator>
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

struct ClientInterfaceObserverValues
{
  struct ClientInterfaceConnectionOnlyObserverValues co;
  std::size_t total_completed_request_count;
  std::size_t total_pending_request_count;
};

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
    // Ensure that the the kTimeout flag is cleared.
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
    // Clear the kTimeout flag to reset shared state.
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

constexpr const struct InterfaceCreationArguments kDefaultInterfaceArguments
{
  /* domain          = */ AF_UNSPEC,
  /* backlog         = */ 5,
  /* max_connections = */ 10,
  /* max_requests    = */ 100,
  /* app_status      = */ EXIT_FAILURE,
  /* unix_path       = */ nullptr,
};

// Create maps for testing which match the default server interface arguments.
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
  {"/tmp/TestFcgiClientInterfaceManagementRequests1"};
constexpr const char*const kUnixPath2
  {"/tmp/TestFcgiClientInterfaceManagementRequests2"};

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

constexpr const struct itimerval kTimeout
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
  ASSERT_NE(setitimer(ITIMER_REAL, &kTimeout, nullptr), -1) <<
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
  /* role           = */ FCGI_RESPONDER,
  /* keep_conn      = */ true,
  /* params_map_ptr = */ &kSharedExerciseParams,
  /* stdin_begin    = */ kStdinDataForClientExercise.data(),
  /* stdin_end      = */ kStdinDataForClientExercise.data() +
                         kStdinDataForClientExercise.size(),
  /* data_begin     = */ kFcgiDataForClientExercise.data(),
  /* data_end       = */ kFcgiDataForClientExercise.data() +
                         kFcgiDataForClientExercise.size()
};

constexpr FcgiType kUnknownTypeForExercise {static_cast<FcgiType>(64)};
const std::vector<std::uint8_t> kDataForUnknownBinaryRequest {2U, 4U};

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
      observer_ptr->connection, kExerciseDataRef));
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

} // namespace

// CloseConnection
// Examined properties:
// 1) Presence of pending management requests in the management request queue.
// 2) Presence of incomplete (pending) application requests.
// 3) Presence of completed applicaton requests.
// 4) Whether or not the connection is connected.
// 5) For a connection which is not connected, the presence or absence of
//    completed but unreleased application requests.
// 6) For the server of a connection, whether or not more than one connection
//    has been made to the server.
// 7) Whether or not closure occurs when the client interface has registered
//    that the connection is ready to be read and it has not been read.
//
//
//

// CompletedRequestCount
// Discussion:
//    Both overloads of CompletedRequestCount are used throughout testing code.
// In particular, the connection overload is used in
// GTestFatalClientInterfaceConnectionOnlyObserverCheck, and the overload which
// takes no parameters is used in GTestFatalClientInterfaceObserverCheck.
// Only special cases which may not be covered in other tests are treated here.
//
// Properties examined here:
// 1) Correct behavior of the connection overload when it is called with a
//    value for connection which has never been connected.
//
// 

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
//    tested on a sigle interface with simultaneous connections to each domain.
// 4) The ability to connect to a server when a previous connection to the
//    server was made and has since been closed.
// 5) The ability of the client to detect connection closure by a server for
//    each domain.
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
  ASSERT_NO_THROW(resource_list_.push_back({-1, kUnixPath1}));
  constexpr int kDomainCount {3};
  constexpr int kInternetDomainCount {kDomainCount - 1};
  constexpr int kDomainArray[kDomainCount] = {AF_INET, AF_INET6, AF_UNIX};
  int pipes[kDomainCount][2] = {}; // AF_UNIX doesn't need a pipe.
  for(int i {0}; i != kDomainCount; ++i)
  {
    ASSERT_NE(pipe(pipes[i]), -1) << std::strerror(errno);
  }
  pid_t child_id_array[kDomainCount] = {};
  int server_index {0};
  for(/*no-op*/; server_index != kDomainCount; ++server_index)
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
  if(server_index < kDomainCount) // A child.
  {
    try
    {
      ChildServerAlrmRestoreAndSelfKillSet();
      struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
      inter_args.domain          = kDomainArray[server_index];
      inter_args.unix_path       = kUnixPath1; // Ignored for internet servers.
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
      if(server_index < kDomainCount)
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
      for(int i {0}; i != kDomainCount; ++i)
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
          if(iter->get_environment_map() != kSharedExerciseParams)
          {
            _exit(EXIT_FAILURE);
          }
          const std::vector<std::uint8_t>& stdin_ref {iter->get_STDIN()};
          const std::vector<std::uint8_t>& data_ref  {iter->get_DATA()};
          int write_count {0};
          // Convert the returned boolean write status to an integer.
          // Check that all writes were successful.
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
  in_port_t ports[kDomainCount] = {};
  for(int i {0}; i < kDomainCount; ++i)
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
  // Define parameters for connecting to the servers and for tracking
  // connection and request state as requests are sent to the servers.
  struct ConnectionParameters
  {
    const char* address_path;
    in_port_t   port;
  };
  const struct ConnectionParameters kConnectionParameters[kDomainCount]
  {
    {"127.0.0.1", ports[0]},
    {"::1", ports[1]},
    {kUnixPath1, 0U}
  };
  struct ConnectionTracker
  {
    int                                                domain;
    bool                                               received_get_values;
    bool                                               received_unknown;
    struct ClientInterfaceConnectionOnlyObserverValues observer;
  };
  std::map<int, ConnectionTracker> connection_map                     {};
  std::size_t                      total_completed_request_count      {0U};
  std::size_t                      total_pending_request_count        {0U};
  int                              connection_count                   {2};
  std::size_t                      connection_completed_request_count {0U};
  TestFcgiClientInterface          client_inter                       {};
  // Connect to all of the servers twice.
  auto GTestFatalConnector =
  [
    &client_inter,
    &connection_completed_request_count,
    &connection_count,
    &connection_map,
    &kConnectionParameters,
    &kDomainArray
  ]
  (
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace conn_tracer {"", invocation_line,
      "lambda GTestFatalConnector"};
    for(int i {0}; i != kDomainCount; ++i)
    {
      int connection_buffer {};
      for(int j {0}; j != connection_count; ++j)
      {
        constexpr const char* prefix {"connection iteration index: "};
        std::string error_message {prefix};
        error_message.append(std::to_string((connection_count*i) + j));
        ::testing::ScopedTrace iteration_tracer {"", __LINE__, error_message};
        ASSERT_NO_THROW(connection_buffer = client_inter.Connect(
          kConnectionParameters[i].address_path,
          kConnectionParameters[i].port)) << std::strerror(errno);
        ASSERT_NE(connection_buffer, -1)  << std::strerror(errno);
        // Assert that the returned descriptor is non-blocking.
        int flags {fcntl(connection_buffer, F_GETFL)};
        ASSERT_NE(flags, -1) << std::strerror(errno);
        ASSERT_TRUE(flags & O_NONBLOCK);
        ASSERT_TRUE(connection_map.insert
        (
          {
            connection_buffer,
            {
              /* domain              = */ kDomainArray[i],
              /* received_get_values = */ false,
              /* received_unknown    = */ false,
              /* observer            = */
              {
                /* connection                         = */ connection_buffer,
                /* connection_completed_request_count = */
                      connection_completed_request_count,
                /* connection_count                   = */
                      connection_count * kDomainCount,
                /* is_connected                       = */ true,
                /* management_request_count           = */ 0U,
                /* connection_pending_request_count   = */ 0U,
                /* ready_event_count                  = */ 0U,
              }
            }
          }
        ).second);
      }
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalConnector(__LINE__));
  constexpr int first_application_request_count {3};
  std::set<FcgiRequestIdentifier> application_request_set {};
  // Send requests on each of the six connections.
  for(std::map<int, ConnectionTracker>::iterator
    connection_iter {connection_map.begin()};
    connection_iter != connection_map.end();
    ++connection_iter)
  {
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendExerciseRequests(
      &client_inter,
       kExerciseDataRef,
       first_application_request_count,
      &connection_iter->second.observer,
      &total_completed_request_count,
      &total_pending_request_count,
      &application_request_set,
       __LINE__)
    ) << "Connection descriptor: " << connection_iter->first << '\n'
      << "Domain: " << connection_iter->second.domain;
  }

  // The following lambdas are used in the processing lambda below.
  auto RemainingManagementRequests =
  [
    &connection_map
  ]
  ()->bool
  {
    bool management_requests_remain {false};
    for(std::map<int, ConnectionTracker>::iterator management_iter
      {connection_map.begin()}; management_iter != connection_map.end();
      ++management_iter)
    {
      if(!(management_iter->second.received_get_values) ||
         !(management_iter->second.received_unknown))
      {
        management_requests_remain = true;
        break;
      }
    }
    return management_requests_remain;
  };

  auto ResponseReceiptCompletionCheck =
  [
    &RemainingManagementRequests,
    &application_request_set
  ]
  ()->bool
  {
    return !(RemainingManagementRequests() || application_request_set.size());
  };

  auto ConnectionClosureCompletionCheck =
  [
    &connection_map
  ]
  ()->bool
  {
    bool connected_connections_remain {false};
    for(std::map<int, ConnectionTracker>::iterator connection_iter
      {connection_map.begin()};
      connection_iter != connection_map.end();
      ++connection_iter)
    {
      if(connection_iter->second.observer.is_connected)
      {
        connected_connections_remain = true;
        break;
      }
    }
    return !connected_connections_remain;
  };

  auto ConnectionRequestIteratorCheck =
  [
    &application_request_set
  ]
  (
    int descriptor_value,
    std::set<FcgiRequestIdentifier>::iterator connection_request_iter
  )->bool
  {
    return (connection_request_iter != application_request_set.end())  &&
           (connection_request_iter->descriptor() == descriptor_value);
  };

  auto ApplicationRequestCheck =
  [
    &application_request_set,
    &ConnectionRequestIteratorCheck
  ]
  (
    int descriptor_value,
    int expected_application_count,
    int invocation_line
  )->bool
  {
    ::testing::ScopedTrace tracer {"", invocation_line,
      "lambda ApplicationRequestCheck"};
    int application_request_count {0};
    std::set<FcgiRequestIdentifier>::iterator connection_request_iter
      {application_request_set.lower_bound({descriptor_value,
        FCGI_NULL_REQUEST_ID})};
    while(ConnectionRequestIteratorCheck(descriptor_value,
      connection_request_iter))
    {
      ++application_request_count;
      ++connection_request_iter;
    }
    bool as_expected {application_request_count == expected_application_count};
    EXPECT_TRUE(as_expected);
    return as_expected;
  };

  auto ApplicationRequestCountMessage =
  [
    &application_request_set,
    &RemainingManagementRequests,
    &client_inter
  ]
  ()->std::string
  {
    std::string message {"Remaining application requests: "};
    message.append(std::to_string(application_request_set.size()));
    message.append(1, '\n').append("Do management requests remain? ");
    message.append((RemainingManagementRequests()) ? "true" : "false");
    message.append(1, '\n').append("Reported count of pending application "
      "requests: ").append(std::to_string(client_inter.PendingRequestCount()));
    message.append(1, '\n').append("Reported count of completed application "
      "requests: ").append(std::to_string(client_inter.CompletedRequestCount()));
    return message;
  };
  // Receive responses. Validate responses as they are received. Response
  // receipt is not ordered between servers. For a given server, the following
  // sequence must occur:
  // 1) FCGI_GET_VALUES_RESULT
  // 2) FCGI_UNKNOWN_TYPE
  // 3) Responses to application requests. The order of application request
  //    responses is not specified.
  // --
  // 4) In the case that connection closure by the server is expected, a
  //    ConnectionClosure event should occur last.
  //
  //    Response processing is handled by the below lambda. It is used in two
  // cases. First, it is used when two connections were made to each of the
  // three servers. In this case, connection closure by the server is not
  // expected. Second, it is used after the initial request-response cycle.
  // All connections are closed, and one connection to each server is made.
  // In this case, the keep_conn flag is not set for requests. As such, it is
  // expected that each connection will be closed by the corresponding server.
  //    Response processing occurs in a loop on event receipt. This loop may
  // hang in a call to RetrieveServerEvent if the TestFcgiClientInterface
  // instance fails to return all of the expected ServerEvent instances by the
  // time that the TestFcgiClientInterface instance stops receiving data from
  // one or more servers. In this case, with the assumption that the servers
  // are correct, the TestFcgiClientInterface instance must have made an error
  // either during request transmission or during response processing.
  auto GTestFatalProcessServerEvents =
  [
    &ApplicationRequestCheck,
    &ApplicationRequestCountMessage,
    &application_request_set,
    &client_inter,
    &ConnectionClosureCompletionCheck,
    &connection_map,
    &ResponseReceiptCompletionCheck
  ]
  (
    bool                                   expect_closure_by_server,
    const struct FcgiRequestDataReference& sent_request_ref,
    int                                    application_request_count
  )
  {
    int count_of_connections {static_cast<int>(connection_map.size())};
    while(true)
    {
      std::string error_message {ApplicationRequestCountMessage()};
      ::testing::ScopedTrace response_tracer {"", __LINE__,
        error_message.data()};
      std::unique_ptr<ServerEvent> event_uptr {};
      ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
      ServerEvent* event_ptr {event_uptr.get()};
      // Start an if-else-if ladder on types derived from ServerEvent.
      // For each event type, take an appropriate action given the
      // status of connection tracking state and request tracking state.
      // Break when all expected responses have been received.

                           /// FcgiResponse ///
      if(FcgiResponse* app_response_ptr {dynamic_cast<FcgiResponse*>(
        event_ptr)})
      {
        FcgiRequestIdentifier id {app_response_ptr->RequestId()};
        std::map<int, ConnectionTracker>::iterator conn_iter
          {connection_map.find(id.descriptor())};
        ASSERT_NE(conn_iter, connection_map.end());
         /// Check for conformance to the expected order of event receipt. ///
        EXPECT_TRUE(conn_iter->second.received_get_values);
        EXPECT_TRUE(conn_iter->second.received_unknown);
        if(expect_closure_by_server)
        {
          EXPECT_TRUE(conn_iter->second.observer.is_connected);
        }
        std::set<FcgiRequestIdentifier>::iterator app_request_iter
          {application_request_set.find(id)};
        ASSERT_NE(app_request_iter, application_request_set.end());
        application_request_set.erase(app_request_iter);
                    /// Verify the data of the response. ///
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
        auto StreamDataComparison =
        []
        (
          const std::uint8_t*              reference_stream_begin,
          const std::uint8_t*              reference_stream_end,
          const std::vector<std::uint8_t>& response_stream
        )->void
        {
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
        StreamDataComparison(sent_request_ref.data_begin,
          sent_request_ref.data_end, app_response_ptr->FcgiStderr());
        StreamDataComparison(sent_request_ref.stdin_begin,
          sent_request_ref.stdin_end, app_response_ptr->FcgiStdout());
              /// Check if all expected events have been received. ///
        if(ResponseReceiptCompletionCheck())
        {
          if(expect_closure_by_server && !ConnectionClosureCompletionCheck())
          {
            continue;
          }
          break;
        }
      }
                             /// GetValuesResult ///
      else if(GetValuesResult* get_values_ptr {dynamic_cast<GetValuesResult*>(
        event_ptr)})
      {
        int descriptor_value {get_values_ptr->RequestId().descriptor()};
        std::map<int, ConnectionTracker>::iterator conn_iter
          {connection_map.find(descriptor_value)};
        ASSERT_NE(conn_iter, connection_map.end());
        /// Check for conformance to the expected order of event receipt. ///
        ASSERT_FALSE(conn_iter->second.received_get_values);
        if(expect_closure_by_server)
        {
          EXPECT_TRUE(conn_iter->second.observer.is_connected);
        }
        conn_iter->second.received_get_values = true;
        EXPECT_FALSE(conn_iter->second.received_unknown);
        ApplicationRequestCheck(descriptor_value, application_request_count,
          __LINE__);
                     /// Verify the data of the response. ///
        ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(
          get_values_ptr,
          false,
          descriptor_value,
          kNameOnlyMap,
          kMapWithValues,
          __LINE__));
              /// Check if all expected events have been received. ///
        if(ResponseReceiptCompletionCheck())
        {
          if(expect_closure_by_server && !ConnectionClosureCompletionCheck())
          {
            continue;
          }
          break;
        }
      }
                             /// UnknownType ///
      else if(UnknownType* unknown_ptr {dynamic_cast<UnknownType*>(
        event_ptr)})
      {
        int descriptor_value {unknown_ptr->RequestId().descriptor()};
        std::map<int, ConnectionTracker>::iterator conn_iter
          {connection_map.find(descriptor_value)};
        ASSERT_NE(conn_iter, connection_map.end());
        /// Check for conformance to the expected order of event receipt. ///
        EXPECT_TRUE(conn_iter->second.received_get_values);
        ASSERT_FALSE(conn_iter->second.received_unknown);
        conn_iter->second.received_unknown = true;
        if(expect_closure_by_server)
        {
          EXPECT_TRUE(conn_iter->second.observer.is_connected);
        }
        ApplicationRequestCheck(descriptor_value, application_request_count,
          __LINE__);
                      /// Verify the data of the response. ///
        EXPECT_EQ(kUnknownTypeForExercise, unknown_ptr->Type());
        const struct ManagementRequestData& returned_data
          {unknown_ptr->Request()};
        EXPECT_EQ(kUnknownTypeForExercise,       returned_data.type);
        EXPECT_EQ(ParamsMap {},                    returned_data.params_map);
        EXPECT_EQ(kDataForUnknownBinaryRequest, returned_data.data);
               /// Check if all expected events have been received. ///
        if(ResponseReceiptCompletionCheck())
        {
          if(expect_closure_by_server && !ConnectionClosureCompletionCheck())
          {
            continue;
          }
          break;
        }
      }
                             /// ConnectionClosure ///
      else if(ConnectionClosure* closure_ptr {dynamic_cast<ConnectionClosure*>(
        event_ptr)})
      {
        constexpr const char* error_message
          {"A server unexpectedly closed a connection."};
        FcgiRequestIdentifier id {closure_ptr->RequestId()};
        int descriptor_value {id.descriptor()};
        std::map<int, ConnectionTracker>::iterator conn_iter
          {connection_map.find(descriptor_value)};
        if(expect_closure_by_server)
        {
          ASSERT_NE(conn_iter, connection_map.end());
          /// Check for conformance to the expected order of event receipt. ///
          bool connection_management_requests_remain
            {!(conn_iter->second.received_get_values) ||
             !(conn_iter->second.received_unknown)};
          if(connection_management_requests_remain ||
             !ApplicationRequestCheck(descriptor_value, 0, __LINE__))
          {
             FAIL() << error_message;
          }
          conn_iter->second.observer.is_connected = false;
                       /// Verify the data of the response. ///
          EXPECT_EQ(FCGI_NULL_REQUEST_ID, id.Fcgi_id());
          count_of_connections -= 1;
          EXPECT_EQ(count_of_connections, client_inter.ConnectionCount());
          bool is_connected {false};
          ASSERT_NO_THROW(is_connected =
            client_inter.IsConnected(descriptor_value));
          EXPECT_FALSE(is_connected);
               /// Check if all expected events have been received. ///
          if(ConnectionClosureCompletionCheck())
            break;
        }
        else // Abort if a connection is closed when closure is not expected.
        {
          EXPECT_NE(conn_iter, connection_map.end());
          FAIL() << error_message;
        }
      }
                             /// InvalidRecord ///
      else if(InvalidRecord* invalid_record_ptr {dynamic_cast<InvalidRecord*>(
        event_ptr)})
      {
        FcgiRequestIdentifier id {invalid_record_ptr->RequestId()};
        std::map<int, ConnectionTracker>::iterator conn_iter
          {connection_map.find(id.descriptor())};
        EXPECT_NE(conn_iter, connection_map.end());
        FAIL() << "An invalid record was received when none was expected.";
      }
                        /// Error - catch all others ///
      else
      {
        FAIL() << "An error occurred in a call to RetrieveServerEvent. The "
          "call either returned a null std::unique_ptr<ServerEvent> instance "
          "or returned an instance that pointed to an unknown derived type.";
      }
    } // end while(true) loop on types of events derived from ServerEvent.
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalProcessServerEvents(false,
    kExerciseDataRef, first_application_request_count));
  // Once all of the responses have been received, the expected observable
  // state values of the client interface are known.
  total_pending_request_count   = 0U;
  total_completed_request_count = kDomainCount * connection_count *
    first_application_request_count;
  EXPECT_EQ(client_inter.PendingRequestCount(), total_pending_request_count);
  EXPECT_EQ(client_inter.CompletedRequestCount(),
    total_completed_request_count);
  int closed_count {0};
  for(std::map<int, ConnectionTracker>::iterator obs_iter
    {connection_map.begin()}; obs_iter != connection_map.end(); ++obs_iter)
  {
    obs_iter->second.observer.connection_completed_request_count +=
      first_application_request_count;
    obs_iter->second.observer.connection_count                   -=
      closed_count;
    obs_iter->second.observer.connection_pending_request_count   -=
      first_application_request_count;
    obs_iter->second.observer.management_request_count            = 0U;
    obs_iter->second.observer.ready_event_count                   = 0U;
    ASSERT_NO_FATAL_FAILURE(
      GTestFatalClientInterfaceConnectionOnlyObserverCheck(
        client_inter,
        obs_iter->second.observer,
        __LINE__
      )
    );
    bool closed {false};
    int descriptor_value {obs_iter->first};
    ASSERT_NO_THROW(closed = client_inter.CloseConnection(descriptor_value));
    ASSERT_TRUE(closed);
    ++closed_count;
    EXPECT_FALSE(client_inter.IsConnected(descriptor_value));
    ASSERT_NO_THROW(client_inter.ReleaseId(descriptor_value));
  }
  //    Connect to the servers again and exercise the client interface as
  // before with the exception that each connection should be closed by its
  // server. Because of connection closure by servers, only a single request
  // may be sent to each server.
  //    Variable, descriptive connection state which is passed as arguments
  // to helper functions is updated.
  ASSERT_EQ(application_request_set.size(), 0U);
  connection_map.clear();
  total_completed_request_count = 0U;
  connection_completed_request_count = 0U;
  connection_count = 1;
  constexpr int second_application_request_count {1};
  ASSERT_NO_FATAL_FAILURE(GTestFatalConnector(__LINE__));
  struct FcgiRequestDataReference close_exercise_data {kExerciseDataRef};
  close_exercise_data.keep_conn = false;
  for(std::map<int, ConnectionTracker>::iterator
      connection_iter {connection_map.begin()};
      connection_iter != connection_map.end();
      ++connection_iter)
  {
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendExerciseRequests(
      &client_inter,
       close_exercise_data,
       second_application_request_count,
      &connection_iter->second.observer,
      &total_completed_request_count,
      &total_pending_request_count,
      &application_request_set,
       __LINE__)
    ) << "Connection descriptor: " << connection_iter->first << '\n'
      << "Domain: " << connection_iter->second.domain;
  }
  ASSERT_NO_FATAL_FAILURE(GTestFatalProcessServerEvents(true,
    close_exercise_data, second_application_request_count));
  // Verify state for totals.
  total_pending_request_count    = 0U;
  total_completed_request_count += kDomainCount * connection_count *
    second_application_request_count;
  EXPECT_EQ(client_inter.PendingRequestCount(), total_pending_request_count);
  EXPECT_EQ(client_inter.CompletedRequestCount(),
    total_completed_request_count);
  // Release requests and verify observable state as this is done.
  for(std::map<int, ConnectionTracker>::iterator obs_iter
    {connection_map.begin()}; obs_iter != connection_map.end(); ++obs_iter)
  {
    obs_iter->second.observer.connection_completed_request_count +=
      second_application_request_count;
    std::size_t conn_completed_request_count
      {obs_iter->second.observer.connection_completed_request_count};
    obs_iter->second.observer.connection_count                    = 0;
    obs_iter->second.observer.connection_pending_request_count   -=
      second_application_request_count;
    obs_iter->second.observer.management_request_count            = 0U;
    obs_iter->second.observer.ready_event_count                   = 0U;
    ASSERT_NO_FATAL_FAILURE(
      GTestFatalClientInterfaceConnectionOnlyObserverCheck(
        client_inter,
        obs_iter->second.observer,
        __LINE__
      )
    );
    int descriptor_value {obs_iter->first};
    ASSERT_NO_THROW(client_inter.ReleaseId(descriptor_value));
    total_completed_request_count -= conn_completed_request_count;
    EXPECT_EQ(total_completed_request_count,
      client_inter.CompletedRequestCount());
    std::size_t completed_req_return {};
    ASSERT_NO_THROW(completed_req_return =
      client_inter.CompletedRequestCount(descriptor_value));
    EXPECT_EQ(completed_req_return, 0U);
  }
  EXPECT_EQ(client_inter.CompletedRequestCount(), 0U);
}

// Testing of:
// ConnectionCount
// IsConnected
// ManagementRequestCount
// std::size_t PendingRequestCount()
// std::size_t PendingRequestCount(int)
// ReadyEventCount
//
// Discussion:
//    The above functions are used throughout testing and in
// GTestFatalClientInterfaceConnectionOnlyObserverCheck and
// GTestFatalClientInterfaceObserverCheck. The only special cases which have
// been determined for these functions are when IsConnected and
// PendingRequestCount are called with a connection which has never been
// connected.
//
//


// ReleaseId
// ReleaseId(FcgiRequestIdentifier)
//    This overload is used throughout testing. The following test cases were
// determined to potentially not be exercised in other testing code.
//
// Test cases for ReleaseId(FcgiRequestIdentifier):
// 1) ReleaseId(i) is called when i has not been used by the interface. There
//    are two subcases: i.descriptor() is not connected vs. it is connected.
// 2) ReleaseId(i) is called when i refers to a request which is not complete.
//
// ReleaseId(int)
// Examined properties:
// 1) Whether the connection is connected or not.
// 2) Whether completed but unreleased application requests are associated with
//    the connection.
// 3) The presence of other connection state, such as pending management
//    requests, that should not be affected by a call to ReleaseId.
// 4) The presence of multiple connections to the same server when both
//    connections have state which should be affected by an appropriate call to
//    ReleaseId.
//
// Test cases for ReleaseId(int):
// 1) A call to ReleaseId(c) is made when c refers to a connection value which
//    has never been used by the interface.
// 2) A call to ReleaseId(c) is made when c refers to a connected connection
//    which does not have completed but unreleased application requests.
// 3) As in 2, but completed anb unreleased application requests are present.
// 4) As in 3, but another connection to the server of the connection to be
//    released is present. This other connection also has completed but
//    unreleased application requests. The connection to be released has a
//    larger socket descriptor than the other connection.
// 5) A call to ReleaseId(c) is made when c refers to a connection which is
//    no longer connected and which has completed but unreleased application
//    requests.

// RetrieveServerEvent
// Examined properties:
// 1) Proper behavior regarding the specified throw of a std::logic_error
//    exception.
//    a) As a special case, correct behavior is verified when ConnectionCount()
//       returns zero but at least one connection is disconnected and
//       associated with completed but unreleased application requests.
// 2) Correct behavior when a partial response is received on a connection and
//    other connections are present which are ready to be read. It is specified
//    that other ready connections will be read until either the ready event
//    queue is nonempty or all ready connections have been read until they
//    would block.
// 3) Tests based on types derived from ServerEvent:
//    ConnectionClosure
//    a) A connection is made, and the server immediately closes the
//       connection.
//    b) Other test cases? ?????????????????????????????????????????????
// 

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
//     closure by the client interface user in the case that completed and
//     unreleased requests are present, an FCGI_GET_VALUES request is made when
//     such an application request is present. Then the connection is closed by
//     the user. A new connection with the same descriptor value is made. Then
//     a management request is made which should have a response which is
//     distinct from the response that would have been returned for the
//     previous request. It is verified that the correct response is returned.
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
  ASSERT_NO_THROW(resource_list_.push_back({-1, kUnixPath2}));
  pid_t fork_return {fork()};
  if(fork_return == 0) // child
  {
    ChildServerAlrmRestoreAndSelfKillSet();

    struct InterfaceCreationArguments second_inter_args {kDefaultInterfaceArguments};
    second_inter_args.domain          = AF_UNIX;
    second_inter_args.unix_path       = kUnixPath2;

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
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;

  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(inter_return),
    kUnixPath1}));

  // Create a client interface and check its initial observable state.
  TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.CompletedRequestCount(), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(),       0);
  EXPECT_EQ(client_inter.ReadyEventCount(),       0U);

  // TEST CASE 1
  // Test the copy overload first.
  int local_socket {};
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(kUnixPath1, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  struct ClientInterfaceObserverValues observer_values {};
  observer_values.co.connection                         = local_socket;
  observer_values.co.connection_completed_request_count = 0U;
  observer_values.co.connection_count                   = 1;
  observer_values.co.is_connected                       = true;
  observer_values.co.management_request_count           = 0U;
  observer_values.co.connection_pending_request_count   = 0U;
  observer_values.co.ready_event_count                  = 0U;
  observer_values.total_completed_request_count         = 0U;
  observer_values.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, kMapWithValues));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  std::unique_ptr<ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.co.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  GetValuesResult* gvr_ptr
    {dynamic_cast<GetValuesResult*>(result_uptr.get())};
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__);

  // TEST CASE 2
  // Start testing the move overload.
  ParamsMap value_map_copy {kMapWithValues};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, std::move(value_map_copy)));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.co.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__);

  // TEST CASE 3
  // Send two requests.
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, kMapWithValues));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.co.management_request_count = 2U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
  observer_values.co.management_request_count = 0U;
  observer_values.co.ready_event_count        = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kMpxsNameMap, kMpxsMapWithValue, __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.co.ready_event_count        = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__);

  // TEST CASE 4
  int second_local_socket {};
  ASSERT_NO_THROW(second_local_socket = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(second_local_socket, -1) << std::strerror(errno);
  observer_values.co.connection_count         = 2;
  struct ClientInterfaceObserverValues second_observer {};
  second_observer.co.connection                         = second_local_socket;
  second_observer.co.connection_completed_request_count = 0U;
  second_observer.co.connection_count                   = 2;
  second_observer.co.is_connected                       = true;
  second_observer.co.management_request_count           = 0U;
  second_observer.co.connection_pending_request_count   = 0U;
  second_observer.co.ready_event_count                  = 0U;
  second_observer.total_completed_request_count         = 0U;
  second_observer.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, second_observer, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues));
  ASSERT_TRUE(send_gvr);
  observer_values.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    second_local_socket, kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  second_observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, second_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr, std::unique_ptr<ServerEvent> {});
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  int first_retrieved {gvr_ptr->RequestId().descriptor()};
  bool first_is_first_local {first_retrieved == local_socket};
  auto CyclicCheck =
  [&]
  (
    bool first,
    int connection,
    struct ClientInterfaceObserverValues* observer_ptr,
    int line
  )->void
  {
    if(first)
    {
      observer_ptr->co.management_request_count = 0U;
      ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
        client_inter, *observer_ptr, line));
      GTestFatalCheckGetValuesResult(gvr_ptr, false,
        connection, kNameOnlyMap, kMapWithValues, line);
    }
    else
    {
      observer_ptr->co.management_request_count = 0U;
      ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
        client_inter, *observer_ptr, line));
      GTestFatalCheckGetValuesResult(gvr_ptr, false,
        connection, kMpxsNameMap, kMpxsMapWithValue, line);
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
    client_inter.SendGetValuesRequest(second_local_socket, kMapWithValues));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  second_observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, second_observer, __LINE__));
  bool connection_closure {};
  ASSERT_NO_THROW(connection_closure =
    client_inter.CloseConnection(second_local_socket));
  ASSERT_TRUE(connection_closure);
  observer_values.co.connection_count         = 1;
  second_observer.co.connection_count         = 1;
  second_observer.co.is_connected             = false;
  second_observer.co.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, second_observer, __LINE__));
  
  // TEST CASE 6  
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(kUnixPath2, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  observer_values.co.connection_count         = 2;
  struct ClientInterfaceObserverValues new_observer {};
  new_observer.co.connection                         = new_connection;
  new_observer.co.connection_completed_request_count = 0U;
  new_observer.co.connection_count                   = 2;
  new_observer.co.is_connected                       = true;
  new_observer.co.management_request_count           = 0U;
  new_observer.co.connection_pending_request_count   = 0U;
  new_observer.co.ready_event_count                  = 0U;
  new_observer.total_completed_request_count         = 0U;
  new_observer.total_pending_request_count           = 0U;
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues));
  ASSERT_TRUE(send_gvr);
  observer_values.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  new_observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
  new_observer.co.connection_count         = 1;
  new_observer.co.is_connected             = false;
  new_observer.co.management_request_count = 0U;
  observer_values.co.connection_count      = 1;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
    kNameOnlyMap));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  // Test the move overload.
  ParamsMap name_only_copy {kNameOnlyMap};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    std::move(name_only_copy)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  // Test the move overload.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));

  // TEST CASE 9 (Failure case)
  //    Check for a return of false when a call is made and it is detected that
  // the peer closed the connection.
  //    Destroy the FcgiServerInterface instance to cause the peer connection
  // to be closed.
  //    Before destroying the interface, make a request to allow a check that
  // the count is cleared upon the detection of destruction to be performed.
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, kMapWithValues));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  observer_values.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    kNameOnlyMap));
  EXPECT_FALSE(send_gvr);
  observer_values.co.connection_count         = 0;
  observer_values.co.is_connected             = false;
  observer_values.co.management_request_count = 0U;
  observer_values.co.ready_event_count        = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ConnectionClosure* closure_ptr
    {dynamic_cast<ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  observer_values.co.ready_event_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer_values, __LINE__));
}

// SendGetValuesRequest: Test case set 2
TEST_F(TestFcgiClientInterfaceTestFixture, SendGetValuesRequestTestCaseSet2)
{
  // TEST CASE 10
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  // Create the server interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(inter_return),
    kUnixPath1}));
  // Create a client interface and check its initial observable state.
  TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.CompletedRequestCount(), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(),       0);
  EXPECT_EQ(client_inter.ReadyEventCount(),       0U);
  // Connect to the server.
  int local_connection {};
  ASSERT_NO_THROW(local_connection = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(local_connection, -1) << std::strerror(errno);
  struct ClientInterfaceObserverValues observer {};
  observer.co.connection                         = local_connection;
  observer.co.connection_completed_request_count = 0U;
  observer.co.connection_count                   = 1;
  observer.co.is_connected                       = true;
  observer.co.management_request_count           = 0U;
  observer.co.connection_pending_request_count   = 0U;
  observer.co.ready_event_count                  = 0U;
  observer.total_completed_request_count         = 0U;
  observer.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
  observer.co.connection_pending_request_count  = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
  observer.total_completed_request_count         = 1U;
  observer.co.connection_completed_request_count = 1U;
  observer.total_pending_request_count           = 0U;
  observer.co.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Send an FCGI_GET_VALUES request which will not be answered.
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    local_connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Send an application request which will not be answered.
  FcgiRequestIdentifier second_req_id {};
  ASSERT_NO_THROW(second_req_id = client_inter.SendRequest(local_connection,
    request_data_ref));
  ASSERT_NE(second_req_id, FcgiRequestIdentifier {});
  EXPECT_EQ(second_req_id.descriptor(), local_connection);
  EXPECT_NE(second_req_id.Fcgi_id(), app_req_id.Fcgi_id());
  observer.total_pending_request_count        = 1U;
  observer.co.connection_pending_request_count   = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Close the connection with a completed, unreleased request.
  bool close_return {false};
  ASSERT_NO_THROW(close_return =
    client_inter.CloseConnection(local_connection));
  ASSERT_TRUE(close_return);
  observer.co.connection_count                   = 0U;
  observer.co.is_connected                       = false;
  observer.co.management_request_count           = 0U;
  observer.total_pending_request_count           = 0U;
  observer.co.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Establish a new connection to the server. The same descriptor value should
  // be reused.
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  ASSERT_EQ(new_connection, local_connection);
  struct ClientInterfaceObserverValues new_observer {};
  new_observer.co.connection                         = new_connection;
  new_observer.co.connection_completed_request_count = 1U;
  new_observer.co.connection_count                   = 1;
  new_observer.co.is_connected                       = true;
  new_observer.co.management_request_count           = 0U;
  new_observer.co.connection_pending_request_count   = 0U;
  new_observer.co.ready_event_count                  = 0U;
  new_observer.total_completed_request_count         = 1U;
  new_observer.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  // Send a management request which is distinct from the last one.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  new_observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
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
  new_observer.co.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    new_connection, kMpxsNameMap, kMpxsMapWithValue, __LINE__);

  // TEST CASE 11
  // Send another application request and FCGI_GET_VALUES request which will
  // not be answered.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    new_connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  new_observer.co.management_request_count = 1U;
  FcgiRequestIdentifier third_req_id {};
  ASSERT_NO_THROW(third_req_id = client_inter.SendRequest(new_connection,
    request_data_ref));
  ASSERT_NE(third_req_id, FcgiRequestIdentifier {});
  EXPECT_EQ(third_req_id.descriptor(), new_connection);
  EXPECT_NE(third_req_id.Fcgi_id(), app_req_id.Fcgi_id());
  new_observer.total_pending_request_count        = 1U;
  new_observer.co.connection_pending_request_count   = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  // Destroy the server.
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Allow the client to process server destruction.
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  ConnectionClosure* connection_closure_ptr {dynamic_cast<ConnectionClosure*>(
    response_uptr.get())};
  ASSERT_NE(connection_closure_ptr, nullptr);
  new_observer.co.connection_count                   = 0U;
  new_observer.co.is_connected                       = false;
  new_observer.co.management_request_count           = 0U;
  new_observer.total_pending_request_count           = 0U;
  new_observer.co.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  EXPECT_EQ(connection_closure_ptr->RequestId().descriptor(), new_connection);
  // Create a new server interface.
  // Close the listening socket descriptor of the first server.
  close(resource_list_[0].first);
  resource_list_[0].first = -1;
  inter_args.unix_path = kUnixPath2;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  new_inter_return {};
  ASSERT_NO_THROW(new_inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& new_inter_uptr
    {std::get<0>(new_inter_return)};
  ASSERT_NE(new_inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(new_inter_return),
    kUnixPath2}));
  // Connect to the server.
  int third_connection {};
  ASSERT_NO_THROW(third_connection = client_inter.Connect(kUnixPath2, 0U));
  ASSERT_NE(third_connection, -1) << std::strerror(errno);
  ASSERT_EQ(third_connection, local_connection);
  struct ClientInterfaceObserverValues third_observer {};
  third_observer.co.connection                         = third_connection;
  third_observer.co.connection_completed_request_count = 1U;
  third_observer.co.connection_count                   = 1U;
  third_observer.co.is_connected                       = true;
  third_observer.co.management_request_count           = 0U;
  third_observer.co.connection_pending_request_count   = 0U;
  third_observer.co.ready_event_count                  = 0U;
  third_observer.total_completed_request_count         = 1U;
  third_observer.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, third_observer, __LINE__));
  // Send a new management request and allow the interfaces to act.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(third_connection,
    kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  third_observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, third_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
    new_inter_uptr.get(), __LINE__));
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  gvr_ptr = dynamic_cast<GetValuesResult*>(response_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  third_observer.co.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, third_observer, __LINE__));
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    new_connection, kMpxsNameMap, kMpxsMapWithValue, __LINE__);
  // Release the completed request.
  bool release_return {false};
  ASSERT_NO_THROW(release_return = client_inter.ReleaseId(app_req_id));
  EXPECT_TRUE(release_return);
  third_observer.total_completed_request_count           = 0U;
  third_observer.co.connection_completed_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, third_observer, __LINE__));
}

// SendGetValuesRequest: Test case set 3
TEST_F(TestFcgiClientInterfaceTestFixture, SendGetValuesRequestTestCaseSet3)
{
  // TEST CASE 12
  // The connected descriptor of the interface is used to allow an erroneous
  // response to be sent to the client interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  // Create the server interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(resource_list_.push_back({std::get<1>(inter_return),
    kUnixPath1}));
  TestFcgiClientInterface client_inter {};
  int local_connection {};
  ASSERT_NO_THROW(local_connection = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(local_connection, -1) << std::strerror(errno);
  struct ClientInterfaceObserverValues observer {};
  observer.co.connection                         = local_connection;
  observer.co.connection_completed_request_count = 0U;
  observer.co.connection_count                   = 1;
  observer.co.is_connected                       = true;
  observer.co.management_request_count           = 0U;
  observer.co.connection_pending_request_count   = 0U;
  observer.co.ready_event_count                  = 0U;
  observer.total_completed_request_count         = 0U;
  observer.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Allow the server to process the connection before sending a management
  // request.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    local_connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Construct and write an erroneous response.
  // A header, 2 bytes for name and value lenths, and 14 bytes for the
  // name FCGI_MAX_CONNS. This gives 24 bytes. No padding is required.
  constexpr std::size_t buffer_length {static_cast<std::size_t>(
    FCGI_HEADER_LEN + 2 + 14)};
  std::uint8_t response_buffer[buffer_length] = {};
  PopulateHeader(response_buffer, FcgiType::kFCGI_GET_VALUES_RESULT,
    FCGI_NULL_REQUEST_ID, 16U, 0U);
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
  observer.co.management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  GTestFatalCheckGetValuesResult(gvr_ptr, true,
    local_connection, kNameOnlyMap, ParamsMap {}, __LINE__);
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
