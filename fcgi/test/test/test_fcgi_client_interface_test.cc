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
#include <typeinfo>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"
#include "external/socket_functions/include/socket_functions.h"
#include "external/as_components_testing/gtest/include/as_components_testing_gtest_utilities.h"

#include "server_interface_combined.h"
#include "test/include/fcgi_si_testing_utilities.h"
#include "test/test/include/client_interface_testing_utilities.h"

namespace as_components {
namespace fcgi {
namespace test {
namespace test {

// TESTING DISCUSSION

// Properties and features of the structure of TestFcgiClientInterface which
// inform testing:
//
// Structural features of TestFcgiClientInterface
// 1) A management request queue for each connection.
// 2) The potential for pending application requests for each connection.
// 3) The potential for completed but unreleased application requests for each
//    connection.
// 4) The persistence of completed and unreleased application requests for a
//    connection across connection descriptor closure and reuse.
// 5) The potential for multiple simultaneous connections to the same server.
// 6) The potential for multiple simultaneous connections to distinct servers.
// 7) The fact that TestFcgiClientInterface can monitor multiple connections
//    with a call to RetrieveServerEvent. Data for multiple responses on
//    distinct connections may be read during a single call to
//    RetrieveServerEvent. Data receipt over a connection may involve multiple
//    periods of reading due to read blocking, TestFcgiClientInterface data
//    receipt multiplexing, and the availability of data on other connections.
// 8) The possibility that a record may not be received in-full before its
//    connection blocks for reading. Given that TestFcgiClientInterface can
//    monitor multiple connections, the partial record data must be stored
//    in a way that allows the record to be completed later.
// 9) Invalid FastCGI records may be received at any time during response
//    receipt. TestFcgiClientInterface must handle such records.
//
// Features of the FastCGI protocol and related notions:
// 1) FastCGI record indivisibility for a given connection. Every record must
//    be received in full. Receipt of a record over a connection cannot be
//    interrupted by the receipt of other data on the connection.
// 2) Record padding, with its several unintended but legal variations, must be
//    handled by any FastCGI client.
// 3) Application requests and responses are, in general, composed of multiple
//    streams. Stream data may be received over multiple records.
// 4) Stream data receipt and, more generally, request data receipt may be
//    interrupted due to the FastCGI features of stream multiplexing and
//    request multiplexing over a single socket connection.
// 5) Connection closure can be performed by both the client
//    (TestFcgiClientInterface) and the server. Closure may occur at any time
//    relative to the receipt and transmission of data by the client and server.

// Connection closure:
//    The interface maintains state for each connection and for the interface
// as a whole. When a connection transitions from being connected to being
// disconnected, this state must be appropriately updated. State update is
// largely independent of the cause of connection closure. The connection may
// have been found to be closed when a call which reads from or writes to the
// connection was made. The methods which do so are:
// Read:
// 1) RetrieveServerEvent
//
// Write:
// 1) SendAbortRequest
// 2) SendBinaryManagementRequest
// 3) SendGetValuesRequest
// 4) SendRequest
//
// Alternatively, the connection may have been closed by the user by a call to
// CloseConnection.
//
//    State update upon connection closure:
// Connection state:
// 1) Management request queue: Cleared.
// 2) Current response record state: Cleared (e.g. because a partial record may
//    be present whose data has not been assigned to a particular response).
// 3) Pending requests (and any associated response data): Cleared.
// 4) Completed and unreleased requests: Unchanged.
// 5) Count observers:
//    a) Connection pending request count: reset to zero.
//    b) Connection completed request count: Unchanged.
// 6) Connection status: Transitioned from true to false (disconnected).
//
// Interface state:
// 1) Total connection count: Decremented.
// 2) Total pending request count: Reduced by the number of pending requests
//    which were cleared.
// 3) Total completed request count: Unchanged.
//
//    To ensure that the interface updates state appropriately when a
// connection is transitioned from connected to disconnected, two cases should
// be tested for any method which may cause this transition.
// 1) The connection is not associated with completed-and-unreleased requests.
// 2) The connection is associated with at least one completed-and-unreleased
//    request.
//
// The first case checks for appropriate update when no information about the
// connection must be preserved after the connection is closed. In both cases,
// the state of the connection should be as follows when the tests are
// performed:
// 1) At least one pending management request is present.
// 2) At least one pending application request is present.
// 3) A partially received record is present.
//
// After the interface processes connection closure, a new connection which
// reuses the descriptor of the previous connection should be made.
// Application and management request-response cycles should be performed to
// verify interface integrity.

// Observable state inspection throughout testing:
//    The majority of test cases are implemented in a way which is intended to
// detect any deviations of the observables of TestFcgiClientInterface from
// their expected values. This is done by invoking all class observers
// (directly or by invocation of a wrapper) upon any occasion when either some
// observable is expected to change or observable constancy is expected and it
// seems reasonable that a class implementation error could result in a
// violation of constancy. A precondition for such checks on observable values
// is the exact specification of observable state upon an internal state
// transition of TestFcgiClientInterface. Such specification largely holds for
// the methods of TestFcgiClientInterface.
//    Several testing utility functions are present to support this technique.

// TEST IMPLEMENTATION

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
//    FCGI_GET_VALUES request is made. An unknown management request is made.
//    A call to CloseConnection is made before the server is allowed to respond
//    to the requests. When the connection has been closed, another call to
//    CloseConnection is made.
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
// CloseConnectionCaseSet4
// 5) A connection is made. Two application requests are made. The server
//    completely responds to one of the requests and then sends a partial
//    record for the other request. A call to RetrieveServerEvents is made.
//    After the complete response is retrieved, a call to CloseConnection is
//    made. A new connection is made to the same server. An application request
//    and response cycle is performed.
// CloseConnectionCaseSet5
// 6) Proper interface state update upon connection closure is inspected by
//    GTestFatalConnectionClosureCheck where connection closure is caused by
//    calls to CloseConnection.
//
// Modules which testing depends on:
// 1) FcgiServerInterface
// 2) The immediate success or failure of connection to a listening socket by
//    the implementation of local (AF_UNIX) sockets.
//
// Other modules whose testing depends on this module: none.

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet1)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
  FcgiRequestIdentifier request_id_2 {};
  ASSERT_NO_THROW(request_id_2 =
    client_inter.SendRequest(local_connection, kExerciseDataRef));
  ++(observer.co.connection_pending_request_count);
  ++(observer.in.total_pending_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
    local_connection, kMapWithValues)));
  ++(observer.co.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
    client_inter, observer, __LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_connection, kBinaryRequest.type, kBinaryRequest.data.data(),
    kBinaryRequest.data.data() + kBinaryRequest.data.size())));
  ++(observer.co.management_request_count);
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
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
  ASSERT_NO_THROW(EXPECT_TRUE(
    client_inter.CloseConnection(local_connection_array[1])));
  observer_array[1].is_connected = false;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_array[1], __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
}

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet3)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    fcgi_response_ptr, __LINE__));
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

TEST_F(TestFcgiClientInterfaceTestFixture, CloseConnectionCaseSet5)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};

  // TEST CASE 6
  auto GTestFatalCloseConnectionCloser = [&client_inter, &inter_uptr]
  (
    int                   connection,
    FcgiRequestIdentifier,
    FcgiServerInterface** server_interface_ptr_ptr,
    int                   invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalCloseConnectionCloser"};
    FcgiServerInterface* server_ptr {inter_uptr.get()};
    ASSERT_NO_THROW(client_inter.CloseConnection(connection));
    // Allows the server to process connection closure.
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
      server_ptr, __LINE__));
    *server_interface_ptr_ptr = server_ptr;
    ASSERT_EQ(client_inter.ReadyEventCount(), 0U);
  };

  ASSERT_NO_FATAL_FAILURE(GTestFatalConnectionClosureCheck(kUnixPath1, 0U,
    &client_inter, inter_uptr.get(), DisconnectWithServerReturn
    {GTestFatalCloseConnectionCloser}, __LINE__));
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
//    server was made and the connection has since been closed.
// 5) The ability of the client to detect connection closure by a server for
//    each domain.
// 6) The non-blocking status of returned file descriptors.
// 7) Correct behavior when a connection is made on a previously used
//    descriptor and that descriptor has completed but unreleased requests.
// 8) Proper handling of connection interruption by signal receipt (situations
//    which cause blocking system calls to fail with errno == EINTR).
// 9) Failure cases:
//    a) An attempt to connect to an address for which there is no
//       listening server. For AF_UNIX, a special case is the existence of a
//       file with the connection file path which is not associated with a
//       listening server.
//    b) address == nullptr
//
// Test cases:
// ConnectCase1
// 1) A server for each of the three supported domains is created in its own
//    process. These servers call AcceptRequests in a loop. Connections are
//    made to these servers from a single client interface. Properties 1 to 6
//    are tested.
// ConnectCase2
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
// ConnectCase3
// 3) (Property 7) A connection is made and a request-response cycle is
//    performed. The connection is closed by invoking close connection. Another
//    connection is made which reuses the descriptor of the previous connection.
//    Another request-response cycle is performed. Throughout, the observers of
//    the client interface are used to verify that reuse of the connection
//    descriptor is performed as specified.
// ConnectCase4
// 4) a) Failure as described above for each of the three domains is checked.
//       For AF_UNIX, failure is checked when the connection path is present
//       and absent in the file system.
//    b) A return of -1 is checked for address == nullptr.

TEST_F(TestFcgiClientInterfaceTestFixture, ConnectCase1)
{
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));
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
        ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(sent_request_ref,
          app_response_ptr, __LINE__));
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
  // server. Because of connection closure by the servers due to their response
  // to requests with keep_conn set to false, only a single request may be
  // sent to each server.
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

TEST_F(TestFcgiClientInterfaceTestFixture, ConnectCase3)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  int connection {};
  ASSERT_NO_THROW(ASSERT_NE(connection = client_inter.Connect(kUnixPath1, 0U),
    -1) << std::strerror(errno));
  struct ClientInterfaceObserverValues observer
  {
    /* co = */
    {
      /* connection                         = */ connection,
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

  auto GTestFatalRequestResponseCycle = [&inter_uptr, &client_inter, &observer]
  (int invocation_line)->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalRequestResponseCycle"};
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(ASSERT_NE(id = client_inter.SendRequest(
      observer.co.connection, kExerciseDataRef), FcgiRequestIdentifier {}));
    ++(observer.co.connection_pending_request_count);
    ++(observer.in.total_pending_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      observer, __LINE__));
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!accept_buffer.size())
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
    );
    EXPECT_EQ(accept_buffer.size(), 1U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
      kSharedExerciseParams, kExerciseDataRef.role, kExerciseDataRef.keep_conn,
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
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
      response_ptr, __LINE__));
  };

  ASSERT_NO_FATAL_FAILURE(GTestFatalRequestResponseCycle(__LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(connection)));
  observer.co.is_connected = false;
  --(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  int second_connection {};
  ASSERT_NO_THROW(ASSERT_NE(second_connection = client_inter.Connect(kUnixPath1,
    0U), -1));
  ASSERT_EQ(second_connection, connection);
  observer.co.is_connected = true;
  ++(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalRequestResponseCycle(__LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(second_connection)));
  observer.co.connection_completed_request_count = 0U;
  observer.in.total_completed_request_count      = 0U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(second_connection)));
  observer.co.is_connected = false;
  --(observer.in.connection_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
}

TEST(Connect, ConnectCase4)
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

  ASSERT_NO_THROW(EXPECT_EQ(client_inter.Connect(nullptr, 8000), -1));
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

