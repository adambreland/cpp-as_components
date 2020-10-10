#include "include/test_fcgi_client_interface.h"

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

#include "external/fcgi_si/include/protocol_constants.h"
#include "external/fcgi_si/test/fcgi_si_testing_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"

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
    fcgi_si_test::GTestFatalIgnoreSignal(SIGPIPE);
    fcgi_si_test::GTestFatalSetSignalDisposition(SIGALRM, &SigAlrmHandler);
    ASSERT_TRUE(test_fcgi_client_interface_fcgi_server_accept_timeout.
      is_lock_free());
    // Ensure that the the timeout flag is cleared.
    test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  }

  void TearDown() override
  {
    if(listening_socket_ != -1)
    {
      close(listening_socket_);
      EXPECT_NE(unlink(unix_path_), -1) << std::strerror(errno);
    }
    // Clear the timeout flag to reset shared state.
    test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
    fcgi_si_test::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc_,
      "TestFcgiClientInterfaceGetValuesResult");
    fcgi_si_test::GTestFatalRestoreSignal(SIGALRM);
    fcgi_si_test::GTestFatalRestoreSignal(SIGPIPE);
  }

  // AF_UNIX files cannot be created in the Bazel temporary file directory
  // because its name is too long.
  const char* unix_path_ {"/tmp/TestFcgiClientInterfaceManagementRequests"};
  int listening_socket_ {-1};
  fcgi_si_test::FileDescriptorLeakChecker fdlc_ {};
};




TEST_F(TestFcgiClientInterfaceManagementRequests, SendBinaryManagementRequest)
{
  // Testing explanation
  // This test examines the behavior of TestFcgiClientInterface when
  // SendBinaryManagementRequest is called on a new interface instance. The
  // copy and move overloads are tested. Each of the three specified scenarios
  // which cause false to be returned is tested.
  //
  // Examined properties:
  //
 
  // 
  //
  // Modules which testing depends on:
  // 1) Receipt of and response to FCGI_GET_VALUES requests and unknown
  //    requests by fcgi_si::FcgiServerInterface.
  // 2) The immediate detection of peer closure by the implementation of
  //    local (AF_UNIX) sockets.
  //
  // Other modules whose testing depends on this module: none.
  struct fcgi_si_test::InterfaceCreationArguments inter_args {};
  inter_args.domain          = AF_UNIX;
  inter_args.backlog         = 5;
  inter_args.max_connections = 10;
  inter_args.max_requests    = 100;
  inter_args.app_status      = EXIT_FAILURE;
  inter_args.unix_path       = nullptr;
  inter_args.unix_path       = unix_path_;

  std::tuple<std::unique_ptr<fcgi_si::FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    fcgi_si_test::GTestNonFatalCreateInterface(inter_args));
  std::unique_ptr<fcgi_si::FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  listening_socket_ = std::get<1>(inter_return);

  // Create an interface and check initial state.
  a_component::fcgi::TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.ConnectionCount(), 0);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  int local_socket {};

  // Test the copy overload first.
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(unix_path_, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  EXPECT_EQ(client_inter.ConnectionCount(), 1);
  std::size_t management_count {0U};
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> params_map
  {
    {fcgi_si::FCGI_MAX_CONNS, {}},
    {fcgi_si::FCGI_MAX_REQS, {}},
    {fcgi_si::FCGI_MPXS_CONNS, {}}
  };
  // Encode the map ma
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, params_map));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  params_map[fcgi_si::FCGI_MAX_CONNS].clear();
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
  name_only_copy {params_map};
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  std::vector<fcgi_si::FcgiRequest> accept_result {};
  struct itimerval timeout
  {
    {0, 0},   // it_interval (don't repeat)
    {0, 2000} // it_value (wait 2 ms)
  };
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  // The loop until AcceptRequests blocks was found to be needed as a single
  // call to AcceptRequests does not necessarily read all of the information
  // of the FCGI_GET_VALUES request. The timer set above and its associated
  // SIGALRM signal are used to break out of the blocked AcceptRequests call
  // and the loop.
  //
  // Note that this implementation also tests the specified behavior of
  // AcceptRequests when it has blocked in an I/O multiplexing call and the
  // call fails with errno == EINTR.

  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  std::unique_ptr<a_component::fcgi::ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  a_component::fcgi::GetValuesResult* gvr_ptr
    {dynamic_cast<a_component::fcgi::GetValuesResult*>(result_uptr.get())};
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(params_map, gvr_ptr->RequestMap());
  params_map[fcgi_si::FCGI_MAX_CONNS] = std::vector<std::uint8_t> {'1', '0'};
  params_map[fcgi_si::FCGI_MAX_REQS] = std::vector<std::uint8_t>
    {'1', '0', '0'};
  params_map[fcgi_si::FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};
  EXPECT_EQ(params_map, gvr_ptr->ResponseMap());

  // Start testing the move overload.
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> result_copy
    {params_map};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, std::move(params_map)));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  // Clear the timeout flag.
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  gvr_ptr = dynamic_cast<a_component::fcgi::GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(name_only_copy, gvr_ptr->RequestMap());
  EXPECT_EQ(result_copy, gvr_ptr->ResponseMap());

  // Check for a return of false when a call is made for a non-existent
  // connection.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    name_only_copy));
  EXPECT_FALSE(send_gvr);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);

  // Check for a return of false when a call is made with a map that cannot
  // be encoded in a single record.
  std::vector<std::uint8_t>
    large_name(fcgi_si::kMaxRecordContentByteLength + 1, 1U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
    large_name_map {{std::move(large_name), {1U}}};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);

  // Check for a return of false when a call is made and it is detected that
  // the peer closed the connection.

  // Destroy the FcgiServerInterface instance to cause the peer connection to
  // be closed.
  delete(inter_uptr.release());
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    name_only_copy));
  EXPECT_FALSE(send_gvr);
  EXPECT_EQ(client_inter.ManagementRequestCount(local_socket), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(), 0);
  ASSERT_EQ(client_inter.ReadyEventCount(), 1U);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  a_component::fcgi::ConnectionClosure* closure_ptr
    {dynamic_cast<a_component::fcgi::ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
}

