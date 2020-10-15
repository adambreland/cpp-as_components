#include "test/include/test_fcgi_client_interface.h"

#include <arpa/inet.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
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

// A signal handler and an associated atomic boolean for testing
// fcgi_si_test::TestFcgiClientInterface::SendGetValuesRequest.
namespace {
  std::atomic<bool> test_fcgi_client_interface_fcgi_server_accept_timeout
    {false};
  extern "C" void SigAlrmHandler(int sig)
  {
    test_fcgi_client_interface_fcgi_server_accept_timeout.store(true);
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
      "TestFcgiClientInterfaceGetValuesResult");
    testing::gtest::GTestFatalRestoreSignal(SIGALRM);
    testing::gtest::GTestFatalRestoreSignal(SIGPIPE);
  }

  std::vector<std::pair<int, const char*>> resource_list_ {};
  testing::FileDescriptorLeakChecker fdlc_ {};
};


// TEST_F(TestFcgiClientInterfaceManagementRequests, SendBinaryManagementRequest)
// {
//   // Testing explanation
//   // This test examines the behavior of TestFcgiClientInterface when
//   // SendBinaryManagementRequest is called on a new interface instance. The
//   // copy and move overloads are tested. Each of the three specified scenarios
//   // which cause false to be returned is tested.
//   //
//   // Examined properties:
//   //
 
//   // 
//   //
//   // Modules which testing depends on:
//   // 1) Receipt of and response to FCGI_GET_VALUES requests and unknown
//   //    requests by fcgi_si::FcgiServerInterface.
//   // 2) The immediate detection of peer closure by the implementation of
//   //    local (AF_UNIX) sockets.
//   //
//   // Other modules whose testing depends on this module: none.
//   struct InterfaceCreationArguments inter_args {};
//   inter_args.domain          = AF_UNIX;
//   inter_args.backlog         = 5;
//   inter_args.max_connections = 10;
//   inter_args.max_requests    = 100;
//   inter_args.app_status      = EXIT_FAILURE;
//   inter_args.unix_path       = nullptr;
//   inter_args.unix_path       = unix_path_;

//   std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
//   inter_return {};
//   ASSERT_NO_THROW(inter_return =
//     GTestNonFatalCreateInterface(inter_args));
//   std::unique_ptr<FcgiServerInterface>& inter_uptr
//     {std::get<0>(inter_return)};
//   ASSERT_NE(inter_uptr.get(), nullptr);
//   listening_socket_ = std::get<1>(inter_return);

//   // Create an interface and check initial state.
//   TestFcgiClientInterface client_inter {};
//   EXPECT_EQ(client_inter.ConnectionCount(), 0);
//   EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
//   int local_socket {};

//   // Test the copy overload first.
//   ASSERT_NO_THROW(local_socket =
//     client_inter.Connect(unix_path_, std::get<2>(inter_return)));
//   ASSERT_NE(local_socket, -1) << std::strerror(errno);
//   EXPECT_EQ(client_inter.ConnectionCount(), 1);
//   std::size_t management_count {0U};
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 0U);
//   std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> params_map
//   {
//     {FCGI_MAX_CONNS, {}},
//     {FCGI_MAX_REQS, {}},
//     {FCGI_MPXS_CONNS, {}}
//   };
//   // Encode the map ma
//   bool send_gvr {false};
//   ASSERT_NO_THROW(send_gvr =
//     client_inter.SendGetValuesRequest(local_socket, params_map));
//   ASSERT_TRUE(send_gvr) << std::strerror(errno);
//   params_map[FCGI_MAX_CONNS].clear();
//   std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
//   name_only_copy {params_map};
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 1U);
//   std::vector<FcgiRequest> accept_result {};
//   struct itimerval timeout
//   {
//     {0, 0},   // it_interval (don't repeat)
//     {0, 2000} // it_value (wait 2 ms)
//   };
//   ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
//     std::strerror(errno);
//   // The loop until AcceptRequests blocks was found to be needed as a single
//   // call to AcceptRequests does not necessarily read all of the information
//   // of the FCGI_GET_VALUES request. The timer set above and its associated
//   // SIGALRM signal are used to break out of the blocked AcceptRequests call
//   // and the loop.
//   //
//   // Note that this implementation also tests the specified behavior of
//   // AcceptRequests when it has blocked in an I/O multiplexing call and the
//   // call fails with errno == EINTR.