TEST(AggregatedObservers, UnknownConnection)
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
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
      response_ptr, __LINE__));
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
// 1) Properties related to record receipt:
//    a) Concurrent response information receipt. A major feature of the
//       FastCGI protocol is the ability of clients and servers to use a single
//       connection for multiple requests or responses. As records are
//       indivisible from the perspective of a single connection, a test should
//       be formulated which examines the behavior of the interface when
//       records for distinct requests are received in an interleaved fashion
//       over a single connection.
//    b) Receipt of data for the response to a request over multiple I/O
//       multipexing cycles. Several independent properties can be identified:
//       1) Partial record receipt with subsequent completion of the record
//          during another I/O multiplexing cycle.
//       2) Receipt of data for a response which uses multiple records. In this
//          case, individual complete records and partial records may be
//          received during distinct I/O multiplexing cycles.
//       3) Blocking and response receipt interruption. It is specified that a
//          call to RetrieveServerEvent will read from a ready connection if a
//          a connection which was read during the call would block and the
//          ready event queue was not made non-empty as a result of reading
//          from the connection. This situation can also be viewed as an
//          interruption of response receipt by the reception of information
//          for another response. A test should be developed which verifies
//          proper I/O multiplexing when multiple active connections are
//          present and response data receipt from these connections is
//          interleaved.
//    c) For responses which use multiple records and distinct streams
//       (responses to application requests), the order and interleaving of
//       record receipt for distinct streams.
//    d) Padding: The use of padding to cause the total byte length of a
//       FastCGI record to be a multiple of eight is recommended by the FastCGI
//       standard. Several unusual cases are possible given this recommendation:
//       1) A record which should have padding does not have any.
//       2) A record which should have padding has an incorrect amount.
//       3) A record which should have padding has a non-minimal amount which
//          causes the eight byte criterion to be met.
//       4) A record which should not have padding has padding which causes the
//          total byte length of the record to not be a multiple of eight.
//       5) A record which should not have padding has padding which does not
//          cause the eight byte recommendation to be violated (redundant
//          padding).
//       The usual cases are:
//       1) A record which should not have padding does not have any.
//       2) A record which should have padding has the minimal amount which
//          causes the eight byte property to be met.
// 2) Properties related to connection closure:
//    a) Detection of connection closure by a server when a call to
//       RetrieveServerEvent is made. Proper update of interface state upon
//       the detection of connection closure.
//       1) A special case is when a partial record has been received on the
//          connection.
// 3) Properties related to exception generation:
//    a) Proper behavior regarding the specified throw of a std::logic_error
//       exception.
//       1) As a special case, correct behavior is verified when
//          ConnectionCount() returns zero but at least one connection is
//          disconnected and associated with completed but unreleased
//          application requests.
//    Note: Testing that an invocation of RetrieveServerEvent which should not
//          throw does not throw occurs throughout the testing of
//          TestFcgiClientInterface.
// 4) Tests based on types derived from ServerEvent:
//    ConnectionClosure:
//       See the description of connection closure handling above.
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
//    a server upon the invocation of RetrieveServerEvent. Some of these
//    properties are also mentioned when the properties which are revelant for
//    record receipt are discussed.
//
//    GetValuesResult:
//       Generation of GetValuesResult instances is tested in the testing of
//    SendGetValuuesRequest.
//    
//    InvalidRecord:
//       Generation of an InvalidRecord instance for each of the conditions
//    that should cause the generation of such a record should be verified.
//    These conditions are listed in the documentation of InvalidRecord. See
//    below for an additional discussion of properties which should be examined
//    when testing InvalidRecord instance generation.
//
//    UnknownType
//       Generation of UnknownType instances is tested in the testing of
//    SendBinaryManagementRequest.
// 5) InvalidRecord
//    Discussion:
//       In each case, the appropriate request, when one is present, should be
//    able to be completed and returned after the construction and return of an
//    InvalidRecord instance. This is implied from the intended property of
//    TestFcgiClientInterface that the receipt of an invalid record should not
//    modify interface state.
//
//    Types:
//    Expected types:
//    Application request types:
//    FCGI_END_REQUEST
//    1) An FCGI_END_REQUEST record is received when no application requests
//       have been made. The record is not malformed.
//    2) Several application requests have been made.
//       a) The version of the FastCGI protocol is not 1 (malformed). Otherwise
//          the record is not malformed and would be accepted.
//       b) The state of received data for an application request is such that
//          an FCGI_END_REQUEST record is expected. An FCGI_END_REQUEST record
//          is received, but the FastCGI request identifier does not match any
//          of the application requests.
//       c) Content length error (malformed). Otherwise the record is not
//          malformed and would be accepted.
//          1) Zero content length.
//          2) Content length is not a multiple of eight.
//          3) Content length is a multiple of eight and greater than eight.
//       d) The record would otherwise be accepted, but the protocol status
//          value is not one the the four allowed values (malformed).
//       e) Non-terminated (active) streams:
//          1) Data has not been received for FCGI_STDOUT and FCGI_STDERR.
//          2) Data has been received for FCGI_STDOUT, but it has not been
//             terminated. No data has been received for FCGI_STDERR.
//          3) As 2 but with FCGI_STDERR and FCGI_STDOUT switched.
//          4) Data has been received for both FCGI_STDOUT and FCGI_STDERR and
//             neither have been terminated.
//    FCGI_STDOUT
//    1) An FCGI_STDOUT record is received when no application requests have
//       been made. The record is not malformed. Both terminal and non-terminal
//       records are sent.
//    2) Several application requests have been made. For each case below,
//       terminal and non-terminal records are sent.
//       a) The version of the FastCGI protocol is not 1 (malformed). Otherwise
//          the record is not malformed and would be accepted.
//       b) An FCGI_STDOUT record is received, and its FastCGI request
//          identifier does not match any of the application requests.
//       c) An FCGI_STDOUT record is received for a request whose FCGI_STDOUT
//          stream has already been completed.
//          1) The record is non-terminal (content length is not zero).
//          2) The record is terminal (zero content length).
//    FCGI_STDERR
//    1) As for FCGI_STDOUT, mutatis mutandis.
//
//    Management request types:
//    FCGI_GET_VALUES_RESULT
//    1) An FCGI_GET_VALUES_RESULT record is received when no management
//       requests have been made. The record is not malformed.
//    2) An FCGI_GET_VALUES_RESULT record is received when several management
//       requests have been made:
//       a) The version of the FastCGI protocol is not 1 (malformed). Otherwise
//          the record is not malformed and would be accepted.
//       b) Non-zero FastCGI request identifier (malformed). Otherwise the
//          record is not malformed and would be accepted.
//       c) The management request at the beginning of the management request
//          queue is not an FCGI_GET_VALUES request.
//    FCGI_UNKNOWN_TYPE
//    1) An FCGI_UKNOWN_TYPE record is received when no management requests
//       have been made. The record is not malformed.
//    2) An FCGI_UKNOWN_TYPE record is received when several management
//       requests have been made:
//       a) The version of the FastCGI protocol is not 1 (malformed). Otherwise
//          the record is not malformed and would be accepted.
//       b) Non-zero FastCGI request identifier (malformed). Otherwise the
//          record is not malformed and would be accepted.
//       c) Content length error (malformed). Otherwise the record is not
//          malformed and would be accepted. Cases:
//          1) Zero content length.
//          2) Content length is not a multiple of eight.
//          3) Content length is a multiple of eight and greater than eight.
//       d) The management request at the front of the queue is an
//          FCGI_GET_VALUES request. The record is not malformed.
//
//    Unexpected, but known types; unknown types.
//    1) The following cases were identified as potentially interesting based
//       on the semantics of the FastCGI record types:
//       a) A record is received with a FastCGI identifier which does not
//          correspond to a pending or completed-but-unreleased request. Types:
//          1) FCGI_BEGIN_REQUEST
//       b) A record is received with a FastCGI identifier which corresponds to
//          a pending application request. Types:
//          1) FCGI_PARAMS
//          2) FCGI_STDIN
//          3) FCGI_DATA
//          4) FCGI_ABORT_REQUEST
//          5) An unknown type, e.g. the type with a FastCGI identifier value
//             of 27U.
//       c) A record is received with the management request identifier for the
//          connection. An FCGI_GET_VALUES request is pending on the connection.
//          Types:
//          1) FCGI_GET_VALUES
//
//
//
//

TEST_F(TestFcgiClientInterfaceTestFixture, RetrieveSeverEventConnectionClosure)
{
  // Creates the server interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  // A unique_ptr instance is used rather than a reference to a unique_ptr to
  // allow later assignments.
  std::unique_ptr<FcgiServerInterface> inter_uptr
    {std::move(std::get<0>(inter_return))};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  // No-op for the closure_detector parameter. An implicit call to
  // RetrieveServerEvent will detect closure.
  auto NoOp = [](TestFcgiClientInterface*, FcgiRequestIdentifier)->void{};
  DisconnectWithServerReturn disconnector
  {
    std::bind(GTestFatalServerDestructionClosureMeta,
    /* inter_args_ptr              */ &inter_args,
    /* server_uptr_ptr             */ &inter_uptr,
    /* client_inter_ptr            */ &client_inter,
    /* closure_detector            */  NoOp,
    /* descriptor_list_ptr         */ &descriptor_resource_list_,
    /* connection                  */  std::placeholders::_1,
    /* pending_application_request */  std::placeholders::_2,
    /* server_interface_ptr_ptr    */  std::placeholders::_3,
    /* invocation_line             */  std::placeholders::_4)
  };

  ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(GTestFatalConnectionClosureCheck(
    kUnixPath1, 0U, &client_inter, inter_uptr.get(), disconnector, __LINE__)));
}

