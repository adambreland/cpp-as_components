#include "test/include/test_fcgi_client_interface.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"
#include "external/socket_functions/include/socket_functions.h"
#include "external/a_component_testing/gtest/include/a_component_testing_gtest_utilities.h"

#include "server_interface_combined.h"
#include "test/include/fcgi_si_testing_utilities.h"
#include "test/test/include/client_interface_testing_utilities.h"

namespace a_component {
namespace fcgi {
namespace test {
namespace test {

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
// 8) Connection closure when a partial record has been received for a
//    response. The connection should then be reused, and it should be
//    verified that the normal request-response cycle can be performed.
//
// Test cases:
// CloseConnectionCaseSet1
// 1) A call to CloseConnection is made for a connection which was never
//    connected.
// 2) A connection is made. An application request is made and the server
//    is allowed to respond to it. The response is retrieved with a call to
//    RetrieveServerEvent. Another application request is made. An
//    FCGI_GET_VALUES request is made. A call to CloseConnection is made before
//    the server is allowed to respond to the requests. When the connection
//    has been closed, another call to CloseConnection is made.
// CloseConnectionCaseSet2
// 3) Two connections are made to a server. A call to CloseConnection is made
//    for one of the connections. An application request is then made to the
//    other connection. The server is allowed to process the request. The
//    response is retrieved. The connection is closed with a call to
//    CloseConnection.
// CloseConnectionCaseSet3
// 4) Two connections are made to a server. A management request is made to
//    both connections. The server is allowed to process the requests. A call
//    to RetrieveServerEvent is made. The connection for which
//    RetrieveServerEvent did not return a response is closed.
// 5) A connection is made. Two application requests are made. The server
//    completely responds to one of the requests and then sends a partial
//    record for the other request. A call to RetrieveServerEvents is made.
//    After the complete response is retrieved, a call to CloseConnection is
//    made. A new connection is made to the same server. An application request
//    and response cycle is performed.
//
// Modules which testing depends on:
// 1) FcgiServerInterface
// 2) The immediate success or failure of connection to a listening socket by
//    the implementation of local (AF_UNIX) sockets.
//
// Other modules whose testing depends on this module: none.

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet1)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};
  ClientInterfaceObserverValues observer
  {
    /* co = */
    {
      /* connection                         = */ 1000,
      /* connection_completed_request_count = */ 0U,
      /* is_connected                       = */ false,
      /* management_request_count           = */ 0U,
      /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 0,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };

  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));