//   while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
//   {
//     ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
//     EXPECT_EQ(accept_result.size(), 0U);
//   }
//   std::unique_ptr<ServerEvent> result_uptr {};
//   ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 0U);
//   GetValuesResult* gvr_ptr
//     {dynamic_cast<GetValuesResult*>(result_uptr.get())};
//   ASSERT_NE(gvr_ptr, nullptr);
//   EXPECT_EQ(params_map, gvr_ptr->RequestMap());
//   params_map[FCGI_MAX_CONNS] = std::vector<std::uint8_t> {'1', '0'};
//   params_map[FCGI_MAX_REQS] = std::vector<std::uint8_t>
//     {'1', '0', '0'};
//   params_map[FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};
//   EXPECT_EQ(params_map, gvr_ptr->ResponseMap());

//   // Start testing the move overload.
//   std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> result_copy
//     {params_map};
//   ASSERT_NO_THROW(send_gvr =
//     client_inter.SendGetValuesRequest(local_socket, std::move(params_map)));
//   ASSERT_TRUE(send_gvr) << std::strerror(errno);
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 1U);
//   ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
//     std::strerror(errno);
//   // Clear the timeout flag.
//   test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
//   while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
//   {
//     ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
//     EXPECT_EQ(accept_result.size(), 0U);
//   }
//   ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 0U);
//   gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
//   ASSERT_NE(gvr_ptr, nullptr);
//   EXPECT_EQ(name_only_copy, gvr_ptr->RequestMap());
//   EXPECT_EQ(result_copy, gvr_ptr->ResponseMap());

//   // Check for a return of false when a call is made for a non-existent
//   // connection.
//   ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
//     name_only_copy));
//   EXPECT_FALSE(send_gvr);
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 0U);

//   // Check for a return of false when a call is made with a map that cannot
//   // be encoded in a single record.
//   std::vector<std::uint8_t>
//     large_name(kMaxRecordContentByteLength + 1, 1U);
//   std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
//     large_name_map {{std::move(large_name), {1U}}};
//   ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
//     std::move(large_name_map)));
//   EXPECT_FALSE(send_gvr);
//   ASSERT_NO_THROW(management_count =
//     client_inter.ManagementRequestCount(local_socket));
//   EXPECT_EQ(management_count, 0U);

//   // Check for a return of false when a call is made and it is detected that
//   // the peer closed the connection.

//   // Destroy the FcgiServerInterface instance to cause the peer connection to
//   // be closed.
//   delete(inter_uptr.release());
//   EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
//   ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
//     name_only_copy));
//   EXPECT_FALSE(send_gvr);
//   EXPECT_EQ(client_inter.ManagementRequestCount(local_socket), 0U);
//   EXPECT_EQ(client_inter.ConnectionCount(), 0);
//   ASSERT_EQ(client_inter.ReadyEventCount(), 1U);
//   ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
//   ConnectionClosure* closure_ptr
//     {dynamic_cast<ConnectionClosure*>(result_uptr.get())};
//   ASSERT_NE(closure_ptr, nullptr);
//   EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
//   EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
// }

namespace {
  // AF_UNIX files cannot be created in the Bazel temporary file directory
  // because its name is too long.
  const char* unix_path_1 {"/tmp/TestFcgiClientInterfaceManagementRequests1"};
  const char* unix_path_2 {"/tmp/TestFcgiClientInterfaceManagementRequests2"};
} // namespace

// SendGetValuesRequest
// Discussion
// 1)    Management requests and responses each use a single FastCGI record.
//    Because of this, tests which examine correct interface behavior when
//    a response is received over multiple records are not relevant.
//       This behavior would have been relevant if multiple records may be
//    used in a response as the proper handling of FCGI_GET_VALUES_RESULT
//    records is indirectly tested here.
// 2) Behavior which may depend on socket domain is not tested here.
//
// Test cases:
// Test case set 1:
// 1) A single request is made with the copy overload and the correct
//    response is returned. To test the specified behavior for non-empty
//    input map values, such a value is present in the input map.
// 2) As 1, but the move overload is used.
// 3) To ensure that the client interface correctly handles request order,
//    two distinct requests are made in series of the same server interface.
//    The server interface is then allowed to process the requests. It is
//    verified that the client interface returns the correct responses in the
//    order in which the requests were enqueued.
// 4) The client interface must correctly handle the presence of multiple
//    connections to the same server. Two connections are made to the same
//    server. Distinct requests are sent to each connection. The appropriate
//    responses are verified.
// 5) (Failure case) 
// 6) (Failure case)
// 7) (Failure case)
//
// Test case set 2:
// 8) The client interface must correctly handle the presence of multiple,
//    distinct server interfaces. Two server interfaces are created and each
//    is sent a request with distinct content. The appropriate responses are
//    verified.
//
// Test case set 3:
// 9) To ensure that the client interface correctly handles connection
//    closure by the client interface user in the case that FastCGI
//    identifier values are allocated for a connection, an FCGI_GET_VALUES
//    request is made when a pending application request is present. Then
//    the connection is closed by the user. A new connection with the same
//    descriptor value is made. Then a management request is made which
//    should have a response which is distinct from the response that would
//    have been returned for the previous request. It is verified that the
//    correct response it returned.
//
// Modules which testing depends on:
// 1) Receipt of and response to FCGI_GET_VALUES requests by
//    fcgi_si::FcgiServerInterface.
// 2) The immediate detection of peer closure by the implementation of
//    local (AF_UNIX) sockets. (The Linux internet socket implementations do
//    not currently (202010) possess this property.)
//
// Other modules whose testing depends on this module: none.