TEST_F(TestFcgiClientInterfaceTestFixture, RetrieveServerEventInvalidRecordSet)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  int connection {};
  ASSERT_NO_THROW(ASSERT_NE(connection = client_inter.Connect(kUnixPath1, 0U),
    -1) << std::strerror(errno));
  // Allows the server to process the connection so that connection + 1
  // is a used file descriptor whose file description is the appropriate socket.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  int server_connection {connection + 1};
  std::uint16_t default_request_id {1U};
  FcgiRequestIdentifier default_identifier {connection, default_request_id};

  auto GTestFatalRetrieveCompare =
  [&client_inter, connection, default_identifier]
  (
    const struct FcgiRequestDataReference& echo_req_ref,
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalRetrieveCompare"};
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(echo_req_ref,
      response_ptr, __LINE__));
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
  };

  auto GTestFatalEchoRetrieveCompare =
  [&inter_uptr, &GTestFatalRetrieveCompare]
  (
    const struct FcgiRequestDataReference& echo_req_ref,
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalEchoRetrieveCompare"};
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW(
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      ASSERT_EQ(accept_buffer.size(), 1U);
      ASSERT_NO_FATAL_FAILURE(GTestFatalOperationForRequestEcho(&accept_buffer,
        *(kExerciseDataRef.params_map_ptr), kExerciseDataRef.role,
        kExerciseDataRef.keep_conn, __LINE__));
    );
    ASSERT_NO_FATAL_FAILURE(GTestFatalRetrieveCompare(echo_req_ref, __LINE__));
  };

  auto GTestFatalRequestResponseCycleAfterInvalidRecord =
  [&client_inter, connection, default_identifier,
   &GTestFatalEchoRetrieveCompare]
  (int invocation_line)->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalRequestResponseAfterInvalidRecord"};
    FcgiRequestIdentifier new_id {};
    ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(new_id = client_inter.SendRequest(
      connection, kExerciseDataRef)));
    EXPECT_EQ(new_id, default_identifier);
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoRetrieveCompare(kExerciseDataRef,
      __LINE__));
  };

  // Cases for FCGI_STDOUT, FCGI_STDERR, and FCGI_END_REQUEST.
  // Cases which are shared for the three types are grouped together.
  // Common state:
  constexpr const int kNoApplicationRequestTypeCount {3U};
  FcgiType no_application_request_type_list[kNoApplicationRequestTypeCount] =
    {FcgiType::kFCGI_STDOUT, FcgiType::kFCGI_STDERR,
     FcgiType::kFCGI_END_REQUEST};
  constexpr const unsigned int kTwoHeaderLength   {2 * FCGI_HEADER_LEN};
  constexpr const unsigned int kThreeHeaderLength {3 * FCGI_HEADER_LEN};

  // Case 1: A record is received when no application requests have been made.
  // After record receipt, an application request-response cycle is completed.
  // For FCGI_STDOUT and FCGI_STDERR, terminal and non-terminal records are
  // sent.
  for(int i {0}; i < kNoApplicationRequestTypeCount; ++i)
  {
    FcgiType current_type {no_application_request_type_list[i]};
    constexpr const int kContentLength {8U};
    constexpr const int kRecordLength {kContentLength + FCGI_HEADER_LEN};
    std::uint8_t record_buffer[kRecordLength] = {};
    PopulateHeader(record_buffer, current_type, default_request_id,
      kContentLength, 0U);
    if(current_type == FcgiType::kFCGI_END_REQUEST)
    {
      record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
        FCGI_REQUEST_COMPLETE;
    }
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ kContentLength,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ current_type,
      /* version            */ FCGI_VERSION_1
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kRecordLength,
      expected_invalid_values, __LINE__));
    ASSERT_NO_FATAL_FAILURE(GTestFatalRequestResponseCycleAfterInvalidRecord(
      __LINE__));
    if((current_type == FcgiType::kFCGI_STDOUT) ||
       (current_type == FcgiType::kFCGI_STDERR))
    {
      // Modify the content length for terminal record status.
      record_buffer[kHeaderContentLengthB0Index] = 0U;
      expected_invalid_values.content_length = 0U;
      ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
        &client_inter, server_connection, record_buffer, FCGI_HEADER_LEN,
        expected_invalid_values, __LINE__));
      ASSERT_NO_FATAL_FAILURE(GTestFatalRequestResponseCycleAfterInvalidRecord(
        __LINE__));
    }
  }

  // Case 2a: Incorrect version number but otherwise correct.
  constexpr const int kStreamTypeCount {kNoApplicationRequestTypeCount - 1};
  // The case for FCGI_END_REQUEST requires the FCGI_STDOUT stream to be
  // completed. Because of this, the cases for FCGI_STDOUT and FCGI_STDERR,
  // which require that the streams are not completed, are executed first.
  for(int i {0}; i < kStreamTypeCount; ++i)
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    FcgiType current_type {no_application_request_type_list[i]};
    std::uint8_t record_buffer[FCGI_HEADER_LEN] = {};
    // Terminal records are used for convenience.
    PopulateHeader(record_buffer, current_type, default_request_id,
      0U, 0U);
    record_buffer[kHeaderVersionIndex] = 2U;
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 0U,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ current_type,
      /* version            */ record_buffer[kHeaderVersionIndex]
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, FCGI_HEADER_LEN,
      expected_invalid_values, __LINE__));
    // Allow the server to respond to the request and validate the response.
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoRetrieveCompare(kExerciseDataRef,
      __LINE__));
  }

  // Case 2a for FCGI_END_REQUEST.
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Complete the FCGI_STDOUT and FCGI_STDERR streams.
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    // Prepare and send the invalid record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    record_buffer[kHeaderVersionIndex] = 2U;
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ record_buffer[kHeaderVersionIndex]
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    // Complete the record.
    record_buffer[kHeaderVersionIndex] = FCGI_VERSION_1;
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(server_connection,
      record_buffer, kTwoHeaderLength)) << std::strerror(errno);
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    EXPECT_EQ(response_ptr->FcgiStderr().size(), 0U);
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Case 2b for FCGI_STDOUT and FCGI_STDERR
  for(int i {0}; i < kStreamTypeCount; ++i)
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    FcgiType current_type {no_application_request_type_list[i]};
    FcgiRequestIdentifier incorrect_identifier {connection,
      static_cast<std::uint16_t>(default_request_id + 1U)};
    std::uint8_t record_buffer[FCGI_HEADER_LEN] = {};
    // Terminal records are used for convenience.
    PopulateHeader(record_buffer, current_type, incorrect_identifier.Fcgi_id(),
      0U, 0U);
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 0U,
      /* padding_length     */ 0U,
      /* id                 */ incorrect_identifier,
      /* type               */ current_type,
      /* version            */ record_buffer[kHeaderVersionIndex]
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, FCGI_HEADER_LEN,
      expected_invalid_values, __LINE__));
    // Allow the server to respond to the request and validate the response.
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoRetrieveCompare(kExerciseDataRef,
      __LINE__));
  }

  // Case 2b for FCGI_END_REQUEST.
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Complete the FCGI_STDOUT and FCGI_STDERR streams.
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    // Prepare and send the invalid record.
    FcgiRequestIdentifier incorrect_identifier {connection,
      static_cast<std::uint16_t>(default_request_id + 1U)};
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      incorrect_identifier.Fcgi_id(), 8U, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ incorrect_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    // Complete the record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(server_connection,
      record_buffer, kTwoHeaderLength)) << std::strerror(errno);
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    EXPECT_EQ(response_ptr->FcgiStderr().size(), 0U);
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Case 2c for FCGI_END_REQUEST.
  constexpr const int kContentLengthCaseCount {3U};
  std::uint16_t content_lengths[kContentLengthCaseCount] = {0U, 3U, 16U};
  for(int i {0}; i < kContentLengthCaseCount; ++i)
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Complete the FCGI_STDOUT and FCGI_STDERR streams.
    std::uint8_t record_buffer[kThreeHeaderLength] {};
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    // Prepare and send the invalid record.
    std::uint16_t current_content_length {content_lengths[i]};
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, current_content_length, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ current_content_length,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    int record_length {static_cast<int>(FCGI_HEADER_LEN +
      current_content_length)};
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, record_length,
      expected_invalid_values, __LINE__));
    // Complete the record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(server_connection,
      record_buffer, kTwoHeaderLength)) << std::strerror(errno);
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    EXPECT_EQ(response_ptr->FcgiStderr().size(), 0U);
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Case 2d for FCGI_END_REQUEST.
  {
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Complete the FCGI_STDOUT and FCGI_STDERR streams.
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    // Prepare and send the invalid record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    std::uint8_t invalid_protocol_status {27U};
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      invalid_protocol_status;
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    // Complete the record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(server_connection,
      record_buffer, kTwoHeaderLength)) << std::strerror(errno);
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr);
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    EXPECT_EQ(response_ptr->FcgiStderr().size(), 0U);
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Cases 2.e.1 and 2.e.2 for FCGI_END_REQUEST.
  {
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Prepare and send the invalid record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    std::uint8_t application_record_buffer[kTwoHeaderLength] {};
    // Complete the FCGI_STDOUT stream and send data over FCGI_STDERR.
    PopulateHeader(application_record_buffer, FcgiType::kFCGI_STDOUT,
      default_request_id, 0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection,
      application_record_buffer, static_cast<std::uint32_t>(FCGI_HEADER_LEN)))
      << std::strerror(errno);
    // The values of the FCGI_STDERR stream are immaterial.
    PopulateHeader(application_record_buffer, FcgiType::kFCGI_STDERR,
      default_request_id, 8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, application_record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    // Complete the FCGI_STDERR stream and the response.
    PopulateHeader(application_record_buffer, FcgiType::kFCGI_STDERR,
      default_request_id, 0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection,
      application_record_buffer, static_cast<std::uint32_t>(FCGI_HEADER_LEN)))
      << std::strerror(errno);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    // Retrieve and verify the response.
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr) << typeid(*event_uptr).name();
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    std::uint8_t* stderr_content_begin_ptr {application_record_buffer +
      FCGI_HEADER_LEN};
    std::uint8_t* stderr_content_end_ptr {stderr_content_begin_ptr + 8U};
    ASSERT_NO_FATAL_FAILURE(GTestFatalStreamDataComparison(
      stderr_content_begin_ptr, stderr_content_end_ptr,
      response_ptr->FcgiStderr(), __LINE__));
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Case 2.e.3 for FCGI_END_REQUEST.
  {
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Send data over FCGI_STDERR. The values of the FCGI_STDERR stream are
    // immaterial.
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    // Prepare and send the invalid record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    // Complete the FCGI_STDERR stream, the FCGI_STDOUT stream, and the
    // response.
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    // Retrieve and verify the response.
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] = 0U;
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr) << typeid(*event_uptr).name();
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    std::uint8_t* stderr_content_begin_ptr {record_buffer + FCGI_HEADER_LEN};
    std::uint8_t* stderr_content_end_ptr {stderr_content_begin_ptr + 8U};
    ASSERT_NO_FATAL_FAILURE(GTestFatalStreamDataComparison(
      stderr_content_begin_ptr, stderr_content_end_ptr,
      response_ptr->FcgiStderr(), __LINE__));
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Case 2.e.4 for FCGI_END_REQUEST.
  {
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ default_identifier,
      /* type               */ FcgiType::kFCGI_END_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Send data over FCGI_STDERR. The values of the FCGI_STDERR stream are
    // immaterial.
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    // Prepare and send the invalid record.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    // Complete the FCGI_STDERR stream, the FCGI_STDOUT stream, and the
    // response.
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDERR, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    ASSERT_EQ(static_cast<std::uint32_t>(FCGI_HEADER_LEN),
      socket_functions::SocketWrite(server_connection, record_buffer,
      static_cast<std::uint32_t>(FCGI_HEADER_LEN))) << std::strerror(errno);
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    // Retrieve and verify the response.
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] = 0U;
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr) << typeid(*event_uptr).name();
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    std::uint8_t* content_begin_ptr {record_buffer + FCGI_HEADER_LEN};
    std::uint8_t* content_end_ptr {content_begin_ptr + 8U};
    ASSERT_NO_FATAL_FAILURE(GTestFatalStreamDataComparison(
      content_begin_ptr, content_end_ptr, response_ptr->FcgiStderr(),
      __LINE__));
    ASSERT_NO_FATAL_FAILURE(GTestFatalStreamDataComparison(
      content_begin_ptr, content_end_ptr, response_ptr->FcgiStdout(),
      __LINE__));
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Cases 2.c.1 and 2.c.2 for FCGI_STDOUT and FCGI_STDERR.
  {
    std::uint8_t record_buffer[kTwoHeaderLength] {};
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(connection,
      kExerciseDataRef));
    ASSERT_EQ(id, default_identifier);
    // Complete the FCGI_STDOUT and FCGI_STDERR streams.
    PopulateHeader(record_buffer, FcgiType::kFCGI_STDOUT, default_request_id,
      0U, 0U);
    PopulateHeader(record_buffer + FCGI_HEADER_LEN, FcgiType::kFCGI_STDERR,
      default_request_id, 0U, 0U);
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(server_connection,
      record_buffer, kTwoHeaderLength)) << std::strerror(errno);
    struct ExpectedInvalidRecordValues expected_invalid_values {};
    expected_invalid_values.content_buffer_ptr = record_buffer +
      FCGI_HEADER_LEN;
    expected_invalid_values.padding_length     = 0U;
    expected_invalid_values.id                 = default_identifier;
    expected_invalid_values.version            = FCGI_VERSION_1;
    std::memset(record_buffer + FCGI_HEADER_LEN, 0, 8U);
    constexpr const int kTerminalOrNonterminalCaseCount {2};
    constexpr const int content_lengths[kTerminalOrNonterminalCaseCount] =
      {3, 0};
    constexpr const int record_lengths[kTerminalOrNonterminalCaseCount] =
      {static_cast<int>(FCGI_HEADER_LEN) + 3,
       static_cast<int>(FCGI_HEADER_LEN)};
    for(int i {0}; i < kStreamTypeCount; ++i)
    {
      for(int j {0}; j < kTerminalOrNonterminalCaseCount; ++j)
      {
        // j == 0, then non-terminal; j == 1, then terminal.
        // A data length less than 8 is used without padding.
        const FcgiType current_type {no_application_request_type_list[i]};
        const int current_content_length {content_lengths[j]};
        PopulateHeader(record_buffer, current_type, default_request_id,
          current_content_length, 0U);
        expected_invalid_values.type           = current_type;
        expected_invalid_values.content_length = current_content_length;
        ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
          &client_inter, server_connection, record_buffer, record_lengths[j],
          expected_invalid_values, __LINE__));
      }
    }
    // Send an FCGI_END_REQUEST record to complete the response.
    PopulateHeader(record_buffer, FcgiType::kFCGI_END_REQUEST,
      default_request_id, 8U, 0U);
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] =
      FCGI_REQUEST_COMPLETE;
    ASSERT_EQ(kTwoHeaderLength, socket_functions::SocketWrite(
      server_connection, record_buffer, kTwoHeaderLength)) <<
      std::strerror(errno);
    // Retrieve and verify the response.
    record_buffer[FCGI_HEADER_LEN + kEndRequestProtocolStatusIndex] = 0U;
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
    ASSERT_NE(response_ptr, nullptr) << typeid(*event_uptr).name();
    EXPECT_EQ(response_ptr->AppStatus(), 0);
    EXPECT_EQ(response_ptr->ProtocolStatus(), FCGI_REQUEST_COMPLETE);
    EXPECT_EQ(response_ptr->Request(), kExerciseDataRef);
    EXPECT_EQ(response_ptr->FcgiStdout().size(), 0U);
    EXPECT_EQ(response_ptr->FcgiStderr().size(), 0U);
    EXPECT_EQ(response_ptr->RequestId(), default_identifier);
    // Release the completed request to allow reuse of default_identifier.
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.ReleaseId(connection)));
    // Clear the request from the server.
    std::vector<FcgiRequest> accept_buffer {};
    ASSERT_NO_THROW
    (
      while(!(accept_buffer.size()))
      {
        accept_buffer = inter_uptr->AcceptRequests();
      }
      EXPECT_EQ(accept_buffer.size(), 1U);
      accept_buffer.clear();
    );
  }

  // Cases for expected management request types: FCGI_GET_VALUES_RESULT and
  // FCGI_UNKNOWN_TYPE.

  FcgiRequestIdentifier management_identifier
    {connection, FCGI_NULL_REQUEST_ID};

  auto GTestFatalGetValuesRetrieveCompare =
  [&inter_uptr, &client_inter, management_identifier]
  (
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalGetValuesRetrieveCompare"};
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
      __LINE__));
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    GetValuesResult* gvr_ptr {dynamic_cast<GetValuesResult*>(event_uptr.get())};
    ASSERT_NE(gvr_ptr, nullptr);
    EXPECT_FALSE(gvr_ptr->IsCorrupt());
    EXPECT_EQ(gvr_ptr->RequestMap(), kMpxsNameMap);
    EXPECT_EQ(gvr_ptr->ResponseMap(), kMpxsMapWithValue);
    EXPECT_EQ(gvr_ptr->RequestId(), management_identifier);
    ASSERT_EQ(client_inter.ReadyEventCount(), 0U);
  };

  // Populate an FCGI_GET_VALUES_RESULT record for use across test cases.
  constexpr const int MPXS_name_length {15};
  constexpr const int encoded_MPXS_pair_byte_length
    {2 + MPXS_name_length + 1};
  constexpr const int MPXS_pair_record_length
    {static_cast<int>(FCGI_HEADER_LEN) + encoded_MPXS_pair_byte_length};
  std::uint8_t MPXS_record_buffer[MPXS_pair_record_length] {};
  std::uint8_t* MPXS_content_ptr {MPXS_record_buffer + FCGI_HEADER_LEN};
  const struct ExpectedInvalidRecordValues MPXS_expected_invalid_values
  {
    /* content_buffer_ptr */ MPXS_content_ptr,
    /* content_length     */ encoded_MPXS_pair_byte_length,
    /* padding_length     */ 0U,
    /* id                 */ management_identifier,
    /* type               */ FcgiType::kFCGI_GET_VALUES_RESULT,
    /* version            */ FCGI_VERSION_1
  };
  PopulateHeader(MPXS_record_buffer, FcgiType::kFCGI_GET_VALUES_RESULT,
    FCGI_NULL_REQUEST_ID, encoded_MPXS_pair_byte_length, 0U);
  *MPXS_content_ptr = MPXS_name_length;
  ++MPXS_content_ptr;
  *MPXS_content_ptr = 1U;
  ++MPXS_content_ptr;
  std::memcpy(MPXS_content_ptr, FCGI_MPXS_CONNS.data(), MPXS_name_length);
  MPXS_content_ptr += MPXS_name_length;
  *MPXS_content_ptr = '1';

  std::uint8_t unknown_type_record_buffer[kTwoHeaderLength] {};
  PopulateHeader(unknown_type_record_buffer, FcgiType::kFCGI_UNKNOWN_TYPE,
    FCGI_NULL_REQUEST_ID, 8U, 0U);
  // The first content byte is used to store the unknown type.
  unknown_type_record_buffer[FCGI_HEADER_LEN] = 27U;
  const struct ExpectedInvalidRecordValues unknown_type_expected_invalid_values
  {
    /* content_buffer_ptr */ unknown_type_record_buffer + FCGI_HEADER_LEN,
    /* content_length     */ 8U,
    /* padding_length     */ 0U,
    /* id                 */ management_identifier,
    /* type               */ FcgiType::kFCGI_UNKNOWN_TYPE,
    /* version            */ FCGI_VERSION_1
  };
  const struct ManagementRequestData unknown_request
  {
    /* type */       static_cast<FcgiType>(
      unknown_type_record_buffer[FCGI_HEADER_LEN]),
    /* params_map */ {},
    /* data */       {}
  };

  // Case 1 for FCGI_GET_VALUES_RESULT and FCGI_UNKNOWN_TYPE.
  // Cases 2.a and 2.b for FCGI_GET_VALUES_RESULT.
  {
    // Case 1 for FCGI_GET_VALUES_RESULT.
    std::uint8_t local_MPXS_record_buffer[MPXS_pair_record_length];
    std::memcpy(local_MPXS_record_buffer, MPXS_record_buffer,
      MPXS_pair_record_length);
    struct ExpectedInvalidRecordValues local_MPXS_expected_invalid_values
      {MPXS_expected_invalid_values};
    // Update with the correct buffer pointer.
    local_MPXS_expected_invalid_values.content_buffer_ptr =
      local_MPXS_record_buffer + FCGI_HEADER_LEN;
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_MPXS_record_buffer,
      MPXS_pair_record_length, local_MPXS_expected_invalid_values, __LINE__));

    // Case 1 for FCGI_UNKNOWN_TYPE.
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, unknown_type_record_buffer,
      kTwoHeaderLength, unknown_type_expected_invalid_values, __LINE__));

    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(connection,
      kMpxsNameMap)));
    // Case 2.a for FCGI_GET_VALUES_RESULT.
    local_MPXS_record_buffer[kHeaderVersionIndex] = 7U;
    local_MPXS_expected_invalid_values.version    = 7U;
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_MPXS_record_buffer,
      MPXS_pair_record_length, local_MPXS_expected_invalid_values, __LINE__));
    // Case 2.b for FCGI_GET_VALUES_RESULT.
    local_MPXS_record_buffer[kHeaderVersionIndex] = FCGI_VERSION_1;
    local_MPXS_expected_invalid_values.version    = FCGI_VERSION_1;
    local_MPXS_record_buffer[kHeaderRequestIDB1Index] = 0U;
    local_MPXS_record_buffer[kHeaderRequestIDB0Index] = 10U;
    local_MPXS_expected_invalid_values.id = {connection, 10U};
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_MPXS_record_buffer,
      MPXS_pair_record_length, local_MPXS_expected_invalid_values, __LINE__));
    // Check for proper response processing.
    ASSERT_NO_FATAL_FAILURE(GTestFatalGetValuesRetrieveCompare(__LINE__));
  }

  auto GTestFatalProcessAndRetrieveUnknownRequest =
  [&inter_uptr, &client_inter, &unknown_request, management_identifier]
  (
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalProcessAndRetrieveUnknownRequest"};
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
      __LINE__));
    std::unique_ptr<ServerEvent> event_uptr {};
    ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(event_uptr.get(), nullptr);
    UnknownType* unknown_type_ptr
      {dynamic_cast<UnknownType*>(event_uptr.get())};
    ASSERT_NE(unknown_type_ptr, nullptr);
    EXPECT_EQ(unknown_type_ptr->Request(), unknown_request);
    EXPECT_EQ(unknown_type_ptr->Type(), unknown_request.type);
    EXPECT_EQ(unknown_type_ptr->RequestId(), management_identifier);
  };

  // Case 2.c for FCGI_GET_VALUES_RESULT.
  // Send an unknown management request so that reception of an
  // FCGI_GET_VALUES_RESULT record is erroneous.
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    connection, unknown_request.type, nullptr, nullptr)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
    &client_inter, server_connection, MPXS_record_buffer,
    MPXS_pair_record_length, MPXS_expected_invalid_values, __LINE__));
  // Allow the server to process the response.
  ASSERT_NO_FATAL_FAILURE(GTestFatalProcessAndRetrieveUnknownRequest(
    __LINE__));

  // Cases 2.a, 2.b, and 2.c for FCGI_UNKNOWN_TYPE.
  {
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
      connection, unknown_request.type, nullptr, nullptr)));
    struct ExpectedInvalidRecordValues
    local_unknown_type_expected_invalid_values
      {unknown_type_expected_invalid_values};
    std::uint8_t local_unknown_type_record_buffer[kThreeHeaderLength] = {};
    // Update local_unknown_type_expected_invalid_values with the correct
    // pointer to the local buffer!
    local_unknown_type_expected_invalid_values.content_buffer_ptr =
      local_unknown_type_record_buffer + FCGI_HEADER_LEN;
    std::memcpy(local_unknown_type_record_buffer, unknown_type_record_buffer,
      kTwoHeaderLength);

    // Case 2.a
    local_unknown_type_record_buffer[kHeaderVersionIndex] = 7U;
    local_unknown_type_expected_invalid_values.version    = 7U;
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_unknown_type_record_buffer,
      kTwoHeaderLength, local_unknown_type_expected_invalid_values, __LINE__));
    // Case 2.b
    local_unknown_type_record_buffer[kHeaderVersionIndex] = FCGI_VERSION_1;
    local_unknown_type_expected_invalid_values.version    = FCGI_VERSION_1;
    local_unknown_type_record_buffer[kHeaderRequestIDB1Index] = 0U;
    local_unknown_type_record_buffer[kHeaderRequestIDB0Index] = 10U;
    local_unknown_type_expected_invalid_values.id = {connection, 10U};
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_unknown_type_record_buffer,
      kTwoHeaderLength, local_unknown_type_expected_invalid_values, __LINE__));
    // Case 2.c.1 Zero content length.
    local_unknown_type_expected_invalid_values.id = management_identifier;
    local_unknown_type_expected_invalid_values.content_length = 0U;
    PopulateHeader(local_unknown_type_record_buffer,
      FcgiType::kFCGI_UNKNOWN_TYPE, FCGI_NULL_REQUEST_ID, 0U, 0U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_unknown_type_record_buffer,
      FCGI_HEADER_LEN, local_unknown_type_expected_invalid_values, __LINE__));
    // Case 2.c.2 Content length greater than zero but less than 8.
    local_unknown_type_expected_invalid_values.content_length = 3U;
    local_unknown_type_expected_invalid_values.padding_length = 2U;
    PopulateHeader(local_unknown_type_record_buffer,
      FcgiType::kFCGI_UNKNOWN_TYPE, FCGI_NULL_REQUEST_ID, 3U, 2U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_unknown_type_record_buffer,
      FCGI_HEADER_LEN + 3 + 2, local_unknown_type_expected_invalid_values,
      __LINE__));
    // Case 2.c.3 Content length greater than 8 and a multiple of 8.
    local_unknown_type_expected_invalid_values.content_length = 16U;
    local_unknown_type_expected_invalid_values.padding_length = 0U;
    PopulateHeader(local_unknown_type_record_buffer,
      FcgiType::kFCGI_UNKNOWN_TYPE, FCGI_NULL_REQUEST_ID, 16U, 0U);
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, local_unknown_type_record_buffer,
      kThreeHeaderLength, local_unknown_type_expected_invalid_values,
      __LINE__));
    // Check the response.
    ASSERT_NO_FATAL_FAILURE(GTestFatalProcessAndRetrieveUnknownRequest(
      __LINE__));
  }

  // Case 2.d for FCGI_UNKNOWN_TYPE
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(connection,
    kMpxsMapWithValue)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
    &client_inter, server_connection, unknown_type_record_buffer,
    kTwoHeaderLength, unknown_type_expected_invalid_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalGetValuesRetrieveCompare(__LINE__));

  // Tests for unexpected but known record types and for unknown record types.
  {
    std::uint8_t record_buffer[kTwoHeaderLength] = {};
    // The FastCGI request identifier of the invalid record is unused and not
    // associated with a completed-but-unreleased record.
    // Type: FCGI_BEGIN_REQUEST
    std::uint16_t unused_id {static_cast<std::uint16_t>(2U)};
    PopulateBeginRequestRecord(record_buffer, unused_id, FCGI_RESPONDER, true);
    struct ExpectedInvalidRecordValues expected_invalid_values
    {
      /* content_buffer_ptr */ record_buffer + FCGI_HEADER_LEN,
      /* content_length     */ 8U,
      /* padding_length     */ 0U,
      /* id                 */ {connection, unused_id},
      /* type               */ FcgiType::kFCGI_BEGIN_REQUEST,
      /* version            */ FCGI_VERSION_1
    };
    ASSERT_NO_FATAL_FAILURE(GTestFatalSendRecordAndExpectInvalidRecord(
      &client_inter, server_connection, record_buffer, kTwoHeaderLength,
      expected_invalid_values, __LINE__));
    
    // A pending application request is present for the default identifier and
    // 
  }
}

