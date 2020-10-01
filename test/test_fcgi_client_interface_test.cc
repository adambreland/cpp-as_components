#include "test/test_fcgi_client_interface.h"

#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/protocol_constants.h"
#include "test/fcgi_si_testing_utilities.h"

namespace {
  std::atomic<bool> test_fcgi_client_interface_get_values_result_timeout
    {false};
  extern "C" void SigAlrmHandler(int sig)
  {
    test_fcgi_client_interface_get_values_result_timeout.store(true);
  }
} // namespace

class TestFcgiClientInterfaceGetValuesResult : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    fcgi_si_test::GTestFatalIgnoreSignal(SIGPIPE);
    fcgi_si_test::GTestFatalSetSignalDisposition(SIGALRM, &SigAlrmHandler);
    ASSERT_TRUE(test_fcgi_client_interface_get_values_result_timeout.
      is_lock_free());
  }

  void TearDown() override
  {
    if(listening_socket_ != -1)
    {
      close(listening_socket_);
    }
    fcgi_si_test::GTestFatalRestoreSignal(SIGALRM);
    fcgi_si_test::GTestFatalRestoreSignal(SIGPIPE);
    fcgi_si_test::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc_,
      "TestFcgiClientInterfaceGetValuesResult");
  }

  int listening_socket_ {-1};
  fcgi_si_test::FileDescriptorLeakChecker fdlc_ {};
};

TEST_F(TestFcgiClientInterfaceGetValuesResult, SendGetValuesRequest)
{
  {
    struct fcgi_si_test::InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 10;
    inter_args.max_requests    = 100;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    std::tuple<std::unique_ptr<fcgi_si::FcgiServerInterface>, int, in_port_t>
    inter_return {};
    ASSERT_NO_THROW(inter_return =
      fcgi_si_test::GTestNonFatalCreateInterface(inter_args));
    std::unique_ptr<fcgi_si::FcgiServerInterface>& inter_uptr
      {std::get<0>(inter_return)};
    ASSERT_NE(inter_uptr, std::unique_ptr<fcgi_si::FcgiServerInterface> {});
    listening_socket_ = std::get<1>(inter_return);
    fcgi_si_test::TestFcgiClientInterface client_inter {};
    int local_socket {};
    ASSERT_NO_THROW(local_socket =
      client_inter.Connect("127.0.0.1", std::get<2>(inter_return)));
    ASSERT_NE(local_socket, -1) << std::strerror(errno);
    EXPECT_EQ(client_inter.ConnectionCount(), 1);
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> params_map
    {
      {fcgi_si::FCGI_MAX_CONNS, {}},
      {fcgi_si::FCGI_MAX_REQS, {}},
      {fcgi_si::FCGI_MPXS_CONNS, {}}
    };
    bool send_gvr {false};
    ASSERT_NO_THROW(send_gvr =
      client_inter.SendGetValuesRequest(local_socket, params_map));
    ASSERT_TRUE(send_gvr) << std::strerror(errno);
    std::vector<fcgi_si::FcgiRequest> accept_result {};
    struct itimerval timeout
    {
      {0, 0},   // it_interval (don't repeat)
      {0, 2000} // it_value (wait 2 ms)
    };
    ASSERT_NE(setitimer(ITIMER_REAL, &timeout, nullptr), -1) <<
      std::strerror(errno);
    // The loop until AcceptRequests blocks was found to be needed as a single
    // call to AcceptRequests did not read all of the information of the
    // FCGI_GET_VALUES request. The timer set above and its associated SIGALRM
    // signal are used to break out of the blocked AcceptRequests call and the
    // loop.
    //
    // Note that this implementation also tests the specified behavior of
    // AcceptRequests when it has blocked in an I/O multiplexing call and the
    // call fails with errno == EINTR.
    while(!(test_fcgi_client_interface_get_values_result_timeout.load()))
    {
      ASSERT_NO_THROW(accept_result = inter_uptr->AcceptRequests());
      EXPECT_EQ(accept_result.size(), 0U);
    }
    std::unique_ptr<fcgi_si_test::ServerEvent> result_uptr {};
    ASSERT_NO_THROW(result_uptr = client_inter.RetrieveServerEvent());
    fcgi_si_test::GetValuesResult* gvr_ptr
      {dynamic_cast<fcgi_si_test::GetValuesResult*>(result_uptr.get())};
    ASSERT_NE(gvr_ptr, nullptr);
    EXPECT_EQ(params_map, gvr_ptr->RequestMap());
    params_map[fcgi_si::FCGI_MAX_CONNS] = std::vector<std::uint8_t> {'1', '0'};
    params_map[fcgi_si::FCGI_MAX_REQS] = std::vector<std::uint8_t>
      {'1', '0', '0'};
    params_map[fcgi_si::FCGI_MPXS_CONNS] = std::vector<std::uint8_t> {'1'};
    EXPECT_EQ(params_map, gvr_ptr->ResponseMap());
    bool close_connection_return {false};
    ASSERT_NO_THROW(close_connection_return =
      client_inter.CloseConnection(local_socket));
    ASSERT_TRUE(close_connection_return) << std::strerror(errno);
    EXPECT_EQ(client_inter.ConnectionCount(), 0);
  }
}