  // TEST CASE 1

  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.CloseConnection(1000)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));

  // TEST CASE 2
  int local_connection {};
  ASSERT_NO_THROW(local_connection = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(local_connection, -1) << std::strerror(errno);
  observer.co.connection    = local_connection;
  observer.co.is_connected  = true;
  observer.in.connection_count = 1;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  FcgiRequestIdentifier request_id_1 {};
  ASSERT_NO_THROW(request_id_1 =
    client_inter.SendRequest(local_connection, kExerciseDataRef));
  ++(observer.co.connection_pending_request_count);
  ++(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  // Allow the server to process the connection and the request.
  ASSERT_NO_THROW
  (
    std::vector<FcgiRequest> accept_buffer {};
    while(!(accept_buffer.size()))
    {
      accept_buffer = inter_uptr->AcceptRequests();
    }
    ASSERT_EQ(accept_buffer.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
      kSharedExerciseParams, FCGI_RESPONDER, true, __LINE__));
  );
  // Process the response.
  std::unique_ptr<ServerEvent> response_uptr {};
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(response_uptr.get())};
  ASSERT_NE(response_ptr, nullptr);
  observer.co.connection_completed_request_count = 1U;
  observer.co.connection_pending_request_count   = 0U;
  observer.in.total_completed_request_count         = 1U;
  observer.in.total_pending_request_count           = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
  FcgiRequestIdentifier request_id_2 {};
  ASSERT_NO_THROW(request_id_2 =
    client_inter.SendRequest(local_connection, kExerciseDataRef));
  observer.co.connection_pending_request_count = 1U;
  observer.in.total_pending_request_count         = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
    local_connection, kMapWithValues)));
  observer.co.management_request_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.CloseConnection(local_connection)));
  observer.co.is_connected                     = false;
  observer.co.management_request_count         = 0U;
  observer.co.connection_pending_request_count = 0U;
  observer.in.connection_count                 = 0;
  observer.in.total_pending_request_count      = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.CloseConnection(local_connection)));
}

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet2)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};

  // TEST CASE 3
  constexpr int connection_number {2};
  int local_connection_array[connection_number] = {};
  struct ClientInterfaceConnectionOnlyObserverValues
  observer_array[connection_number] = {};
  struct ClientInterfaceInstanceObserverValues instance_observer
  {
    /* total_completed_request_count = */ 0U,
    /* connection_count              = */ 0,
    /* total_pending_request_count   = */ 0U,
    /* ready_event_count             = */ 0U
  };
  for(int i {0}; i != connection_number; ++i)
  {
    ASSERT_NO_THROW(local_connection_array[i] = client_inter.Connect(
      kUnixPath1, 0U)) << "Connection iteration: " << i;
    observer_array[i].connection                         =
      local_connection_array[i];
    observer_array[i].connection_completed_request_count = 0U;
    observer_array[i].is_connected                       = true;
    observer_array[i].management_request_count           = 0U;
    observer_array[i].connection_pending_request_count   = 0U;
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_array[i], __LINE__)) << "Connection iteration: "
      << i;
    ++(instance_observer.connection_count);
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    EXPECT_NE(local_connection_array[i], -1);
  }
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(
    local_connection_array[0])));
  observer_array[0].is_connected = false;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_array[0], __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  FcgiRequestIdentifier new_request {};
  ASSERT_NO_THROW(new_request =
    client_inter.SendRequest(local_connection_array[1], kExerciseDataRef));
  ++(observer_array[1].connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_array[0], __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Allow the server to process the request.
  ASSERT_NO_THROW
  (
    std::vector<FcgiRequest> accept_buffer {};
    while(!(accept_buffer.size()))
    {
      accept_buffer = inter_uptr->AcceptRequests();
    }
    ASSERT_EQ(accept_buffer.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
      kSharedExerciseParams, FCGI_RESPONDER, true, __LINE__));
  );
  std::unique_ptr<ServerEvent> response_uptr {};
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(response_uptr.get())};
  ASSERT_NE(response_ptr, nullptr);
  ++(observer_array[1].connection_completed_request_count);
  --(observer_array[1].connection_pending_request_count);
  ++(instance_observer.total_completed_request_count);
  --(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_array[1], __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
  ASSERT_NO_THROW(EXPECT_TRUE(
    client_inter.CloseConnection(local_connection_array[1])));
  observer_array[1].is_connected = false;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_array[1], __LINE__));
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
}

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet3)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};

  // TEST CASE 4
  constexpr int connection_number {2};
  int local_connection_array[connection_number] = {};
  struct ClientInterfaceConnectionOnlyObserverValues
  observer_array[connection_number] = {};
  struct ClientInterfaceInstanceObserverValues instance_observer
  {
    /* total_completed_request_count = */ 0U,
    /* connection_count              = */ 0,
    /* total_pending_request_count   = */ 0U,
    /* ready_event_count             = */ 0U
  };
  for(int i {0}; i != connection_number; ++i)
  {
    ASSERT_NO_THROW(local_connection_array[i] = client_inter.Connect(
      kUnixPath1, 0U)) << "Connection iteration: " << i;
    observer_array[i].connection                         =
      local_connection_array[i];
    observer_array[i].connection_completed_request_count = 0U;
    observer_array[i].is_connected                       = true;
    observer_array[i].management_request_count           = 0U;
    observer_array[i].connection_pending_request_count   = 0U;
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_array[i], __LINE__)) << "Connection iteration: "
      << i;
    ++(instance_observer.connection_count);
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    EXPECT_NE(local_connection_array[i], -1);
  }
  for(int i {0}; i != connection_number; ++i)
  {
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
      local_connection_array[i], kMapWithValues)));
    ++(observer_array[i].management_request_count);
    ASSERT_NO_FATAL_FAILURE(
      GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
        observer_array[i], __LINE__));
  }
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
    inter_uptr.get(), __LINE__));
  std::unique_ptr<ServerEvent> response_uptr {};
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  GetValuesResult* gvr_ptr {dynamic_cast<GetValuesResult*>(
    response_uptr.get())};
  ASSERT_NE(gvr_ptr, nullptr);
  // The order of management response production for the two connections is
  // not specified.
  int management_response_connection {gvr_ptr->RequestId().descriptor()};
  bool is_first_connection {management_response_connection ==
    local_connection_array[0]};
  int management_response_connection_index {int(!is_first_connection)};
  int other_connection_index {int(is_first_connection)};
  int unretrieved_response_connection
    {local_connection_array[other_connection_index]};
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    management_response_connection, kNameOnlyMap, kMapWithValues, __LINE__));
  --(observer_array[management_response_connection_index].management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_array[management_response_connection_index],
    __LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(
    unretrieved_response_connection)));
  observer_array[other_connection_index].is_connected             = false;
  observer_array[other_connection_index].management_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_array[other_connection_index], __LINE__));
  --(instance_observer.connection_count);
  EXPECT_EQ(instance_observer.connection_count, client_inter.ConnectionCount());
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_array[management_response_connection_index],
    __LINE__));
}

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet4)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};

  // TEST CASE 5
  int local_connection {};
  ASSERT_NO_THROW(ASSERT_NE(local_connection = client_inter.Connect(
    kUnixPath1, 0U), -1) << std::strerror(errno));
  struct ClientInterfaceConnectionOnlyObserverValues observer
  {
    /* connection                         = */ local_connection,
    /* connection_completed_request_count = */ 0U,
    /* is_connected                       = */ true,
    /* management_request_count           = */ 0U,
    /* connection_pending_request_count   = */ 0U,
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  struct ClientInterfaceInstanceObserverValues instance_observer
  {
    /* total_completed_request_count = */ 0U,
    /* connection_count              = */ 1,
    /* total_pending_request_count   = */ 0U,
    /* ready_event_count             = */ 0U
  };
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  FcgiRequestIdentifier first_id {};
  ASSERT_NO_THROW(ASSERT_TRUE(first_id = client_inter.SendRequest(
    local_connection, kExerciseDataRef)));
  ++(observer.connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  FcgiRequestIdentifier second_id {};
  ASSERT_NO_THROW(ASSERT_TRUE(second_id = client_inter.SendRequest(
    local_connection, kExerciseDataRef)));
  ++(observer.connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);

  auto OperationForPartialResponse = [local_connection]
  (
    std::vector<FcgiRequest>* request_buffer_ptr
  )->void
  {
    std::vector<FcgiRequest>::size_type request_count
      {request_buffer_ptr->size()};
    if(request_count > 0U)
    {
      constexpr const unsigned int number_of_requests {2U};
      if(request_count != number_of_requests)
      {
        std::string error_message {"Unexpected buffer size: "};
        error_message.append(std::to_string(request_count));
        throw std::logic_error {error_message};
      }
      for(int i {0}; i != number_of_requests; ++i)
      {
        EXPECT_EQ((*request_buffer_ptr)[i].get_environment_map(),
          kSharedExerciseParams);
        EXPECT_EQ((*request_buffer_ptr)[i].get_role(), FCGI_RESPONDER);
        EXPECT_EQ((*request_buffer_ptr)[i].get_keep_conn(), true);
        EXPECT_EQ((*request_buffer_ptr)[i].get_STDIN(),
          kStdinDataForClientExercise);
        EXPECT_EQ((*request_buffer_ptr)[i].get_DATA(),
          kFcgiDataForClientExercise);
      }
      if(!((*request_buffer_ptr)[0].Complete(EXIT_SUCCESS)))
      {
        throw std::logic_error {"Could not complete."};
      }
      std::uint8_t response_buffer[2 * FCGI_HEADER_LEN] = {};
      PopulateHeader(response_buffer, FcgiType::kFCGI_STDOUT,
        (*request_buffer_ptr)[1].get_request_identifier().Fcgi_id(),
        FCGI_HEADER_LEN, 0U);
      // Write the partial response to the client.
      // It is assumed that local_connection + 1 must be the socket descriptor
      // of the server interface to the client.
      constexpr std::int_fast32_t partial_record_length {FCGI_HEADER_LEN + 4};
      if(socket_functions::SocketWrite(local_connection + 1, response_buffer,
        partial_record_length) != partial_record_length)
      {
        throw std::logic_error {"Error writing to FCGI_STDOUT."};
      }
    }
  };

  std::function<void(std::vector<FcgiRequest>*)> op_arg
    {OperationForPartialResponse};
  ASSERT_NO_FATAL_FAILURE(GTestFatalServerAcceptLoop(
    inter_uptr.get(), op_arg, __LINE__));
  std::unique_ptr<ServerEvent> response_uptr {};
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ++(observer.connection_completed_request_count);
  --(observer.connection_pending_request_count);
  ++(instance_observer.total_completed_request_count);
  --(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  // It is known that ReadyEventCount should return zero here as the second
  // application request has not been received in-full.
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NE(response_uptr.get(), nullptr);
  FcgiResponse* fcgi_response_ptr
    {dynamic_cast<FcgiResponse*>(response_uptr.get())};
  ASSERT_NE(fcgi_response_ptr, nullptr);
  EXPECT_EQ(fcgi_response_ptr->AppStatus(), EXIT_SUCCESS);
  EXPECT_EQ(fcgi_response_ptr->FcgiStderr().size(), 0U);
  EXPECT_EQ(fcgi_response_ptr->FcgiStdout().size(), 0U);
  EXPECT_EQ(fcgi_response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
  EXPECT_EQ(fcgi_response_ptr->Request(), kExerciseDataRef);
  EXPECT_EQ(fcgi_response_ptr->RequestId(), first_id);
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(local_connection)));
  observer.connection_pending_request_count = 0U;
  observer.is_connected                     = false;
  --(instance_observer.connection_count);
  --(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // This call allows the FcgiServerInterface instance to process the closure
  // of local connection. This tests that the TestFcgiClientInterface correctly
  // closes the connection when a partial record was received.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  int new_connection {};
  ASSERT_NO_THROW(ASSERT_NE(new_connection =
    client_inter.Connect(kUnixPath1, 1), -1) << std::strerror(errno));
  // The order of connection closure in the server and client should allow
  // this assertion to be true. Nonetheless, this assertion relies on logic
  // pertaining to file descriptor usage which is not strictly specified.
  // Re-use of the same descriptor by TestFcgiClientInterface increases the
  // value of the test as this connection will have a completed but unreleased
  // request.
  ASSERT_EQ(local_connection, new_connection);
  observer.connection   = new_connection;
  observer.is_connected = true;
  ++(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  FcgiRequestIdentifier new_id {};
  ASSERT_NO_THROW(ASSERT_TRUE(new_id = client_inter.SendRequest(new_connection,
    kExerciseDataRef)));
  ++(observer.connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW
  (
    std::vector<FcgiRequest> accept_buffer {};
    while(!(accept_buffer.size()))
    {
      accept_buffer = inter_uptr->AcceptRequests();
    }
    ASSERT_EQ(accept_buffer.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
      kSharedExerciseParams, FCGI_RESPONDER, true, __LINE__));
  );
  ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(response_uptr =
    client_inter.RetrieveServerEvent()));
  ASSERT_NE(response_uptr.get(), nullptr);
  ASSERT_NE(fcgi_response_ptr =
    dynamic_cast<FcgiResponse*>(response_uptr.get()), nullptr);
  ++(observer.connection_completed_request_count);
  --(observer.connection_pending_request_count);
  ++(instance_observer.total_completed_request_count);
  --(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, fcgi_response_ptr,
    __LINE__);
  std::size_t completed_request_count {client_inter.CompletedRequestCount(
    new_connection)};
  ASSERT_NO_THROW(client_inter.ReleaseId(new_connection));
  observer.connection_completed_request_count = 0U;
  instance_observer.total_completed_request_count -= completed_request_count;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(new_connection)));
  observer.is_connected = false;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
}

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
// 2)    For each of AF_INET and AF_INET6, a server is created in a separate
//    process. Library interpositioning is used to allow a call to connect
//    which is made by Connect return -1 with errno == EINTR. This simulates
//    a signal being delivered to the client process. When called again, the
//    interposing connect function calls connect and returns the result.
//       This test is currently implemented with:
//    1) //test/test:connect_interpositioning_test
//    2) //test/test:connect_interpositioning
//    3) //test/test:shared_interpositioning_state
//    4) test_fcgi_client_interface_connect_interpositioning_test.sh
//
//    The test is executed outside of the bazel test environment by executing
//    the test shell script. The script is intended to be executed with the
//    current working directory equal to the fcgi workspace directory.
// 3) Failure as described above for each of the three domains is checked.
//    For AF_UNIX, failure is checked when the connection path is present and
//    absent in the file system.