TEST_F(TestFcgiClientInterfaceTestFixture, RetrieveServerEventExceptions)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  // This call may block if the TestFcgiClientInterface instance does not
  // throw as expected.
  ASSERT_EQ(client_inter.ConnectionCount(), 0U);
  ASSERT_EQ(client_inter.ReadyEventCount(), 0U);
  EXPECT_THROW(client_inter.RetrieveServerEvent(), std::logic_error);
  // Perform a request-response cycle to establish a completed-but-unreleased
  // application request.
  int connection {};
  ASSERT_NO_THROW(ASSERT_NE(connection = client_inter.Connect(kUnixPath1, 0U),
    -1)) << std::strerror(errno);
  FcgiRequestIdentifier request_identifier {};
  ASSERT_NO_THROW(ASSERT_NE(request_identifier = client_inter.SendRequest(
    connection, kExerciseDataRef), FcgiRequestIdentifier {}));
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsRequestEcho(inter_uptr.get(),
    *(kExerciseDataRef.params_map_ptr), FCGI_RESPONDER, true, __LINE__));
  std::unique_ptr<ServerEvent> event_uptr {};
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  FcgiResponse* response_ptr {dynamic_cast<FcgiResponse*>(event_uptr.get())};
  ASSERT_NE(response_ptr, nullptr);
  EXPECT_EQ(response_ptr->RequestId(), request_identifier);
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
  ASSERT_EQ(client_inter.CompletedRequestCount(), 1U);
  delete(inter_uptr.release());
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  ConnectionClosure* closure_ptr {dynamic_cast<ConnectionClosure*>(
    event_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId(), (FcgiRequestIdentifier {connection,
    FCGI_NULL_REQUEST_ID}));
  ASSERT_EQ(client_inter.ConnectionCount(), 0);
  ASSERT_EQ(client_inter.ReadyEventCount(), 0U);
  EXPECT_THROW(client_inter.RetrieveServerEvent(), std::logic_error);
}

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
// SendAbortRequestTestCaseSet1
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
// SendAbortRequestTestCaseSet2
// 3) A connection is made. A request is made with keep_conn set to false. The
//    server is allowed to process the request. A call to SendAbortRequest is
//    made. Proper behavior regarding detection of connection closure is
//    verified. At least one pending management request should be present when
//    connection closure is detected to allow proper clearing of the queue for
//    the connection to be verified.
// SendAbortRequestTestCaseSet3
// 4) Connection closure detection when SendAbortRequest is called is exercised
//    through GTestFatalConnectionClosureCheck.
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
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
  FcgiRequestIdentifier first_response_id {response_ptr->RequestId()};
  bool first_request_is_first {first_response_id == request_id_array[0]};
  if(!first_request_is_first)
  {
    EXPECT_EQ(first_response_id, request_id_array[request_count - 1]);
  }
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
  ASSERT_NO_THROW(event_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(event_uptr.get(), nullptr);
  response_ptr = dynamic_cast<FcgiResponse*>(event_uptr.get());
  ASSERT_NE(response_ptr, nullptr);
  --(observer.in.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
  EXPECT_EQ(response_ptr->RequestId(), (first_request_is_first) ?
    request_id_array[request_count - 1] : request_id_array[0]);
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
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
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
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
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
      local_connection, kMapWithValues)));
  ++(observer.co.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
    observer, __LINE__));
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
  observer.co.management_request_count = 0U;
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