// SendGetValuesRequest: Test case set 1.
TEST_F(TestFcgiClientInterfaceManagementRequests,
  SendGetValuesRequestTestCaseSet1)
{
  struct InterfaceCreationArguments inter_args {};
  inter_args.domain          = AF_UNIX;
  inter_args.backlog         = 5;
  inter_args.max_connections = 10;
  inter_args.max_requests    = 100;
  inter_args.app_status      = EXIT_FAILURE;
  inter_args.unix_path       = unix_path_1;

  // Create a server interface to respond to FCGI_GET_VALUES requests sent
  // by a client interface.
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    GTestNonFatalCreateInterface(inter_args));
  std::unique_ptr<FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  resource_list_.push_back({std::get<1>(inter_return), unix_path_1});

  // Create an interface and check initial state.
  TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.ConnectionCount(), 0);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  int local_socket {};

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

  // TEST CASE 1
  // Test the copy overload first.
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(unix_path_1, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  EXPECT_EQ(client_inter.ConnectionCount(), 1);
  std::size_t management_count {0U};
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  std::vector<FcgiRequest> accept_result {};
  struct itimerval timeout
  {
    {0, 0},   // it_interval (don't repeat)
    {0, 2000} // it_value (wait 2 ms)
  };
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  //    The loop until AcceptRequests blocks was found to be needed as a single
  // call to AcceptRequests does not necessarily read all of the information
  // of the FCGI_GET_VALUES request. The timer set above and its associated
  // SIGALRM signal are used to break out of the blocked AcceptRequests call
  // and the loop.
  //    Note that this implementation also tests the specified behavior of
  // AcceptRequests when it has blocked in an I/O multiplexing call and the
  // call fails with errno == EINTR.
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  std::unique_ptr<ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  GetValuesResult* gvr_ptr
    {dynamic_cast<GetValuesResult*>(result_uptr.get())};
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(name_only_map, gvr_ptr->RequestMap());
  EXPECT_EQ(map_with_values, gvr_ptr->ResponseMap());

  // TEST CASE 2
  // Start testing the move overload.
  ParamsMap value_map_copy {map_with_values};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, std::move(value_map_copy)));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  // Clear the timeout flag.
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(name_only_map, gvr_ptr->RequestMap());
  EXPECT_EQ(map_with_values, gvr_ptr->ResponseMap());

  // TEST CASE 3
  ParamsMap mpxs_name_map
  {
    {FCGI_MPXS_CONNS, {}}
  };
  ParamsMap mpxs_map_with_value {mpxs_name_map};
  mpxs_map_with_value[FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};
  // Send two requests.
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, mpxs_map_with_value));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, map_with_values));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 2U);
  // Allow the interface to process the requests.
  // Clear the timeout flag.
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  // Retrieve the responses and check for proper response ordering.
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  EXPECT_EQ(client_inter.ReadyEventCount(), 1U);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(mpxs_name_map, gvr_ptr->RequestMap());
  EXPECT_EQ(mpxs_map_with_value, gvr_ptr->ResponseMap());
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  gvr_ptr = dynamic_cast<GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(name_only_map, gvr_ptr->RequestMap());
  EXPECT_EQ(map_with_values, gvr_ptr->ResponseMap());

  // TEST CASE 4





















  // TEST CASE 5
  // Check for a return of false when a call is made for a non-existent
  // connection.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    name_only_map));
  EXPECT_FALSE(send_gvr);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);

  // TEST CASE 6
  // Check for a return of false when a call is made with a map that cannot
  // be encoded in a single record.
  std::vector<std::uint8_t>
    large_name(kMaxRecordContentByteLength + 1, 1U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
    large_name_map {{std::move(large_name), {1U}}};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);

  // TEST CASE 7
  //    Check for a return of false when a call is made and it is detected that
  // the peer closed the connection.
  //    Destroy the FcgiServerInterface instance to cause the peer connection
  // to be closed.
  delete(inter_uptr.release());
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    name_only_map));
  EXPECT_FALSE(send_gvr);
  EXPECT_EQ(client_inter.ManagementRequestCount(local_socket), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(), 0);
  ASSERT_EQ(client_inter.ReadyEventCount(), 1U);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ConnectionClosure* closure_ptr
    {dynamic_cast<ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
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