TEST_F(TestFcgiClientInterfaceTestFixture, ConnectCase1)
{
  ASSERT_NO_THROW(resource_list_.push_back({-1, kUnixPath1}));
  constexpr int kDomainCount {3};
  constexpr int kDomainArray[kDomainCount] = {AF_INET, AF_INET6, AF_UNIX};
  int pipes[kDomainCount][2] = {};
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
      int saved_errno {errno};
      // In parent and an error occurred.
      // Terminate any children which were created.
      for(int i {0}; i != server_index; ++i)
      {
        // Don't abort on fatal failures until termination of each child has
        // been attempted.
        EXPECT_NO_FATAL_FAILURE(GTestFatalTerminateChild(child_id_array[i],
          __LINE__)) << "Iteration count: " << i;
      }
      // Return from the current test.
      FAIL() << std::strerror(saved_errno);
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
      //    All servers should write the ephemeral port back to the parent
      // process. The port is in network byte order. It can be byte-serialized
      // directly.
      //    Note that this write synchronizes the server processes and the
      // client process.
      //    Close all of the read pipes.
      for(int i {0}; i != kDomainCount; ++i)
      {
        close(pipes[i][0]);
      }
      std::uint8_t* port_ptr {static_cast<std::uint8_t*>(static_cast<void*>(
        &std::get<2>(inter_return)))};
      if(socket_functions::SocketWrite(pipes[server_index][1], port_ptr,
        sizeof(in_port_t)) < sizeof(in_port_t))
      {
        _exit(EXIT_FAILURE);
      }
      // Close all of the write pipes.
      for(int i {0}; i != kDomainCount; ++i)
      {
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
          if(iter->get_role() != FCGI_RESPONDER)
          {
            _exit(EXIT_FAILURE);
          }
          // Both true and false values for keep_conn are expected.
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
  // Note that this process is a synchronization mechanism between the
  // server processes and the client process.
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
                /* is_connected                       = */ true,
                /* management_request_count           = */ 0U,
                /* connection_pending_request_count   = */ 0U
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
    int                                    application_request_count,
    int                                    invocation_line
  )->void
  {
    int count_of_connections {static_cast<int>(connection_map.size())};
    while(true)
    {
      std::string error_message {ApplicationRequestCountMessage()};
      ::testing::ScopedTrace response_tracer {"", invocation_line,
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
        GTestNonFatalEchoResponseCompare(sent_request_ref,
          app_response_ptr, __LINE__);
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
    kExerciseDataRef, first_application_request_count, __LINE__));
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
    obs_iter->second.observer.connection_pending_request_count   -=
      first_application_request_count;
    obs_iter->second.observer.management_request_count            = 0U;
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
    close_exercise_data, second_application_request_count, __LINE__));
  // Verify state for totals.
  total_pending_request_count    = 0U;
  total_completed_request_count += kDomainCount * connection_count *
    second_application_request_count;
  EXPECT_EQ(client_inter.PendingRequestCount(), total_pending_request_count);
  EXPECT_EQ(client_inter.CompletedRequestCount(),
    total_completed_request_count);
  // Terminate the child processes.
  for(int i {0}; i < kDomainCount; ++i)
  {
    EXPECT_NO_FATAL_FAILURE(GTestFatalTerminateChild(child_id_array[i],
      __LINE__)) << "Iteration count i = " << i;
  }
  ASSERT_FALSE(HasFatalFailure()) << "Fatal assertion failure during "
    "child termination.";
  // Release requests and verify observable state as this is done.
  for(std::map<int, ConnectionTracker>::iterator obs_iter
    {connection_map.begin()}; obs_iter != connection_map.end(); ++obs_iter)
  {
    obs_iter->second.observer.connection_completed_request_count +=
      second_application_request_count;
    std::size_t conn_completed_request_count
      {obs_iter->second.observer.connection_completed_request_count};
    obs_iter->second.observer.connection_pending_request_count   -=
      second_application_request_count;
    obs_iter->second.observer.management_request_count            = 0U;
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

// See the documentation for the test cases of Connect for the discussion of
// ConnectCase2.

TEST(Connect, ConnectCase3)
{
  TestFcgiClientInterface client_inter {};
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.Connect("127.0.0.1", 11000U), -1));
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.Connect("::1", 11000U), -1));
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.Connect(kUnixPath1, 0U), -1));
  int unix_socket {socket(AF_UNIX, SOCK_STREAM, 0)};
  ASSERT_NE(unix_socket, -1) << std::strerror(errno);
  struct sockaddr_un unix_address {};
  unix_address.sun_family = AF_UNIX;
  std::strcpy(unix_address.sun_path, kUnixPath1);
  if((bind(unix_socket, static_cast<struct sockaddr*>(static_cast<void*>(
    &unix_address)), sizeof(struct sockaddr_un))) == -1)
  {
    close(unix_socket);
    FAIL() << "A call to bind failed.\n" << std::strerror(errno);
  }
  close(unix_socket);
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.Connect(kUnixPath1, 0U), -1));
  ASSERT_NE(unlink(kUnixPath1), -1) << std::strerror(errno);
}

// Testing of:
// std::size_t CompletedRequestCount()
// std::size_t CompletedRequestCount(int)
// ConnectionCount
// IsConnected
// ManagementRequestCount
// std::size_t PendingRequestCount()
// std::size_t PendingRequestCount(int)
// ReadyEventCount
//
// Discussion:
//    The above functions are used throughout testing and, in particular, in
// GTestFatalClientInterfaceConnectionOnlyObserverCheck and
// GTestFatalClientInterfaceObserverCheck. The only special cases which have
// been determined for these functions are when CompletedRequestCount,
// IsConnected, and PendingRequestCount are called with a connection which has
// never been connected.
//
// Modules which testing depends on: none.
// Other modules whose testing depends on this module: none.

TEST(AggregatedObservers, UknownConnection)
{
  TestFcgiClientInterface client_inter {};
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.CompletedRequestCount(1000), 0U));
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.IsConnected(1000), false));
  EXPECT_NO_THROW(EXPECT_EQ(client_inter.PendingRequestCount(1000), 0U));
}

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
// 4) The presence of multiple connections to the same server when each
//    connection has state which should be affected by an appropriate call to
//    ReleaseId.
//
// Test cases for ReleaseId(int):
// 1) A call to ReleaseId(c) is made when c refers to a connection value which
//    has never been used by the interface.
// 2) A call to ReleaseId(c) is made when c refers to a connected connection
//    which does not have completed but unreleased application requests.
// 3) As in 2, but completed and unreleased application requests are present.
// 4) As in 3, but another connection to the server of the connection to be
//    released is present. This other connection also has completed but
//    unreleased application requests. The connection to be released has a
//    larger socket descriptor than the other connection.
// 5) A call to ReleaseId(c) is made when c refers to a connection which is
//    no longer connected and which has completed but unreleased application
//    requests.

TEST_F(TestFcgiClientInterfaceTestFixture, ReleaseId)
{
  // All test cases are implemented in this test.

  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.ReleaseId(FcgiRequestIdentifier
    {1000, 1U})));
  int first_connection {};
  ASSERT_NO_THROW(ASSERT_NE(first_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  struct ClientInterfaceConnectionOnlyObserverValues observer_1
  {
    /* connection                         = */ first_connection,
    /* connection_completed_request_count = */ 0U,
    /* is_connected                       = */ true,
    /* management_request_count           = */ 0U,
    /* connection_pending_request_count   = */ 0U
  };
  struct ClientInterfaceInstanceObserverValues instance_observer
  {
    /* total_completed_request_count = */ 0U,
    /* connection_count              = */ 1,
    /* total_pending_request_count   = */ 0U,
    /* ready_event_count             = */ 0U
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.ReleaseId(FcgiRequestIdentifier
    {first_connection, 1U})));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.ReleaseId(first_connection)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);

  auto GTestFatalProcessSingleRequest = [&inter_uptr]
  (
    std::vector<FcgiRequest>* accept_buffer_ptr,
    int invocation_line)->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalProcessSingleRequest"};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer_ptr->size()))
      {
        (*accept_buffer_ptr) = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer_ptr->size(), 1U);
      ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(
        accept_buffer_ptr, kSharedExerciseParams, FCGI_RESPONDER, true,
        __LINE__));
      accept_buffer_ptr->clear();
    );
  };

  auto GTestFatalSendAndProcessRequest = [&]
  (
    struct ClientInterfaceConnectionOnlyObserverValues* observer_ptr,
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "GTestFatalSendAndProcessRequest"};

    FcgiRequestIdentifier request_id {};
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW(ASSERT_TRUE(request_id = client_inter.SendRequest(
      observer_ptr->connection, kExerciseDataRef)));
    ++(observer_ptr->connection_pending_request_count);
    ++(instance_observer.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, *observer_ptr, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    // Allow the server to process the request.
    ASSERT_NO_FATAL_FAILURE(GTestFatalProcessSingleRequest(&accept_buffer,
      __LINE__));
    // Retrieve the response.
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    --(observer_ptr->connection_pending_request_count);
    ++(observer_ptr->connection_completed_request_count);
    --(instance_observer.total_pending_request_count);
    ++(instance_observer.total_completed_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, *observer_ptr, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
      __LINE__);
    EXPECT_EQ(response_ptr->RequestId(), request_id);
  };

  ASSERT_NO_FATAL_FAILURE(GTestFatalSendAndProcessRequest(&observer_1,
    __LINE__));
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.ReleaseId(first_connection)));
  observer_1.connection_completed_request_count = 0U;
  instance_observer.total_completed_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalSendAndProcessRequest(&observer_1,
    __LINE__));
  int second_connection {};
  ASSERT_NO_THROW(ASSERT_NE(second_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  EXPECT_LT(first_connection, second_connection);
  struct ClientInterfaceConnectionOnlyObserverValues observer_2
  {
    /* connection                         = */ second_connection,
    /* connection_completed_request_count = */ 0U,
    /* is_connected                       = */ true,
    /* management_request_count           = */ 0U,
    /* connection_pending_request_count   = */ 0U
  };
  ++(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_2, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalSendAndProcessRequest(&observer_2,
    __LINE__));
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.ReleaseId(second_connection)));
  observer_2.connection_completed_request_count = 0U;
  --(instance_observer.total_completed_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_2, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.CloseConnection(first_connection)));
  observer_1.is_connected = false;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_2, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.ReleaseId(first_connection)));
  observer_1.connection_completed_request_count = 0U;
  --(instance_observer.total_completed_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_1, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_2, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
}