TEST_F(TestFcgiClientInterfaceTestFixture, SendAbortRequestTestCaseSet3)
{
  // Creates the server interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  // Move the unique_ptr to allow later assignments.
  std::unique_ptr<FcgiServerInterface> inter_uptr
    {std::move(std::get<0>(inter_return))};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  // No-op for the closure_detector parameter. An implicit call to
  // RetrieveServerEvent will detect closure.
  auto CallSendAbortRequest = []
  (
    TestFcgiClientInterface* client_inter_ptr,
    FcgiRequestIdentifier pending_application_request
  )->void
  {
    ASSERT_FALSE(client_inter_ptr->SendAbortRequest(
      pending_application_request));
  };
  DisconnectWithServerReturn disconnector
  {
    std::bind(GTestFatalServerDestructionClosureMeta,
    /* inter_args_ptr              */ &inter_args,
    /* server_uptr_ptr             */ &inter_uptr,
    /* client_inter_ptr            */ &client_inter,
    /* closure_detector            */  CallSendAbortRequest,
    /* descriptor_list_ptr         */ &descriptor_resource_list_,
    /* connection                  */  std::placeholders::_1,
    /* pending_application_request */  std::placeholders::_2,
    /* server_interface_ptr_ptr    */  std::placeholders::_3,
    /* invocation_line             */  std::placeholders::_4)
  };

  ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(GTestFatalConnectionClosureCheck(
    kUnixPath1, 0U, &client_inter, inter_uptr.get(), disconnector, __LINE__)));
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

// SendGetValuesRequest and SendBinaryManagementRequest
// Test cases:
// Test case set 1:
//  1) A single request is made with the SendGetValuesRequest copy overload.
//     The response is retrieved and verified. To test the specified behavior
//     for non-empty input map values, such a value is present in the input map.
//  2) As 1, but the move overload is used.
//  3) SendBinaryManagementRequest copy overload.
//     a) An FCGI_GET_VALUES request and an unknown management request are made.
//     b) An empty byte sequence which is not described by null pointers is
//        used in a call to SendBinaryManagementRequest.
//     c) As b, but a sequence which is described by null pointers is used.
//  4) SendBinaryManagementRequest move overload.
//     a) An FCGI_GET_VALUES request and an unknown management request are made.
//     b) An empty std::vector<std::uint8_t>&& argument is used in a call to
//        the move overload.
//  5) To ensure that the client interface correctly handles request order,
//     four requests are made in series to the same server interface. The
//     mechanism by which the request is made alternates from invoking
//     SendGetValuesRequest to SendBinaryManagementRequest. For
//     SendBinaryManagementRequest, one request is made for each of
//     FCGI_GET_VALUES and an unknown management request. The server interface
//     is then allowed to process the requests. It is verified that the client
//     interface returns the correct responses in the order in which the
//     requests were enqueued.
//  6) The client interface must correctly handle the presence of multiple
//     connections to the same server. Two connections are made to the same
//     server. Distinct management requests are sent on each connection via
//     both SendGetValuesRequest and SendBinaryManagementRequest. For
//     SendBinaryManagementRequest, one request is made for FCGI_GET_VALUES
//     and an unknown management request. The appropriate responses are
//     verified.
//  7) The client interface must correctly handle the presence of multiple,
//     distinct server interfaces. Two server interfaces are created, and each
//     is sent a request with distinct content. The appropriate responses are
//     verified. This is done for each of a SendGetValuesRequest and
//     SendBinaryManagementRequest. For SendBinaryManagementRequest, an
//     FCGI_GET_VALUES request is made, and an unknown management request is
//     made.
//  8) (Failure case) Calls are made with a value for connection which does not
//     refer to a connection which is managed by the interface.
//     a) A negative number.
//     b) A positive number.
//  9) (Failure case) For SendGetValuesRequests, calls are made with names
//     which result in FCGI_GET_VALUES record content which cannot be encoded
//     in a single FCGI_GET_VALUES record. A single large name is used for
//     simplicity. For SendBinaryManagementRequest, calls are made with a
//     byte sequence whose length exceeds the maximum value of a FastCGI record.
//     The copy and move overloads are both tested.
// 10) (Failure case) A call is made after the peer closed the connection.
//     Correct detection of connection closure and correct interface state
//     update, which includes clearing the queue of unanswered requests and the
//     enqueuement of an appropriate ConnectionClosure instance, are verified.
// Test case set 2: Management request queue interaction with CloseConnection.
// 11) To ensure that the client interface correctly handles connection
//     closure by the client interface user in the case that completed and
//     unreleased requests are present, an FCGI_GET_VALUES request is made when
//     such an application request is present. Then the connection is closed by
//     the user. A new connection with the same descriptor value is made. Then
//     a management request is made which should have a response which is
//     distinct from the response that would have been returned for the
//     previous request. It is verified that the correct response is returned.
// 12) As 10, but connection closure is performed by the server and detected
//     by the client interface.
// Test case set 3: Exercising error detection during GetValuesResult
// construction.
// 13) a) An FCGI_GET_VALUES request is made and a response with a FastCGI
//        name-value pair encoding error is sent. The detection of this error
//        and the specified behavior during construction of the GetValuesResult
//        instance are verified. In particular, for GetValuesResult instance
//        gvr, gvr.IsCorrupt() == true and gvr.ResponseMap() == ParamsMap {}.
//     b) An FCGI_GET_VALUES request is made and a response with a duplicate
//        name where the duplicates have the same value is received.
//     c) As b, but the duplicates have distinct values.
// Test case set 4: Connection closure detection as exercised through
// GTestFatalConnectionClosureCheck and GTestFatalServerDestructionClosureMeta
// with a call to SendBinaryManagementRequest.
// Test case set 5: Connection closure detection as exercised through
// GTestFatalConnectionClosureCheck and GTestFatalServerDestructionClosureMeta
// with a call to SendGetValuesRequest.
//
// Modules and features which testing depends on:
//  1) The immediate detection of peer closure by the implementation of
//     local (AF_UNIX) sockets.
//  2) The immediate success or failure of connection to a listening socket
//     by the implementation of local (AF_UNIX) sockets. In particular, that a
//     call to accept in the server process is not required for the call of
//     connect by the client to return. This allows testing to be performed in
//     a single process.
//  Server interface behavior:
//  3) Receipt of and response to FCGI_GET_VALUES requests by
//     ::as_components::fcgi::FcgiServerInterface.
//  4) FcgiRequest object production through calls to
//     ::as_components::fcgi::FcgiServerInterface::AcceptRequests
//     and correct transmission of terminal stream records and an
//     FCGI_END_REQUEST record by ::as_components::fcgi::FcgiRequest::Complete.
// Other modules whose testing depends on this module: none.