TEST_F(TestFcgiClientInterfaceManagementRequests, SendGetValuesRequest)
{
  // Discussion
  // 1) Management requests and responses each use a single FastCGI record.
  //    Because of this, tests which examine correct interface behavior when
  //    responses are received over multiple records are not relevant.
  //
  // Test cases:
  // Tests for which calls to SendGetValueRequest return true.
  // 1) A single request is made with the copy overload and the correct
  //    response is returned.
  // 2) As 1, but the move overload is used.
  // 3) To ensure that the client interface correctly handles request order,
  //    two distinct requests are made in series. Then the server interface
  //    is allowed to process the requests. It is verified that the client
  //    interface returns the correct responses in the order in which the
  //    requests were enqueued.
  // 4) To ensure that the client interface correctly handles connection
  //    closure by the client interface user in the case that FastCGI
  //    identifier values are allocated for a connection, an FCGI_GET_VALUES
  //    request is made when a pending application request is present. Then
  //    the connection is closed by the user. A new connection with the same
  //    descriptor value is made. Then a management request is made which
  //    should have a response which is distinct from the response that would
  //    have been returned for the previous request. It is verified that the
  //    correct response it returned.
  //
  // Tests for which calls to SendGetValueRequest return false.
  // 
  //












  // Testing explanation
  // This test examines the behavior of TestFcgiClientInterface when
  // SendGetValuesRequest is called on a new interface instance. The copy and
  // move overloads are tested. Each of the three specified scenarios
  // which cause false to be returned is tested.
  //
  // Modules which testing depends on:
  // 1) Receipt of and response to FCGI_GET_VALUES requests by
  //    fcgi_si::FcgiServerInterface.
  // 2) The immediate detection of peer closure by the implementation of
  //    local (AF_UNIX) sockets.
  //
  // Other modules whose testing depends on this module: none.
  struct fcgi_si_test::InterfaceCreationArguments inter_args {};
  inter_args.domain          = AF_UNIX;
  inter_args.backlog         = 5;
  inter_args.max_connections = 10;
  inter_args.max_requests    = 100;
  inter_args.app_status      = EXIT_FAILURE;
  inter_args.unix_path       = nullptr;
  inter_args.unix_path       = unix_path_;

  // Create a server interface to respond to FCGI_GET_VALUES requests sent
  // by a client interface.
  std::tuple<std::unique_ptr<fcgi_si::FcgiServerInterface>, int, in_port_t>
  inter_return {};
  ASSERT_NO_THROW(inter_return =
    fcgi_si_test::GTestNonFatalCreateInterface(inter_args));
  std::unique_ptr<fcgi_si::FcgiServerInterface>& inter_uptr
    {std::get<0>(inter_return)};
  ASSERT_NE(inter_uptr.get(), nullptr);
  listening_socket_ = std::get<1>(inter_return);

  // Create an interface and check initial state.
  a_component::fcgi::TestFcgiClientInterface client_inter {};
  EXPECT_EQ(client_inter.ConnectionCount(), 0);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  int local_socket {};

  // Test the copy overload first.
  ASSERT_NO_THROW(local_socket =
    client_inter.Connect(unix_path_, std::get<2>(inter_return)));
  ASSERT_NE(local_socket, -1) << std::strerror(errno);
  EXPECT_EQ(client_inter.ConnectionCount(), 1);
  std::size_t management_count {0U};
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> params_map
  {
    {fcgi_si::FCGI_MAX_CONNS, {10U}},
    {fcgi_si::FCGI_MAX_REQS, {}},
    {fcgi_si::FCGI_MPXS_CONNS, {}}
  };
  bool send_gvr {false};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, params_map));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  params_map[fcgi_si::FCGI_MAX_CONNS].clear();
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
  name_only_copy {params_map};
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  std::vector<fcgi_si::FcgiRequest> accept_result {};
  struct itimerval timeout
  {
    {0, 0},   // it_interval (don't repeat)
    {0, 2000} // it_value (wait 2 ms)
  };
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  // The loop until AcceptRequests blocks was found to be needed as a single
  // call to AcceptRequests does not necessarily read all of the information
  // of the FCGI_GET_VALUES request. The timer set above and its associated
  // SIGALRM signal are used to break out of the blocked AcceptRequests call
  // and the loop.
  //
  // Note that this implementation also tests the specified behavior of
  // AcceptRequests when it has blocked in an I/O multiplexing call and the
  // call fails with errno == EINTR.

  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  std::unique_ptr<a_component::fcgi::ServerEvent> result_uptr {};
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  a_component::fcgi::GetValuesResult* gvr_ptr
    {dynamic_cast<a_component::fcgi::GetValuesResult*>(result_uptr.get())};
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(params_map, gvr_ptr->RequestMap());
  params_map[fcgi_si::FCGI_MAX_CONNS] = std::vector<std::uint8_t> {'1', '0'};
  params_map[fcgi_si::FCGI_MAX_REQS] = std::vector<std::uint8_t>
    {'1', '0', '0'};
  params_map[fcgi_si::FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};
  EXPECT_EQ(params_map, gvr_ptr->ResponseMap());

  // Start testing the move overload.
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> result_copy
    {params_map};
  ASSERT_NO_THROW(send_gvr =
    client_inter.SendGetValuesRequest(local_socket, std::move(params_map)));
  ASSERT_TRUE(send_gvr) << std::strerror(errno);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 1U);
  ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
    std::strerror(errno);
  // Clear the timeout flag.
  test_fcgi_client_interface_fcgi_server_accept_timeout.store(false);
  while(!(test_fcgi_client_interface_fcgi_server_accept_timeout.load()))
  {
    ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
    EXPECT_EQ(accept_result.size(), 0U);
  }
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);
  gvr_ptr = dynamic_cast<a_component::fcgi::GetValuesResult*>(result_uptr.get());
  ASSERT_NE(gvr_ptr, nullptr);
  EXPECT_EQ(name_only_copy, gvr_ptr->RequestMap());
  EXPECT_EQ(result_copy, gvr_ptr->ResponseMap());

  // Check for a return of false when a call is made for a non-existent
  // connection.
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(1000,
    name_only_copy));
  EXPECT_FALSE(send_gvr);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);

  // Check for a return of false when a call is made with a map that cannot
  // be encoded in a single record.
  std::vector<std::uint8_t>
    large_name(fcgi_si::kMaxRecordContentByteLength + 1, 1U);
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
    large_name_map {{std::move(large_name), {1U}}};
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    std::move(large_name_map)));
  EXPECT_FALSE(send_gvr);
  ASSERT_NO_THROW(management_count =
    client_inter.ManagementRequestCount(local_socket));
  EXPECT_EQ(management_count, 0U);

  // Check for a return of false when a call is made and it is detected that
  // the peer closed the connection.

  // Destroy the FcgiServerInterface instance to cause the peer connection to
  // be closed.
  delete(inter_uptr.release());
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
  ASSERT_NO_THROW(send_gvr = client_inter.SendGetValuesRequest(local_socket,
    name_only_copy));
  EXPECT_FALSE(send_gvr);
  EXPECT_EQ(client_inter.ManagementRequestCount(local_socket), 0U);
  EXPECT_EQ(client_inter.ConnectionCount(), 0);
  ASSERT_EQ(client_inter.ReadyEventCount(), 1U);
  ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
  a_component::fcgi::ConnectionClosure* closure_ptr
    {dynamic_cast<a_component::fcgi::ConnectionClosure*>(result_uptr.get())};
  ASSERT_NE(closure_ptr, nullptr);
  EXPECT_EQ(closure_ptr->RequestId().descriptor(), local_socket);
  EXPECT_EQ(client_inter.ReadyEventCount(), 0U);
}