// RetrieveServerEvent
// Examined properties:
// 1) Proper behavior regarding the specified throw of a std::logic_error
//    exception.
//    a) As a special case, correct behavior is verified when ConnectionCount()
//       returns zero but at least one connection is disconnected and
//       associated with completed but unreleased application requests.
// 2)    The ability to successfully generate an instance of the appropriate
//    type derived from ServerEvent when receipt of the data of a response has
//    been interrupted by receipt of data for other responses. In other words,
//    the ability to handle response generation when the data of a response is
//    provided over more than one cycle of I/O multiplexing over open
//    connections.
//       Note that correct behavior for the above case implies correct behavior
//    relative to the specification of how data is read in the case that
//    reading from a connection would block and other ready connections are
//    present.
// 3) Tests based on types derived from ServerEvent:
//    ConnectionClosure:
//       ConnectionClosure instances are constructed in calls to
//    RetrieveServerEvent throughout testing. It was determined that the
//    following properties may not be examined in other testing code.
//    a) A connection is made, and the server immediately closes the
//       connection.
//    b) A partial response to a request has been received, and the server
//       closes the connection before the response is received in-full. At
//       least, a case for management requests and a case for application
//       requests should be present.
//
//    FcgiResponse:
//       Use of RetrieveServerEvent and concomitant generation of FcgiResponse
//    instances occurs throughout testing. The following discussion addresses
//    properties which were determined to potentially not be covered in testing
//    code which is not specific to the testing of RetrieveServerEvent.
//       FcgiResponse is the only event which contains information which will
//    have been received from a server over multiple FastCGI records. This is
//    because at least an empty FCGI_STDOUT record and an FCGI_END_REQUEST
//    record must be received for a response. All other responses are responses
//    to management requests and use one FastCGI record. Given this property
//    for FcgiResponse, the order of record receipt is a relevant property for
//    testing. Also, given this property, record type interleaving is a
//    relevant property for testing. Finally, all responses to FastCGI
//    application requests share the property that the transmission of a
//    terminal record for FCGI_STDERR is optional if no data was transmitted
//    over this stream. All of these properties should be addressed when
//    testing the generation of FcgiResponse instances from data received from
//    a server upon the invocation of RetrieveServerEvent.
//
//    GetValuesResult:
//       Generation of GetValuesResult instances is tested in the testing of
//    SendGetValuuesRequest.
//    
//    InvalidRecord:
//       Generation of an InvalidRecord instance for each of the conditions
//    that should cause the generation of such a record should be tested. These
//    conditions are listed in the documentation of InvalidRecord.
//
//    UnknownType
//       Generation of UnknownType instances is tested in the testing of
//    SendBinaryManagementRequest.
// 4) Connection closure by the 

// SendAbortRequest
// Examined properties:
// 1) Four properties, each with distinct possible values, are present which
//    can be considered in combination:
//    a) Whether the connection of the FcgiRequestIdentifier argument is
//       connected or not.
//    b) Whether requests other than the potential request given by the
//       FcgiRequestIdentifier argument are pending on the connection.
//    c) As in b, but for completed requests instead of pending requests.
//    d) Which of the three possible states applies to the
//       FcgiRequestIdentifier argument: pending, completed, or released.
//    When disallowed combinations are removed, 16 states remain. Not all of
//    these states are tested.
// 2) Detection of connection closure during the call. This involves a
//    transition of the interface from a state where it registered the
//    connection as connected to a state where it registered the connection
//    as disconnected. Specified behavior in this case should be verified.
//
// Test cases:
// 1) A call is made with an FcgiRequestIdentifier argument that corresponds to
//    a connection that never existed. A return of false is verified.
// 2) A connection is made.
//    a) A call is made to SendAbortRequest. A return of false is verified.
//    b) Two application requests are made. Before the server can process the
//       requests, a call to SendAbortRequest is made for the second request.
//       Another call to SendAbortRequest is made for this request and a return
//       of true is verified. Transmission of a correct FCGI_ABORT_REQUEST
//       record is verified. Responses are sent by the server. The responses
//       are received by the client interface and exposed by a call to
//       RetrieveServerEvent. This causes the requests to transition from
//       pending to completed.
//    c) A call to SendAbortRequest is made for one of the requests that was
//       just completed. A return of false is verified.
//    d) An application request is made. As before, a call to SendAbortRequest
//       is made for the new request, and correct behavior is verified.
//    e) The connection is closed. A call to SendAbortRequest is made for one
//       of the completed-but-unreleased requests. A return of false is
//       verified.
//    f) The completed-but-unreleased requests are released. A call to
//       SendAbortRequest is made with an FcgiRequestIdentifier argument that
//       does not correspond to one of the previous requests.
// Note: 1 and 2 cover each of the values of each of the four properties
//       separately (though not in combination).
// 3) A connection is made. A request is made with keep_conn set to false. The
//    server is allowed to process the request. A call to SendAbortRequest is
//    made. Proper behavior regarding detection of connection closure is
//    verified.
//
// Modules which testing depends on:
// 1) FcgiServerInterface and, in particular, its behavior regarding receipt
//    of FCGI_ABORT_REQUEST records.
// 2) The immediate detection of connection closure by the implementation of
//    AF_UNIX sockets.
//
// Other modules whose testing depends on this module: none.
TEST_F(TestFcgiClientInterfaceTestFixture, SendAbortRequestTestCaseSet1)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};
  // TEST CASE 1
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendAbortRequest(
    FcgiRequestIdentifier {1000, 1U})));
  int local_connection {};
  // TEST CASE 2a
  ASSERT_NO_THROW(ASSERT_NE(local_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
    /* connection                         = */ local_connection,
    /* connection_completed_request_count = */ 0U,
    /* is_connected                       = */ true,
    /* management_request_count           = */ 0U,
    /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 1,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendAbortRequest(
    FcgiRequestIdentifier {local_connection, 1U})));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  // TEST CASE 2b
  constexpr int request_count {2};
  FcgiRequestIdentifier request_id_array[request_count] = {};
  for(int i {0}; i != request_count; ++i)
  {
    ::testing::ScopedTrace iteration_tracer {__FILE__, __LINE__,
      std::to_string(i)};
    ASSERT_NO_THROW(request_id_array[i] = client_inter.SendRequest(
      local_connection, kExerciseDataRef));
    ASSERT_TRUE(request_id_array[i]);
    ++(observer.co.connection_pending_request_count);
    ++(observer.in.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  }
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  // Allow the server to process the requests.
  std::vector<FcgiRequest> accept_buffer {};
  std::vector<FcgiRequest> request_list {};

  auto AppendRequests = [&request_list]
  (std::vector<FcgiRequest>* buffer_ptr)->void
  {
    request_list.insert(request_list.end(),
      std::move_iterator(buffer_ptr->begin()),
      std::move_iterator(buffer_ptr->end()));
    buffer_ptr->clear();
  };

  std::function<void(std::vector<FcgiRequest>*)> local_op {AppendRequests};
  ASSERT_NO_FATAL_FAILURE(GTestFatalServerAcceptLoop(inter_uptr.get(),
    local_op, __LINE__));
  ASSERT_EQ(request_list.size(), std::size_t(request_count));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendAbortRequest(
    request_id_array[request_count - 1])));
  // A duplicate abort request.
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendAbortRequest(
    request_id_array[request_count - 1])));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  int aborted_index {(request_list[0].get_request_identifier() ==
    request_id_array[0]) ? 0 : 1};
  ASSERT_NO_THROW(EXPECT_TRUE(request_list[aborted_index].AbortStatus()));
  ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&request_list,
    kSharedExerciseParams, FCGI_RESPONDER, true, __LINE__));
  request_list.clear();
  // Retrieve the responses.
  std::unique_ptr<ServerEvent> event_uptr {};
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
  ASSERT_NE(response_ptr, nullptr);
  observer.co.connection_pending_request_count = 0U;
  observer.co.connection_completed_request_count = request_count;
  observer.in.total_pending_request_count = 0U;
  observer.in.total_completed_request_count = request_count;
  observer.in.ready_event_count = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
      __LINE__);
  FcgiRequestIdentifier first_response_id {response_ptr->RequestId()};
  bool first_request_is_first {first_response_id == request_id_array[0]};
  if(!first_request_is_first)
  {
    EXPECT_EQ(first_response_id, request_id_array[request_count - 1]);
  }
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  response_ptr = dynamic_cast<FcgiResponse*>(event_uptr.get());
  ASSERT_NE(response_ptr, nullptr);
  --(observer.in.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  EXPECT_EQ(response_ptr->RequestId(), (first_request_is_first) ?
    request_id_array[request_count - 1] : request_id_array[0]);
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
  // TEST CASE 2c
  for(int i {0}; i != request_count; ++i)
  {
    ::testing::ScopedTrace iteration_tracer {__FILE__, __LINE__,
      std::to_string(i)};
    ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendAbortRequest(
      request_id_array[i])));
  }
  // TEST CASE 2d
  FcgiRequestIdentifier new_id {};
  ASSERT_NO_THROW(new_id = client_inter.SendRequest(local_connection,
    kExerciseDataRef));
  ++(observer.co.connection_pending_request_count);
  ++(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalServerAcceptLoop(inter_uptr.get(),
    local_op, __LINE__));
  ASSERT_EQ(request_list.size(), 1U);
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendAbortRequest(new_id)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(EXPECT_TRUE(request_list[0].AbortStatus()));
  ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&request_list,
    kSharedExerciseParams, FCGI_RESPONDER, true, __LINE__));
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  response_ptr = dynamic_cast<FcgiResponse*>(event_uptr.get());
  ASSERT_NE(response_ptr, nullptr);
  --(observer.co.connection_pending_request_count);
  ++(observer.co.connection_completed_request_count);
  --(observer.in.total_pending_request_count);
  ++(observer.in.total_completed_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
  // TEST CASE 2e
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.CloseConnection(local_connection)));
  observer.co.is_connected = false;
  --(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendAbortRequest(new_id)));
  // TEST CASE 2f
  ASSERT_NO_THROW(EXPECT_TRUE(client_inter.ReleaseId(local_connection)));
  observer.co.connection_completed_request_count = 0U;
  observer.in.total_completed_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendAbortRequest(new_id)));
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendAbortRequest(
    FcgiRequestIdentifier {local_connection, 1000})));
}