TEST_F(TestFcgiClientInterfaceTestFixture, ManagementRequestsTestCaseSet1)
{
  //    Creates server interfaces to respond to FCGI_GET_VALUES requests sent
  // by a client interface.
  //    The child process is forked to house the second interface. The child
  // is killed when the second interface is no longer needed. SIGKILL is sent
  // by the parent. This behavior does not allow the cleanup mechanism of
  // Google Test test fixtures to be performed. The AF_UNIX file path of the
  // child server interface is scheduled for release in the parent process.
  //    Synchronization between the parent and child is needed to ensure that
  // the server interface has been constructed before the client (parent) tries
  // to connect to it.
  sigset_t sigusr1_set  {};
  ASSERT_NE(sigemptyset(&sigusr1_set), -1) << std::strerror(errno);
  ASSERT_NE(sigaddset(&sigusr1_set, SIGUSR1), -1) << std::strerror(errno);
  ASSERT_NE(sigprocmask(SIG_BLOCK, &sigusr1_set, nullptr), -1) <<
    std::strerror(errno);

  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath2));

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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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

  // Populate a byte sequence for an FCGI_GET_VALUES request to be sent by
  // SendBinaryManagementRequest.
  constexpr int fcgi_max_conns_name_length {14};
  constexpr int get_values_binary_content_length {2 +
    fcgi_max_conns_name_length};
  std::uint8_t get_values_sequence[get_values_binary_content_length] = {};
  // Note that padding is not needed as get_values_binary_content_length
  // == 16 and 16 % 8 == 0.
  std::uint8_t* byte_ptr {get_values_sequence};
  *byte_ptr = fcgi_max_conns_name_length;
  ++byte_ptr;
  *byte_ptr = 0U;
  ++byte_ptr;
  std::memcpy(byte_ptr, FCGI_MAX_CONNS.data(), fcgi_max_conns_name_length);
  ParamsMap request_map  {};
  ParamsMap response_map {{FCGI_MAX_CONNS, {'1', '0'}}};

  int local_socket {};
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(kUnixPath1, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);

  // TEST CASE 1
  // Tests the SendGetValuesRequest copy overload.
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
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues)) << std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  std::unique_ptr<ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  GetValuesResult* gvr_ptr
    {dynamic_cast<GetValuesResult*>(result_uptr.get())};
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__);

  // TEST CASE 2
  // Tests the SendGetValueRequest move overload.
  ParamsMap value_map_copy {kMapWithValues};
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(local_socket,
    std::move(value_map_copy))) << std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__);

  // TEST CASE 3
  // Tests the SendBinaryManagementRequest copy overload.
  // FCGI_GET_VALUES through SendBinaryManagementRequest.
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, FcgiType::kFCGI_GET_VALUES, get_values_sequence,
    get_values_sequence + get_values_binary_content_length)) <<
      std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, request_map, response_map, __LINE__);
  // Unknown management request.
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, kBinaryRequest.type, kBinaryRequest.data.data(),
    kBinaryRequest.data.data() + kBinaryRequest.data.size())) <<
      std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  UnknownType* unknown_ptr {dynamic_cast<UnknownType*>(result_uptr.get())};
  ASSERT_NE(unknown_ptr, nullptr);
  EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
    {local_socket, 0U}));
  EXPECT_EQ(unknown_ptr->Request(), kBinaryRequest);
  EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);
  // An empty range described by non-null pointers and an empty range described
  // by null pointers.
  constexpr const int empty_range_count {2};
  for(int i {0}; i != empty_range_count; ++i)
  {
    if(i == 0)
    {
      ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
        local_socket, kBinaryRequest.type, kBinaryRequest.data.data(),
        kBinaryRequest.data.data())) << std::strerror(errno));
    }
    else
    {
      ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
        local_socket, kBinaryRequest.type, nullptr, nullptr)) <<
          std::strerror(errno));
    }
    ++(observer_values.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_values, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
      __LINE__));
    ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
    --(observer_values.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_values, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    unknown_ptr = dynamic_cast<UnknownType*>(result_uptr.get());
    ASSERT_NE(unknown_ptr, nullptr);
    EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
      {local_socket, 0U}));
    EXPECT_EQ(unknown_ptr->Request().type, kBinaryRequest.type);
    EXPECT_EQ(unknown_ptr->Request().params_map.size(), 0U);
    EXPECT_EQ(unknown_ptr->Request().data.size(), 0U);
    EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);
  }

  // TEST CASE 4
  // Tests the SendBinaryManagementRequest move overload.
  std::vector<std::uint8_t> get_values_request_vector {get_values_sequence,
    get_values_sequence + get_values_binary_content_length};
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, FcgiType::kFCGI_GET_VALUES,
    std::move(get_values_request_vector))) << std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, request_map, response_map, __LINE__);
  // Unknown management request.
  std::vector<std::uint8_t> random_byte_sequence_copy {kBinaryRequest.data};
  auto GTestFatalSendAndRetrieveUnknown = [&](int invocation_line)
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalSendAndRetrieveUnknown"};
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
      local_socket, kBinaryRequest.type, std::move(random_byte_sequence_copy)))
        << std::strerror(errno));
    ++(observer_values.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_values, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
      __LINE__));
    ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
    --(observer_values.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_values, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
    unknown_ptr = dynamic_cast<UnknownType*>(result_uptr.get());
    ASSERT_NE(unknown_ptr, nullptr);
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalSendAndRetrieveUnknown(__LINE__));
  EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
    {local_socket, 0U}));
  EXPECT_EQ(unknown_ptr->Request(), kBinaryRequest);
  EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);
  // Empty vector.
  random_byte_sequence_copy.clear();
  ASSERT_NO_FATAL_FAILURE(GTestFatalSendAndRetrieveUnknown(__LINE__));
  EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
    {local_socket, 0U}));
  EXPECT_EQ(unknown_ptr->Request().type, kBinaryRequest.type);
  EXPECT_EQ(unknown_ptr->Request().params_map.size(), 0U);
  EXPECT_EQ(unknown_ptr->Request().data.size(), 0U);
  EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);

  // TEST CASE 5
  // Send two FCGI_GET_VALUES requests and two binary management requests.
  // Requests types are interleaved.
  // 1
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(local_socket,
    kMpxsMapWithValue)) << std::strerror(errno));
  // 2
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, FcgiType::kFCGI_GET_VALUES, get_values_sequence,
    get_values_sequence + get_values_binary_content_length)) <<
      std::strerror(errno));
  // 3
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues)) << std::strerror(errno));
  // 4
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, kBinaryRequest.type, kBinaryRequest.data.data(),
    kBinaryRequest.data.data() +  kBinaryRequest.data.size())) <<
      std::strerror(errno));
  observer_values.management_request_count += 4U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  // Allow the interface to process the requests.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  // Retrieve the responses and check for proper response ordering.
  // All management requests should have been processed by the server interface
  // when AcceptRequests was called in the loop above.
  // 1
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  observer_values.management_request_count = 0U;
  instance_observer.ready_event_count      = 3U;
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kMpxsNameMap, kMpxsMapWithValue, __LINE__));
  // 2
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(instance_observer.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, request_map, response_map, __LINE__));
  // 3
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(instance_observer.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
    local_socket, kNameOnlyMap, kMapWithValues, __LINE__));
  // 4
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  --(instance_observer.ready_event_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  unknown_ptr = dynamic_cast<UnknownType*>(result_uptr.get());
  ASSERT_NE(unknown_ptr, nullptr);
  EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
    {local_socket, 0U}));
  EXPECT_EQ(unknown_ptr->Request(), kBinaryRequest);
  EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);

  // TEST CASE 6
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
  // local_socket
  // 1
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, kBinaryRequest.type, kBinaryRequest.data.data(),
    kBinaryRequest.data.data() + kBinaryRequest.data.size())));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // 2
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues)) << std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // second_local_socket
  // 1
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
    second_local_socket, kMpxsMapWithValue)) << std::strerror(errno));
  ++(second_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // 2
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    second_local_socket, FcgiType::kFCGI_GET_VALUES, get_values_sequence,
    get_values_sequence + get_values_binary_content_length)));
  ++(second_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Allow the server to process the requests.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  int current_connection {};
  int local_socket_count {0};
  int second_local_socket_count {0};
  for(int number_remaining {4}; number_remaining > 0; --number_remaining)
  {
    ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(result_uptr.get(), nullptr);
    current_connection = result_uptr->RequestId().descriptor();
    if(current_connection == local_socket)
    {
      ++local_socket_count;
      if(local_socket_count == 1)
      {
        observer_values.management_request_count = 0U;
        ++(instance_observer.ready_event_count);
        unknown_ptr = dynamic_cast<UnknownType*>(result_uptr.get());
        ASSERT_NE(unknown_ptr, nullptr);
        EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
          {local_socket, 0U}));
        EXPECT_EQ(unknown_ptr->Request(), kBinaryRequest);
        EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);
      }
      else
      {
        --(instance_observer.ready_event_count);
        gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
        ASSERT_NE(gvr_ptr, nullptr);
        ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
          local_socket, kNameOnlyMap, kMapWithValues, __LINE__));
      }
    }
    else
    {
      ++second_local_socket_count;
      if(second_local_socket_count == 1)
      {
        second_observer.management_request_count = 0U;
        ++(instance_observer.ready_event_count);
        gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
        ASSERT_NE(gvr_ptr, nullptr);
        ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
          second_local_socket, kMpxsNameMap, kMpxsMapWithValue, __LINE__));
      }
      else
      {
        --(instance_observer.ready_event_count);
        gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
        ASSERT_NE(gvr_ptr, nullptr);
        ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
          second_local_socket, request_map, response_map, __LINE__));
      }
    }
    ASSERT_NO_FATAL_FAILURE(
      GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
        observer_values, __LINE__));
    ASSERT_NO_FATAL_FAILURE(
      GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
        second_observer, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
        instance_observer, __LINE__);
  }
  // Close second_local_socket to clean up state.
  EXPECT_TRUE(client_inter.CloseConnection(second_local_socket));
  second_observer.is_connected = false;
  --(instance_observer.connection_count);
  ASSERT_NO_FATAL_FAILURE(
    GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
      observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(
    GTestFatalClientInterfaceConnectionOnlyObserverCheck(client_inter,
      second_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);

  // TEST CASE 7
  int new_connection {};
  ASSERT_NO_THROW(new_connection = client_inter.Connect(kUnixPath2, 0U));
  ASSERT_NE(new_connection, -1) << std::strerror(errno);
  ++(instance_observer.connection_count);
  struct ClientInterfaceConnectionOnlyObserverValues new_observer
  {
    /* connection                         = */ new_connection,
    /* connection_completed_request_count = */ 0U,
    /* is_connected                       = */ true,
    /* management_request_count           = */ 0U,
    /* connection_pending_request_count   = */ 0U
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Exercise SendGetValuesRequest.
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(local_socket,
    kMapWithValues)) << std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(new_connection,
    kMpxsMapWithValue)) << std::strerror(errno));
  ++(new_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Allow the local server to process the request.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr.get(), nullptr);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  int retrieved_socket {gvr_ptr->RequestId().descriptor()};
  bool first_is_first {retrieved_socket == local_socket};
  auto GTestFatalCyclicCheck = [&](bool first, int invocation_line)
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalCyclicCheck"};
    if(first)
    {
      --(observer_values.management_request_count);
      ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
        local_socket, kNameOnlyMap, kMapWithValues, __LINE__));
    }
    else
    {
      --(new_observer.management_request_count);
      ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
        new_connection, kMpxsNameMap, kMpxsMapWithValue, __LINE__));
    }
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_values, __LINE__));
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, new_observer, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalCyclicCheck(first_is_first, __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr.get(), nullptr);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NO_FATAL_FAILURE(GTestFatalCyclicCheck(!first_is_first, __LINE__));
  // Exercise SendBinaryManagementRequest
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    local_socket, FcgiType::kFCGI_GET_VALUES, get_values_sequence,
    get_values_sequence + get_values_binary_content_length)) <<
      std::strerror(errno));
  ++(observer_values.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendBinaryManagementRequest(
    new_connection, kBinaryRequest.type, kBinaryRequest.data.data(),
    kBinaryRequest.data.data() + kBinaryRequest.data.size())) <<
      std::strerror(errno));
  ++(new_observer.management_request_count);
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, new_observer, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Allow the local server to process the request.
  ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr.get(), nullptr);
  int first_binary {result_uptr->RequestId().descriptor()};
  bool first_binary_is_first {first_binary == local_socket};
  auto GTestFatalUnknownTypeCheck = [&](int invocation_line)->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lamba GTestFatalUnknownTypeCheck"};
    unknown_ptr = dynamic_cast<UnknownType*>(result_uptr.get());
    ASSERT_NE(unknown_ptr, nullptr);
    EXPECT_EQ(unknown_ptr->RequestId(), (FcgiRequestIdentifier
      {new_connection, 0U}));
    EXPECT_EQ(unknown_ptr->Request(), kBinaryRequest);
    EXPECT_EQ(unknown_ptr->Type(), kBinaryRequest.type);
  };
  auto GTestFatalBinaryCyclicCheck = [&](bool first, int invocation_line)
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalBinaryCyclicCheck"};
    if(first)
    {
      --(observer_values.management_request_count);
      gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
      ASSERT_NE(gvr_ptr, nullptr);
      ASSERT_NO_FATAL_FAILURE(GTestFatalCheckGetValuesResult(gvr_ptr, false,
        local_socket, request_map, response_map, __LINE__));
    }
    else
    {
      --(new_observer.management_request_count);
      GTestFatalUnknownTypeCheck(__LINE__);
    }
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, observer_values, __LINE__));
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
      client_inter, new_observer, __LINE__));
    GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
      instance_observer, __LINE__);
  };
  ASSERT_NO_FATAL_FAILURE(GTestFatalBinaryCyclicCheck(first_binary_is_first,
    __LINE__));
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NE(result_uptr.get(), nullptr);
  ASSERT_NO_FATAL_FAILURE(GTestFatalBinaryCyclicCheck(!first_binary_is_first,
    __LINE__));
  // Close new_connection as it and the process that houses it are no longer
  // needed.
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
  // Terminate and reap the process which houses the second interface.
  ASSERT_NO_FATAL_FAILURE(GTestFatalTerminateChild(fork_return, __LINE__));

  // TEST CASE 8 (Failure case)
  // SendGetValuesRequest overloads.
  // Copy.
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendGetValuesRequest(-1,
    kNameOnlyMap)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendGetValuesRequest(1000,
    kNameOnlyMap)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Move.
  ParamsMap name_only_copy {kNameOnlyMap};
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendGetValuesRequest(-1,
    std::move(name_only_copy))));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  name_only_copy = kNameOnlyMap;
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendGetValuesRequest(1000,
    std::move(name_only_copy))));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // SendBinaryManagementRequest overloads
  // Copy
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendBinaryManagementRequest(-1,
   kBinaryRequest.type, kBinaryRequest.data.data(), kBinaryRequest.data.data()
    + kBinaryRequest.data.size())));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendBinaryManagementRequest(1000,
   kBinaryRequest.type, kBinaryRequest.data.data(), kBinaryRequest.data.data()
    + kBinaryRequest.data.size())));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Move
  random_byte_sequence_copy = kBinaryRequest.data;
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendBinaryManagementRequest(-1,
   kBinaryRequest.type, std::move(random_byte_sequence_copy))));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  random_byte_sequence_copy = kBinaryRequest.data;
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendBinaryManagementRequest(1000,
   kBinaryRequest.type, std::move(random_byte_sequence_copy))));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);

  // TEST CASE 9 (Failure case)
  // Check for a return of false when a call is made with a map that cannot
  // be encoded in a single record.
  // SendGetValuesRequest
  // Copy
  std::vector<std::uint8_t>
    large_name(kMaxRecordContentByteLength + 1, 1U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
    large_name_map {{{large_name}, {1U}}};
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendGetValuesRequest(local_socket,
    large_name_map)));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Move
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map))));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // SendBinaryManagementRequest
  // Copy
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendBinaryManagementRequest(
    local_socket, static_cast<FcgiType>(100U), large_name.data(),
    large_name.data() + large_name.size())));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);
  // Move
  ASSERT_NO_THROW(ASSERT_FALSE(client_inter.SendBinaryManagementRequest(
    local_socket, static_cast<FcgiType>(100U), std::move(large_name))));
  ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceConnectionOnlyObserverCheck(
    client_inter, observer_values, __LINE__));
  GTestNonFatalClientInterfaceInstanceObserverCheck(client_inter,
    instance_observer, __LINE__);

  // TEST CASE 10 (Failure case)
  delete(inter_uptr.release());
  using ServerCreationReturn = std::tuple<std::unique_ptr<FcgiServerInterface>,
    int, in_port_t>;

  auto GTestFatalCleanUpAndCreateInterface = [this, &inter_args]
  (
    ServerCreationReturn* new_interface_data_ptr,
    int invocation_line
  )->void
  {
    ::testing::ScopedTrace {__FILE__, invocation_line,
      "lambda GTestFatalCleanUpAndCreateInterface"};
    close(descriptor_resource_list_[0]);
    ASSERT_NE(unlink(kUnixPath1), -1) << std::strerror(errno);
    ServerCreationReturn local_interface_data {};
    ASSERT_NO_THROW(local_interface_data = GTestNonFatalCreateInterface(
      inter_args, __LINE__));
    ASSERT_NE(std::get<0>(local_interface_data).get(), nullptr);
    descriptor_resource_list_[0] = std::get<1>(local_interface_data);
    *new_interface_data_ptr = std::move(local_interface_data);
  };

  ASSERT_NO_THROW(ASSERT_TRUE(client_inter.CloseConnection(local_socket)));
  constexpr const int method_count {4};
  for(int i {0}; i != method_count; ++i)
  {
    ServerCreationReturn new_interface_return {};
    ASSERT_NO_FATAL_FAILURE(GTestFatalCleanUpAndCreateInterface(
      &new_interface_return, __LINE__));
    std::unique_ptr<FcgiServerInterface>& new_inter_uptr {std::get<0>(
      new_interface_return)};
    int new_connection {};
    ASSERT_NO_THROW(ASSERT_NE(new_connection = client_inter.Connect(kUnixPath1,
      0U), -1) << std::strerror(errno));
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
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      new_observer, __LINE__));
    // Allow the server interface to process the connection.
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(
      new_inter_uptr.get(), __LINE__));
    // Send an application and management request.
    FcgiRequestIdentifier id {};
    ASSERT_NO_THROW(id = client_inter.SendRequest(new_connection,
      kExerciseDataRef));
    ASSERT_NE(id, FcgiRequestIdentifier {});
    ++(new_observer.co.connection_pending_request_count);
    ++(new_observer.in.total_pending_request_count);
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
      new_connection, kSharedExerciseParams)));
    ++(new_observer.co.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      new_observer, __LINE__));
    // Destroy the server interface to cause connection closure.
    delete(new_inter_uptr.release());
    // Check that connection closure is registered by the overloads and verify
    // expected state changes.
    ASSERT_NO_THROW
    (
      switch(i) {
        case 0 : {
          EXPECT_FALSE(client_inter.SendGetValuesRequest(new_connection,
            kMapWithValues));
          break;
        }
        case 1 : {
          ParamsMap map_with_values_copy {kMapWithValues};
          EXPECT_FALSE(client_inter.SendGetValuesRequest(new_connection,
            std::move(map_with_values_copy)));
          break;
        }
        case 2 : {
          EXPECT_FALSE(client_inter.SendBinaryManagementRequest(new_connection, kBinaryRequest.type,
            kBinaryRequest.data.data(), kBinaryRequest.data.data() +
            kBinaryRequest.data.size()));
          break;
        }
        case 3 : {
          random_byte_sequence_copy = kBinaryRequest.data;
          EXPECT_FALSE(client_inter.SendBinaryManagementRequest(new_connection,
            kBinaryRequest.type, std::move(random_byte_sequence_copy)));
          break;
        }
      }
    );
    new_observer.co.connection_pending_request_count = 0U;
    new_observer.co.is_connected                     = false;
    new_observer.co.management_request_count         = 0U;
    new_observer.in.connection_count                 = 0U;
    new_observer.in.ready_event_count                = 1U;
    new_observer.in.total_pending_request_count      = 0U;
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      new_observer, __LINE__));
    ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(result_uptr.get(), nullptr);
    ConnectionClosure* closure_ptr {dynamic_cast<ConnectionClosure*>(
      result_uptr.get())};
    ASSERT_NE(closure_ptr, nullptr);
    EXPECT_EQ(closure_ptr->RequestId().descriptor(), new_connection);
    --(new_observer.in.ready_event_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(client_inter,
      new_observer, __LINE__));
  }
}