TEST_F(TestFcgiClientInterfaceTestFixture, SendAbortRequestTestCaseSet2)
{
  // TEST CASE 3

  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};
  int local_connection {};
  ASSERT_NO_THROW(ASSERT_NE(local_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
    /* connection                         = */ local_connection,
    /* connection_completed_request_count = */ 0U,
    /* is_connected                       = */ true,
    /* management_request_count           = */ 0U,
    /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 1,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  struct FcgiRequestDataReference request_representation {kExerciseDataRef};
  request_representation.keep_conn = false;
  FcgiRequestIdentifier request_id {};
  ASSERT_NO_THROW(request_id = client_inter.SendRequest(local_connection,
    request_representation));
  ASSERT_TRUE(request_id);
  ++(observer.co.connection_pending_request_count);
  ++(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  // Allow the server to process the request.
  std::vector<FcgiRequest> accept_buffer {};
  ASSERT_NO_THROW
  (
    while(!(accept_buffer.size()))
    {
      accept_buffer = inter_uptr->AcceptRequests();
    }
  );
  ASSERT_EQ(accept_buffer.size(), 1U);
  ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
    kSharedExerciseParams, FCGI_RESPONDER, false, __LINE__));
  // Allow the server to close the connection.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  // Check that a call to SendAbortRequest detects connection closure.
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendAbortRequest(request_id)));
  observer.co.is_connected = false;
  --(observer.co.connection_pending_request_count);
  --(observer.in.connection_count);
  ++(observer.in.ready_event_count);
  --(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  std::unique_ptr<ServerEvent> event_uptr {};
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  ConnectionClosure* closure_ptr {dynamic_cast<ConnectionClosure*>(
    event_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId(), (FcgiRequestIdentifier {local_connection,
    0U}));
  --(observer.in.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
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
//     the connection over which the call was made is closed by the application
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
  //    Synchronization between the parent and child is needed to ensure that
  // the server interface has been constructed before the client (parent) tries
  // to connect to it.
  sigset_t sigusr1_set  {};
  ASSERT_NE(sigemptyset(&sigusr1_set), -1) << std::strerror(errno);
  ASSERT_NE(sigaddset(&sigusr1_set, SIGUSR1), -1) << std::strerror(errno);
  ASSERT_NE(sigprocmask(SIG_BLOCK, &sigusr1_set, nullptr), -1) <<
    std::strerror(errno);

  ASSERT_NO_THROW(resource_list_.push_back({-1, kUnixPath2}));

  pid_t fork_return {fork()};
  if(fork_return == 0) // child
  {
    ChildServerAlrmRestoreAndSelfKillSet();

    struct InterfaceCreationArguments second_inter_args
      {kDefaultInterfaceArguments};
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
    // Signal the parent that the server interface has been constructed.
    if(kill(getppid(), SIGUSR1) == -1)
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

  // Wait until the child signals that it has constructed the interface.
  int sigwaitinfo_return {};
  while(((sigwaitinfo_return = sigwaitinfo(&sigusr1_set, nullptr)) == -1) &&
    (errno == EINTR))
    continue;
  ASSERT_NE(sigwaitinfo_return, -1) << std::strerror(errno);

  // TEST CASE 1
  // Test the copy overload first.
  int local_socket {};
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(kUnixPath1, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  struct ClientInterfaceConnectionOnlyObserverValues observer_values {};
  observer_values.connection                         = local_socket;
  observer_values.connection_completed_request_count = 0U;
  observer_values.is_connected                       = true;
  observer_values.management_request_count           = 0U;
  observer_values.connection_pending_request_count   = 0U;
  struct ClientInterfaceInstanceObserverValues instance_observer {};
  instance_observer.total_completed_request_count    = 0U;
  instance_observer.connection_count                 = 1;
  instance_observer.total_pending_request_count      = 0U;
  instance_observer.ready_event_count                = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, kMapWithValues));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  std::unique_ptr<ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  observer_values.management_request_count += 2U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  instance_observer.ready_event_count      = 1U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kMpxsNameMap, kMpxsMapWithValue, __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  instance_observer.ready_event_count        = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__);

  // TEST CASE 4
  int second_local_socket {};
  ASSERT_NO_THROW(second_local_socket = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(second_local_socket, -1) << std::strerror(errno);
  ++(instance_observer.connection_count);
  struct ClientInterfaceConnectionOnlyObserverValues second_observer {};
  second_observer.connection                         = second_local_socket;
  second_observer.connection_completed_request_count = 0U;
  second_observer.is_connected                       = true;
  second_observer.management_request_count           = 0U;
  second_observer.connection_pending_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues));
  ASSERT_TRUE(send_gvr);
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    second_local_socket, kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  ++(second_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
    struct ClientInterfaceConnectionOnlyObserverValues* observer_ptr,
    int line
  )->void
  {
    observer_ptr->management_request_count = 0U;
    ASSERT_NO_FATAL_FAILURE(
      GTestFatalClientInterfaceConnectionOnlyObserverCheck(
        client_inter, *observer_ptr, line));
    if(first)
    {
      GTestFatalCheckGetValuesResult(gvr_ptr, false,
        connection, kNameOnlyMap, kMapWithValues, line);
    }
    else
    {
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
  ++(second_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  bool connection_closure {};
  ASSERT_NO_THROW(connection_closure =
    client_inter.CloseConnection(second_local_socket));
  ASSERT_TRUE(connection_closure);
  second_observer.is_connected             = false;
  second_observer.management_request_count = 0U;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  
  // TEST CASE 6  
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(kUnixPath2, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  ++(instance_observer.connection_count);
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  struct ClientInterfaceConnectionOnlyObserverValues new_observer {};
  new_observer.connection                         = new_connection;
  new_observer.connection_completed_request_count = 0U;
  new_observer.is_connected                       = true;
  new_observer.management_request_count           = 0U;
  new_observer.connection_pending_request_count   = 0U;
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues));
  ASSERT_TRUE(send_gvr);
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  ++(new_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  // Close the connection to second server by terminating the child process.
  ASSERT_NO_THROW(client_inter.CloseConnection(new_connection));
  new_observer.is_connected             = false;
  new_observer.management_request_count = 0U;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalTerminateChild(fork_return, __LINE__));

  // TEST CASE 7 (Failure case)
  // Check for a return of false when a call is made for a non-existent
  // connection.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    kNameOnlyMap));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  // Test the move overload.
  ParamsMap name_only_copy {kNameOnlyMap};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    std::move(name_only_copy)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  // Test the move overload.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Check for constancy.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    kNameOnlyMap));
  EXPECT_FALSE(send_gvr);
  observer_values.is_connected             = false;
  observer_values.management_request_count = 0U;
  --(instance_observer.connection_count);
  ++(instance_observer.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ConnectionClosure* closure_ptr
    {dynamic_cast<ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  --(instance_observer.ready_event_count);
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
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
  struct ClientInterfaceConnectionOnlyObserverValues observer {};
  observer.connection                         = local_connection;
  observer.connection_completed_request_count = 0U;
  observer.is_connected                       = true;
  observer.management_request_count           = 0U;
  observer.connection_pending_request_count   = 0U;
  struct ClientInterfaceInstanceObserverValues instance_observer {};
  instance_observer.total_completed_request_count = 0U;
  instance_observer.connection_count              = 1;
  instance_observer.total_pending_request_count   = 0U;
  instance_observer.ready_event_count             = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
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
  ASSERT_TRUE(app_req_id);
  EXPECT_EQ(app_req_id.descriptor(), local_connection);
  ++(observer.connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Allow the server to accept the request and generate an FcgiResponse object.
  std::vector<FcgiRequest> accept_buffer {};
  while(accept_buffer.size() == 0U)
  {
    ASSERT_NO_THROW(accept_buffer = inter_uptr->AcceptRequests());
  }
  EXPECT_EQ(accept_buffer.size(), 1U);
  bool complete_return {false};
  ASSERT_NO_THROW(complete_return = accept_buffer[0].Complete(EXIT_SUCCESS));
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
  EXPECT_EQ(fcgi_response_ptr->Request(), request_data_ref);
  EXPECT_EQ(fcgi_response_ptr->RequestId(), app_req_id);
  ++(observer.connection_completed_request_count);
  --(observer.connection_pending_request_count);
  ++(instance_observer.total_completed_request_count);
  --(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Send an FCGI_GET_VALUES request which will not be answered.
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    local_connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  ++(observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  // Send an application request which will not be answered.
  FcgiRequestIdentifier second_req_id {};
  ASSERT_NO_THROW(second_req_id = client_inter.SendRequest(local_connection,
    request_data_ref));
  ASSERT_TRUE(second_req_id);
  EXPECT_EQ(second_req_id.descriptor(), local_connection);
  EXPECT_NE(second_req_id.Fcgi_id(), app_req_id.Fcgi_id());
  ++(observer.connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Close the connection with a completed, unreleased request.
  bool close_return {false};
  ASSERT_NO_THROW(close_return =
    client_inter.CloseConnection(local_connection));
  ASSERT_TRUE(close_return);
  observer.is_connected                       = false;
  observer.management_request_count           = 0U;
  observer.connection_pending_request_count   = 0U;
  --(instance_observer.connection_count);
  --(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Establish a new connection to the server. The same descriptor value should
  // be reused.
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  ASSERT_EQ(new_connection, local_connection);
  struct ClientInterfaceConnectionOnlyObserverValues new_observer {};
  new_observer.connection                         = new_connection;
  new_observer.connection_completed_request_count = 1U;
  new_observer.is_connected                       = true;
  new_observer.management_request_count           = 0U;
  new_observer.connection_pending_request_count   = 0U;
  ++(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Send a management request which is distinct from the last one.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(new_connection,
    kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  ++(new_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  --(new_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    new_connection, kMpxsNameMap, kMpxsMapWithValue, __LINE__));

  // TEST CASE 11
  // Send another application request and FCGI_GET_VALUES request which will
  // not be answered.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    new_connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  ++(new_observer.management_request_count);
  FcgiRequestIdentifier third_req_id {};
  ASSERT_NO_THROW(third_req_id = client_inter.SendRequest(new_connection,
    request_data_ref));
  ASSERT_TRUE(third_req_id);
  EXPECT_EQ(third_req_id.descriptor(), new_connection);
  EXPECT_NE(third_req_id.Fcgi_id(), app_req_id.Fcgi_id());
  ++(new_observer.connection_pending_request_count);
  ++(instance_observer.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  // Destroy the server.
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  // Allow the client to process server destruction.
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  ConnectionClosure* connection_closure_ptr {dynamic_cast<ConnectionClosure*>(
    response_uptr.get())};
  ASSERT_NE(connection_closure_ptr, nullptr);
  new_observer.is_connected                     = false;
  new_observer.management_request_count         = 0U;
  new_observer.connection_pending_request_count = 0U;
  instance_observer.total_pending_request_count = 0U;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
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
  struct ClientInterfaceConnectionOnlyObserverValues third_observer {};
  third_observer.connection                         = third_connection;
  third_observer.connection_completed_request_count = 1U;
  third_observer.is_connected                       = true;
  third_observer.management_request_count           = 0U;
  third_observer.connection_pending_request_count   = 0U;
  ++(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, third_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Send a new management request and allow the interfaces to act.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(third_connection,
    kMpxsMapWithValue));
  ASSERT_TRUE(send_gvr);
  ++(third_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, third_observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
    new_inter_uptr.get(), __LINE__));
  ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(response_uptr.get(), nullptr);
  gvr_ptr = dynamic_cast<GetValuesResult*>(response_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  --(third_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, third_observer, __LINE__));
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    new_connection, kMpxsNameMap, kMpxsMapWithValue, __LINE__);
  // Release the completed request.
  bool release_return {false};
  ASSERT_NO_THROW(release_return = client_inter.ReleaseId(app_req_id));
  EXPECT_TRUE(release_return);
  third_observer.connection_completed_request_count = 0U;
  instance_observer.total_completed_request_count   = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, third_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
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
  struct ClientInterfaceConnectionOnlyObserverValues observer {};
  observer.connection                         = local_connection;
  observer.connection_completed_request_count = 0U;
  observer.is_connected                       = true;
  observer.management_request_count           = 0U;
  observer.connection_pending_request_count   = 0U;
  struct ClientInterfaceInstanceObserverValues instance_observer {};
  instance_observer.total_completed_request_count = 0U;
  instance_observer.connection_count              = 1;
  instance_observer.total_pending_request_count   = 0U;
  instance_observer.ready_event_count             = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Allow the server to process the connection before sending a management
  // request.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(
    local_connection, kMapWithValues));
  ASSERT_TRUE(send_gvr);
  ++(observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
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
  --(observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer, __LINE__));
  GTestFatalCheckGetValuesResult(gvr_ptr, true,
    local_connection, kNameOnlyMap, ParamsMap {}, __LINE__);
}

// SendRequest
// Discussion:
//    SendRequest is used throughout testing. Only the properties that may not
// be explicitly or implicitly examined in other tests are examined here.
//
// Examined properties:
// 1) FcgiRequestDataReference values:
//    a) The two standard roles other than FCGI_RESPONDER: FCGI_AUTHORIZER and
//       FCGI_FILTER. Tests should include standard and non-standard stream
//       data. For example, FCGI_STDIN data for an FCGI_AUTHORIZER
//       request is non-standard data for the request type.
//    b) A non-standard role.
//    c) A null value for params_map_ptr.
//    d) When params_map_ptr points to an empty map.
//    e) Null values for the stream pointers.
//    f) Non-null values for the stream pointers which give empty streams.
// 2) Default-constructed identifier return case: The connection argument did
//    not refer to a connection which was currently connected and managed by
//    the interface.
//    a) A negative connection value is given.
//    b) The connection is not connected but is associated with completed and
//       unreleased requests.
//    c) The connection is not connected and is not associated with completed
//       and unreleased requests.
// 3) Default-constructed identifier return case: The connection was found to
//    be closed.
//
// Test cases:
// SendRequestCaseSet1
// 1) A request for each of FCGI_AUTHORIZER and FCGI_FILTER is made with
//    standard data.
// 2) As 1, but non-standard data is present.
// 3) A request with a non-standard role is made.
//
// SendRequestCaseSet2
// 4) Seventeen test cases which exercise SendRequest with a variety of
//    combinations of null, empty, and non-empty states for the params_map_ptr
//    and the stream pointers of struct FcgiRequestDataReference.
//
//     # | params_map_ptr | stdin range | data range
//    ----------------------------------------------
//     1   null             null          null
//     2   null             null          non-empty
//     3   null             empty         null
//     4   null             empty         empty
//     5   null             non-empty     empty
//     6   null             non-empty     non-empty
//     7   empty            null          null
//     8   empty            null          non-empty
//     9   empty            empty         empty
//    10   empty            empty         non-empty
//    11   empty            non-empty     null
//    12   empty            non-empty     non-empty
//    13   non-empty        null          null
//    14   non-empty        null          empty
//    15   non-empty        empty         empty
//    16   non-empty        empty         non-empty
//    17   non-empty        non-empty     null
//
// SendRequestCaseSet3 (Default-constructed FcgiRequestIdentifier return)
// 5) a) It is verified that false is returned by a call to SendRequest for a
//       negative connection.
//    b) It is verified that false is returned by a call to SendRequest for a
//       connection which is not connected and does not have completed but
//       unreleased requests.
//    c) A connection is made, and a request-response cycle is completed. The
//       connection is closed. It is verified that false is returned for a call
//       to SendRequest for the connection.
//    d) The requests are released for the previous connection. It is verified
//       that false is returned for a call to SendRequest for the connection.
// 6) Proper detection and handling of connection closure by the peer is
//    verified.
//
// Modules and features which testing depends on:
// 1) FcgiServerInterface
// 2) The immediate detection of connection closure by the implementation of
//    AF_UNIX sockets.
//
// Other modules whose testing depends on this module: none.

TEST_F(TestFcgiClientInterfaceTestFixture, SendRequestCaseSet1)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};

  // Define the request representations.
  struct FcgiRequestDataReference standard_authorizer
  {
    /* role           = */  FCGI_AUTHORIZER,
    /* keep_conn      = */  true,
    /* params_map_ptr = */ &kSharedExerciseParams,
    /* stdin_begin    = */  nullptr,
    /* stdin_end      = */  nullptr,
    /* data_begin     = */  nullptr,
    /* data_end       = */  nullptr
  };
  struct FcgiRequestDataReference non_standard_authorizer
  {
    /* role           = */  FCGI_AUTHORIZER,
    /* keep_conn      = */  true,
    /* params_map_ptr = */ &kSharedExerciseParams,
    /* stdin_begin    = */  kStdinDataForClientExercise.data(),
    /* stdin_end      = */  kStdinDataForClientExercise.data() +
                              kStdinDataForClientExercise.size(),
    /* data_begin     = */  nullptr,
    /* data_end       = */  nullptr
  };
  struct FcgiRequestDataReference standard_filter {kExerciseDataRef};
  standard_filter.role = FCGI_FILTER;
  struct FcgiRequestDataReference non_standard_filter
  {
    /* role           = */  FCGI_FILTER,
    /* keep_conn      = */  true,
    /* params_map_ptr = */ &kSharedExerciseParams,
    /* stdin_begin    = */  nullptr,
    /* stdin_end      = */  nullptr,
    /* data_begin     = */  nullptr,
    /* data_end       = */  nullptr
  };
  struct FcgiRequestDataReference unknown_role {kExerciseDataRef};
  unknown_role.role = 100U;

  constexpr const int known_role_request_count {4};
  struct FcgiRequestDataReference* request_array[known_role_request_count] =
    {&standard_authorizer, &non_standard_authorizer, &standard_filter,
      &non_standard_filter};

  int local_connection {};
  ASSERT_NO_THROW(ASSERT_NE(local_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
      /* connection                         = */ local_connection,
      /* connection_completed_request_count = */ 0U,
      /* is_connected                       = */ true,
      /* management_request_count           = */ 0U,
      /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 1,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));

  for(int i {0}; i != known_role_request_count; ++i)
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(local_connection,
      *request_array[i]));
    ASSERT_NE(id, FcgiRequestIdentifier {});
    ++(observer.co.connection_pending_request_count);
    ++(observer.in.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      observer, __LINE__));
    std::vector<FcgiRequest> accept_buffer {};
    while(!accept_buffer.size())
    {
      accept_buffer = inter_uptr->AcceptRequests();
    }
    ASSERT_EQ(accept_buffer.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(
      &accept_buffer, kSharedExerciseParams, (*request_array[i]).role, true,
      __LINE__));
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    --(observer.co.connection_pending_request_count);
    --(observer.in.total_pending_request_count);
    ++(observer.co.connection_completed_request_count);
    ++(observer.in.total_completed_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      observer, __LINE__));
    GTestNonFatalEchoResponseCompare(*request_array[i], response_ptr, __LINE__);
  }
  FcgiRequestIdentifier id {};
  ASSERT_NO_THROW(id = client_inter.SendRequest(local_connection,
    unknown_role));
  ASSERT_NE(id, FcgiRequestIdentifier {});
  ++(observer.co.connection_pending_request_count);
  ++(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  std::vector<FcgiRequest> accept_buffer {};
  while(!accept_buffer.size())
  {
    accept_buffer = inter_uptr->AcceptRequests();
  }
  EXPECT_EQ(accept_buffer.size(), 1U);
  EXPECT_EQ(accept_buffer[0].get_environment_map(), kSharedExerciseParams);
  EXPECT_EQ(accept_buffer[0].get_role(), unknown_role.role);
  EXPECT_TRUE(accept_buffer[0].get_keep_conn());
  EXPECT_EQ(accept_buffer[0].get_STDIN(), kStdinDataForClientExercise);
  EXPECT_EQ(accept_buffer[0].get_DATA(), kFcgiDataForClientExercise);
  ASSERT_NO_THROW(accept_buffer[0].RejectRole(EXIT_FAILURE));
  std::unique_ptr<ServerEvent> event_uptr {};
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
  ASSERT_NE(response_ptr, nullptr);
  --(observer.co.connection_pending_request_count);
  --(observer.in.total_pending_request_count);
  ++(observer.co.connection_completed_request_count);
  ++(observer.in.total_completed_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  EXPECT_EQ(response_ptr->RequestId(), id);
  EXPECT_EQ(response_ptr->AppStatus(), EXIT_FAILURE);
  EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_UNKNOWN_ROLE);
  EXPECT_EQ(response_ptr->Request(), unknown_role);
  EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
  EXPECT_EQ(response_ptr->FcgiStderr().size(), 0U);
}

TEST_F(TestFcgiClientInterfaceTestFixture, SendRequestCaseSet2)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};
  int local_connection {};
  ASSERT_NO_THROW(ASSERT_NE(local_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
      /* connection                         = */ local_connection,
      /* connection_completed_request_count = */ 0U,
      /* is_connected                       = */ true,
      /* management_request_count           = */ 0U,
      /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 1,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));

  // Each case is represented by a 1-byte binary code. The following constants
  // use two bits to encode the three values for each of params, stdin, and
  // data. In bits, the byte is partitioned as:
  //
  //     bit number: 7    6 : 5    4 : 3    2 : 1    0
  //            use: unused : data   : stdin  : params
  //
  // The masks defined below allow the individual values for each of the three
  // used partitions to be extracted.
  constexpr const std::uint8_t params_null      {0x01};
  constexpr const std::uint8_t params_empty     {0x02};
  constexpr const std::uint8_t params_non_empty {0x03};
  constexpr const std::uint8_t stdin_null       {params_null      << 2};
  constexpr const std::uint8_t stdin_empty      {params_empty     << 2};
  constexpr const std::uint8_t stdin_non_empty  {params_non_empty << 2};
  constexpr const std::uint8_t data_null        {stdin_null       << 2};
  constexpr const std::uint8_t data_empty       {stdin_empty      << 2};
  constexpr const std::uint8_t data_non_empty   {stdin_non_empty  << 2};

  constexpr const std::uint8_t params_mask {0x03};
  constexpr const std::uint8_t stdin_mask  {params_mask << 2};
  constexpr const std::uint8_t data_mask   {stdin_mask << 2};

  // Only 17 of the 27 possible cases are exercised.
  const std::vector<std::uint8_t> cases {
  /*  1 */ ( params_null      | stdin_null      | data_null      ),
  /*  2 */ ( params_null      | stdin_null      | data_non_empty ),
  /*  3 */ ( params_null      | stdin_empty     | data_null      ),
  /*  4 */ ( params_null      | stdin_empty     | data_empty     ),
  /*  5 */ ( params_null      | stdin_non_empty | data_empty     ),
  /*  6 */ ( params_null      | stdin_non_empty | data_non_empty ),
  /*  7 */ ( params_empty     | stdin_null      | data_null      ),
  /*  8 */ ( params_empty     | stdin_null      | data_non_empty ),
  /*  9 */ ( params_empty     | stdin_empty     | data_empty     ),
  /* 10 */ ( params_empty     | stdin_empty     | data_non_empty ),
  /* 11 */ ( params_empty     | stdin_non_empty | data_null      ),
  /* 12 */ ( params_empty     | stdin_non_empty | data_non_empty ),
  /* 13 */ ( params_non_empty | stdin_null      | data_null      ),
  /* 14 */ ( params_non_empty | stdin_null      | data_empty     ),
  /* 15 */ ( params_non_empty | stdin_empty     | data_empty     ),
  /* 16 */ ( params_non_empty | stdin_empty     | data_non_empty ),
  /* 17 */ ( params_non_empty | stdin_non_empty | data_null      )};

  ParamsMap empty_map {};
  std::uint8_t placeholder_byte_array[1] = {1U};

  // A request-response cycle is performed for each case.
  for(std::vector<std::uint8_t>::const_iterator case_iter {cases.cbegin()};
    case_iter != cases.cend(); ++case_iter)
  {
    std::uint8_t current_case {*case_iter};
    // Populate the fields of request_representation for the case.
    struct FcgiRequestDataReference request_representation {};
    request_representation.role = FCGI_RESPONDER;
    request_representation.keep_conn = true;
    switch(params_mask & current_case) {
      case params_null : {
        break;
      };
      case params_empty : {
        request_representation.params_map_ptr = &empty_map;
        break;
      };
      case params_non_empty : {
        request_representation.params_map_ptr = &kSharedExerciseParams;
        break;
      };
      default : {
        throw std::logic_error {"Unrecognized params type."};
      };
    }
    switch(stdin_mask & current_case) {
      case stdin_null : {
        break;
      };
      case stdin_empty : {
        request_representation.stdin_begin = placeholder_byte_array;
        request_representation.stdin_end   = placeholder_byte_array;
        break;
      };
      case stdin_non_empty : {
        request_representation.stdin_begin =
          kStdinDataForClientExercise.data();
        request_representation.stdin_end    =
          kStdinDataForClientExercise.data() +
          kStdinDataForClientExercise.size();
        break;
      };
      default : {
        throw std::logic_error {"Unrecognized stdin type."};
      };
    }
    switch(data_mask & current_case) {
      case data_null : {
        break;
      };
      case data_empty : {
        request_representation.data_begin = placeholder_byte_array;
        request_representation.data_end   = placeholder_byte_array;
        break;
      };
      case data_non_empty : {
        request_representation.data_begin =
          kFcgiDataForClientExercise.data();
        request_representation.data_end    =
          kFcgiDataForClientExercise.data() +
          kFcgiDataForClientExercise.size();
        break;
      };
      default : {
        throw std::logic_error {"Unrecognized data type."};
      };
    }
    // Begin the request-response cycle.
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(local_connection,
      request_representation));
    ASSERT_TRUE(id);
    ++(observer.co.connection_pending_request_count);
    ++(observer.in.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
    // Allow the server to process the request.
    std::vector<FcgiRequest> accept_buffer {};
    std::vector<FcgiRequest>::size_type count {0U};
    ASSERT_NO_THROW
    (
      while(!count)
      {
        accept_buffer = inter_uptr->AcceptRequests();
        count = accept_buffer.size();
        if(count)
        {
          ASSERT_EQ(count, 1U);
          const ParamsMap* map_ptr {request_representation.params_map_ptr};
          if(map_ptr == nullptr)
          {
            EXPECT_EQ(accept_buffer[0].get_environment_map().size(), 0U);
          }
          else
          {
            EXPECT_EQ(accept_buffer[0].get_environment_map(), *map_ptr);
          }
          ASSERT_NO_FATAL_FAILURE(GTestFatalRequestEcho(&(accept_buffer[0]),
            __LINE__));
        }
      }
    );
    // Retrieve the response.
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    --(observer.co.connection_pending_request_count);
    --(observer.in.total_pending_request_count);
    ++(observer.co.connection_completed_request_count);
    ++(observer.in.total_completed_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
    GTestNonFatalEchoResponseCompare(request_representation, response_ptr,
      __LINE__);
  }
  ASSERT_NO_THROW(client_inter.ReleaseId(local_connection));
  observer.co.connection_completed_request_count = 0U;
  observer.in.total_completed_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
  ASSERT_NO_THROW(client_inter.CloseConnection(local_connection));
  observer.co.is_connected = false;
  --(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
}

TEST_F(TestFcgiClientInterfaceTestFixture, SendRequestCaseSet3)
{
  // Create the server interface.
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

  TestFcgiClientInterface client_inter {};

  // TEST CASE 5
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
      /* connection                         = */ -1,
      /* connection_completed_request_count = */ 0U,
      /* is_connected                       = */ false,
      /* management_request_count           = */ 0U,
      /* connection_pending_request_count   = */ 0U
    },
    /* in = */
    {
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 0,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter, observer.in,
    __LINE__);
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendRequest(-1,
    kExerciseDataRef)));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter, observer.in,
    __LINE__);
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendRequest(1000,
    kExerciseDataRef)));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter, observer.in,
    __LINE__);

  int local_connection {};
  ASSERT_NO_THROW(ASSERT_NE(local_connection = client_inter.Connect(kUnixPath1,
    0U), -1) << std::strerror(errno));
  observer.co.connection = local_connection;
  observer.co.is_connected = true;
  ++(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  FcgiRequestIdentifier request_id {};
  ASSERT_NO_THROW(request_id = client_inter.SendRequest(local_connection,
    kExerciseDataRef));
  ASSERT_TRUE(request_id);
  ++(observer.co.connection_pending_request_count);
  ++(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW
  (
    std::vector<FcgiRequest> accept_buffer {};
    while(!accept_buffer.size())
    {
      accept_buffer = inter_uptr->AcceptRequests();
    }
    ASSERT_EQ(accept_buffer.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
      kSharedExerciseParams, FCGI_RESPONDER, true, __LINE__));
  );
  // Retrieve the response.
  std::unique_ptr<ServerEvent> event_uptr {};
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
  ASSERT_NE(response_ptr, nullptr);
  EXPECT_EQ(response_ptr->RequestId(), request_id);
  --(observer.co.connection_pending_request_count);
  --(observer.in.total_pending_request_count);
  ++(observer.co.connection_completed_request_count);
  ++(observer.in.total_completed_request_count);
  GTestNonFatalEchoResponseCompare(kExerciseDataRef, response_ptr,
    __LINE__);
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(local_connection)));
  observer.co.is_connected = false;
  --(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendRequest(local_connection,
    kExerciseDataRef)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(local_connection)));
  observer.co.connection_completed_request_count = 0U;
  observer.in.total_completed_request_count = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(EXPECT_FALSE(client_inter.SendRequest(local_connection,
    kExerciseDataRef)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));

  // TEST CASE 6
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(kUnixPath1, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  struct ClientInterfaceObserverValues new_observer
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
      /* total_completed_request_count = */ 0U,
      /* connection_count              = */ 1,
      /* total_pending_request_count   = */ 0U,
      /* ready_event_count             = */ 0U
    }
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  //    Allow the server to accept the connection. This appears to be necessary
  // for the AF_UNIX implementation to immediately detect closure.
  //    One call to AcceptRequests should be sufficient.
  ASSERT_NO_THROW
  (
    std::vector<FcgiRequest> accept_buffer {};
    accept_buffer = inter_uptr->AcceptRequests();
    EXPECT_EQ(accept_buffer.size(), 0U);
  );
  // Destroy the server to allow the client to detect connection closure.
  ASSERT_NO_THROW(delete(inter_uptr.release()));
  FcgiRequestIdentifier closure_request_id {};
  ASSERT_NO_THROW(closure_request_id = client_inter.SendRequest(
    new_connection, kExerciseDataRef));
  EXPECT_EQ(closure_request_id, FcgiRequestIdentifier {});
  ASSERT_FALSE(closure_request_id);
  new_observer.co.is_connected = false;
  --(new_observer.in.connection_count);
  ++(new_observer.in.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  // Retrieve the ConnectionClosure instance.
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  --(new_observer.in.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, new_observer, __LINE__));
  ASSERT_NE(event_uptr.get(), nullptr);
  ConnectionClosure* closure_ptr {dynamic_cast<ConnectionClosure*>(
    event_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId(), (FcgiRequestIdentifier {new_connection,
    0U}));
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