TEST_F(TestFcgiClientInterfaceTestFixture, ManagementRequestsTestCaseSet2)
{
  // TEST CASE 11
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  // Creates the server interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));
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

  // TEST CASE 12
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
  close(descriptor_resource_list_[0]);
  descriptor_resource_list_.erase(descriptor_resource_list_.begin());
  inter_args.unix_path = kUnixPath2;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  new_inter_return {};
  ASSERT_NO_THROW(new_inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& new_inter_uptr
    {std::get<0>(new_inter_return)};
  ASSERT_NE(new_inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
    std::get<1>(new_inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath2));
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

TEST_F(TestFcgiClientInterfaceTestFixture, ManagementRequestsTestCaseSet3)
{
  // TEST CASE 13
  // The connected descriptor of the interface is used to allow an erroneous
  // response to be sent to the client interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  // Creates the server interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));
  TestFcgiClientInterface client_inter {};
  constexpr const int case_count {3};
  for(int i {0}; i != case_count; ++i)
  {
    std::string trace_message {"Iteration count: "};
    std::string iteration_count {std::to_string(i)};
    trace_message.insert(trace_message.end(), iteration_count.begin(),
      iteration_count.end());
    ::testing::ScopedTrace tracer {__FILE__, __LINE__, trace_message};
    int connection {};
    ASSERT_NO_THROW(connection = client_inter.Connect(kUnixPath1, 0U));
    ASSERT_NE(connection, -1) << std::strerror(errno);
    struct ClientInterfaceObserverValues observer
    {
      /* co = */
      {
        /* connection                         = */ connection,
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
    // Allow the server to process the connection before sending a management
    // request.
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
      __LINE__));
    ASSERT_NO_THROW(ASSERT_TRUE(client_inter.SendGetValuesRequest(
      connection, kMapWithValues)) << std::strerror(errno));
    ++(observer.co.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
    switch(i) {
      case 0 : {
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
        ASSERT_EQ(socket_functions::SocketWrite(connection + 1,
          response_buffer, buffer_length), buffer_length);
        break;
      }
      case 1 : [[fallthrough]];
      case 2 : {
        using PairList = std::vector<std::pair<std::vector<std::uint8_t>,
          std::vector<std::uint8_t>>>;
        std::string max_conn_string {std::to_string(
          kDefaultInterfaceArguments.max_connections)};
        std::string max_req_string {std::to_string(
          kDefaultInterfaceArguments.max_requests)};
        PairList pair_vector_duplicate
        {
          {{FCGI_MAX_CONNS}, {max_conn_string.begin(), max_conn_string.end()}},
          {{FCGI_MAX_REQS}, {max_req_string.begin(), max_req_string.end()}},
          {{FCGI_MAX_CONNS}, {max_conn_string.begin(), max_conn_string.end()}},
          {{FCGI_MPXS_CONNS}, {'1'}}
        };
        PairList pair_vector_mismatch
        {
          {{FCGI_MAX_CONNS}, {max_conn_string.begin(), max_conn_string.end()}},
          {{FCGI_MAX_REQS}, {max_req_string.begin(), max_req_string.end()}},
          {{FCGI_MAX_CONNS}, {'2'}},
          {{FCGI_MPXS_CONNS}, {'1'}}
        };
        PairList& pair_list_ref {(i == 1) ? pair_vector_duplicate :
          pair_vector_mismatch};
        std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
          std::vector<std::uint8_t>, std::size_t, PairList::iterator>
        encode_return {EncodeNameValuePairs(pair_list_ref.begin(),
          pair_list_ref.end(), FcgiType::kFCGI_GET_VALUES_RESULT, 0U, 0U)};
        ASSERT_FALSE(EncodeNVPairSingleRecordFailure(encode_return,
          pair_list_ref.end()));
        std::vector<struct iovec>& iovec_list {std::get<2>(encode_return)};
        std::size_t total_write_length {std::get<1>(encode_return)};
        std::tuple<struct iovec*, int, std::size_t> gather_write_return
          {socket_functions::ScatterGatherSocketWrite(connection + 1,
            iovec_list.data(), iovec_list.size(), total_write_length)};
        ASSERT_EQ(std::get<2>(gather_write_return), 0U) <<
          std::strerror(errno);
        break;
      }
    }
    // Allow the client to process the response.
    std::unique_ptr<ServerEvent> response_uptr {};
    ASSERT_NO_THROW(response_uptr = client_inter.RetrieveServerEvent());
    ASSERT_NE(response_uptr.get(), nullptr);
    GetValuesResult* gvr_ptr {dynamic_cast<GetValuesResult*>(
      response_uptr.get())};
    ASSERT_NE(gvr_ptr, nullptr);
    --(observer.co.management_request_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
    GTestFatalCheckGetValuesResult(gvr_ptr, true,
      connection, kNameOnlyMap, ParamsMap {}, __LINE__);
    // Prepare to send a response which includes duplicate names. Two cases
    // may be distinguished: the duplicate names have the same value or they do
    // not.
    ASSERT_NO_THROW(EXPECT_TRUE(client_inter.CloseConnection(connection)));
    observer.co.is_connected = false;
    --(observer.in.connection_count);
    ASSERT_NO_FATAL_FAILURE(GTestFatalClientInterfaceObserverCheck(
      client_inter, observer, __LINE__));
    // Allow the server to process connection closure.
    ASSERT_NO_FATAL_FAILURE(GTestFatalAcceptRequestsExpectNone(inter_uptr.get(),
      __LINE__));
  }
}

TEST_F(TestFcgiClientInterfaceTestFixture, ManagementRequestsTestCaseSet4)
{
  // Creates the server interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  // Move the unique_ptr to allow later assignments.
  std::unique_ptr<FcgiServerInterface> inter_uptr
    {std::move(std::get<0>(inter_return))};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  // No-op for the closure_detector parameter. An implicit call to
  // RetrieveServerEvent will detect closure.
  auto CallSendBinaryManagementRequest = []
  (
    TestFcgiClientInterface* client_inter_ptr,
    FcgiRequestIdentifier pending_application_request
  )->void
  {
    ASSERT_NO_THROW(ASSERT_FALSE(client_inter_ptr->SendBinaryManagementRequest(
      pending_application_request.descriptor(), FcgiType::kFCGI_GET_VALUES,
      kBinaryRequest.data.data(),
      kBinaryRequest.data.data() + kBinaryRequest.data.size())));
  };
  DisconnectWithServerReturn disconnector
  {
    std::bind(GTestFatalServerDestructionClosureMeta,
    /* inter_args_ptr              */ &inter_args,
    /* server_uptr_ptr             */ &inter_uptr,
    /* client_inter_ptr            */ &client_inter,
    /* closure_detector            */  CallSendBinaryManagementRequest,
    /* descriptor_list_ptr         */ &descriptor_resource_list_,
    /* connection                  */  std::placeholders::_1,
    /* pending_application_request */  std::placeholders::_2,
    /* server_interface_ptr_ptr    */  std::placeholders::_3,
    /* invocation_line             */  std::placeholders::_4)
  };

  ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(GTestFatalConnectionClosureCheck(
    kUnixPath1, 0U, &client_inter, inter_uptr.get(), disconnector, __LINE__)));
}

TEST_F(TestFcgiClientInterfaceTestFixture, ManagementRequestsTestCaseSet5)
{
  // Creates the server interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  // Move the unique_ptr to allow later assignments.
  std::unique_ptr<FcgiServerInterface> inter_uptr
    {std::move(std::get<0>(inter_return))};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  // No-op for the closure_detector parameter. An implicit call to
  // RetrieveServerEvent will detect closure.
  auto CallSendGetValuesRequest = []
  (
    TestFcgiClientInterface* client_inter_ptr,
    FcgiRequestIdentifier pending_application_request
  )->void
  {
    ASSERT_NO_THROW(ASSERT_FALSE(client_inter_ptr->SendGetValuesRequest(
      pending_application_request.descriptor(), kMapWithValues)));
  };
  DisconnectWithServerReturn disconnector
  {
    std::bind(GTestFatalServerDestructionClosureMeta,
    /* inter_args_ptr              */ &inter_args,
    /* server_uptr_ptr             */ &inter_uptr,
    /* client_inter_ptr            */ &client_inter,
    /* closure_detector            */  CallSendGetValuesRequest,
    /* descriptor_list_ptr         */ &descriptor_resource_list_,
    /* connection                  */  std::placeholders::_1,
    /* pending_application_request */  std::placeholders::_2,
    /* server_interface_ptr_ptr    */  std::placeholders::_3,
    /* invocation_line             */  std::placeholders::_4)
  };

  ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(GTestFatalConnectionClosureCheck(
    kUnixPath1, 0U, &client_inter, inter_uptr.get(), disconnector, __LINE__)));
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
// SendRequestCaseSet4
// 7) The connection was found to be closed by GTestFatalConnectionClosureCheck
//    and GTestFatalServerDestructionClosureMeta with a call to SendRequest.
//
// Modules and features which testing depends on:
// 1) FcgiServerInterface
// 2) The immediate detection of connection closure by the implementation of
//    AF_UNIX sockets.
//
// Other modules whose testing depends on this module: none.

TEST_F(TestFcgiClientInterfaceTestFixture, SendRequestCaseSet1)
{
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(*request_array[i],
      response_ptr, __LINE__));
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
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
    ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(
      request_representation, response_ptr, __LINE__));
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
  // Creates the server interface.
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
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

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
  ASSERT_NO_FATAL_FAILURE(GTestFatalEchoResponseCompare(kExerciseDataRef,
    response_ptr, __LINE__));
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

TEST_F(TestFcgiClientInterfaceTestFixture, SendRequestCaseSet4)
{
  // Creates the server interface.
  struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
  inter_args.domain          = AF_UNIX;
  inter_args.unix_path       = kUnixPath1;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
    inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args, __LINE__));
  // Move the unique_ptr to allow later assignments.
  std::unique_ptr<FcgiServerInterface> inter_uptr
    {std::move(std::get<0>(inter_return))};
  ASSERT_NE(inter_uptr.get(), nullptr);
  ASSERT_NO_THROW(descriptor_resource_list_.push_back(
      std::get<1>(inter_return)));
  ASSERT_NO_THROW(path_resource_list_.push_back(kUnixPath1));

  TestFcgiClientInterface client_inter {};
  // No-op for the closure_detector parameter. An implicit call to
  // RetrieveServerEvent will detect closure.
  auto CallSendGetValuesRequest = []
  (
    TestFcgiClientInterface* client_inter_ptr,
    FcgiRequestIdentifier pending_application_request
  )->void
  {
    ASSERT_NO_THROW(ASSERT_FALSE(client_inter_ptr->SendRequest(
      pending_application_request.descriptor(), kExerciseDataRef)));
  };
  DisconnectWithServerReturn disconnector
  {
    std::bind(GTestFatalServerDestructionClosureMeta,
    /* inter_args_ptr              */ &inter_args,
    /* server_uptr_ptr             */ &inter_uptr,
    /* client_inter_ptr            */ &client_inter,
    /* closure_detector            */  CallSendGetValuesRequest,
    /* descriptor_list_ptr         */ &descriptor_resource_list_,
    /* connection                  */  std::placeholders::_1,
    /* pending_application_request */  std::placeholders::_2,
    /* server_interface_ptr_ptr    */  std::placeholders::_3,
    /* invocation_line             */  std::placeholders::_4)
  };

  ASSERT_NO_FATAL_FAILURE(ASSERT_NO_THROW(GTestFatalConnectionClosureCheck(
    kUnixPath1, 0U, &client_inter, inter_uptr.get(), disconnector, __LINE__)));
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components
