#include "test/include/test_fcgi_client_interface.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "external/googletest/googletest/include/gtest/gtest.h"
#include "external/socket_functions/include/socket_functions.h"

#include "test/include/fcgi_si_testing_utilities.h"
#include "test/test/include/client_interface_testing_utilities.h"
#include "test/test/include/shared_interpositioning_state.h"

namespace a_component {
namespace fcgi {
namespace test {
namespace test {

TEST_F(TestFcgiClientInterfaceTestFixture, ConnectCase2)
{
  constexpr const int kChildCount {2};
  constexpr const char* kInternetAddresses[kChildCount] = {"127.0.0.1", "::1"};
  pid_t child_id_array[kChildCount] = {};
  int pipe_return_array[kChildCount][2] = {};
  TestFcgiClientInterface client_inter {};

  // A lambda function to close all of the pipe descriptors.
  auto GTestFatalPipeClosureOnError = [&pipe_return_array]
  (
    int         count,
    std::string message,
    int         invocation_line
  )->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "GTestFatalPipeClosure"};
    for(int i {0}; i != count; ++i)
    {
      close(pipe_return_array[i][0]);
      close(pipe_return_array[i][1]);
    }
    FAIL() << message;
  };

  // Create the pipes.
  for(int i {0}; i != kChildCount; ++i)
  {
    if(pipe(pipe_return_array[i]) == -1)
    {
      std::string error_message {"Iteration count: "};
      error_message.append(std::to_string(i)).append(1, '\n');
      error_message.append(std::strerror(errno));
      // This assert functions as a return as a fatal failure always occurs.
      ASSERT_NO_FATAL_FAILURE(GTestFatalPipeClosureOnError(i,
        error_message, __LINE__));
    }
  }
  // Fork the child processes.
  for(int i {0}; i != kChildCount; ++i)
  {
    child_id_array[i] = fork();
    if(child_id_array[i] == -1) // In parent and an error occured.
    {
      int saved_errno {errno};
      // Terminate any children created so far.
      for(int j {0}; j != i; ++j)
      {
        EXPECT_NO_FATAL_FAILURE(GTestFatalTerminateChild(child_id_array[j],
          __LINE__)) << "Iteration count: " << j;
      }
      // Cleanup the pipes.
      GTestFatalPipeClosureOnError(kChildCount, "Cleanup after fork error.",
        __LINE__);
      // Return from the current test.
      FAIL() << "Fork error: " << std::strerror(saved_errno);
    }
    else if(child_id_array[i] == 0) // In child.
    {
      ChildServerAlrmRestoreAndSelfKillSet();
      // Create a server interface.
      struct InterfaceCreationArguments inter_args {kDefaultInterfaceArguments};
      inter_args.domain = ((i == 0) ? AF_INET : AF_INET6);
      std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
      inter_tuple {GTestNonFatalCreateInterface(inter_args, __LINE__)};
      std::unique_ptr<FcgiServerInterface>& server_interface_uptr
        {std::get<0>(inter_tuple)};
      if(server_interface_uptr.get() == nullptr)
      {
        _exit(EXIT_FAILURE);
      }
      //    Write the port back to the client server. The port is in network
      // byte order and can be byte serialized directly.
      //    This action synchronizes the parent and child processes so that the
      // parent will not send SIGUSR1 until after it has received the port of
      // the server interface of the child process.
      //    Close the descriptors for the read ends of the pipes.
      for(int j {0}; j != kChildCount; ++j)
      {
        EXPECT_NE(close(pipe_return_array[j][0]), -1) << std::strerror(errno);
      }
      if(socket_functions::SocketWrite(pipe_return_array[i][1],
        static_cast<std::uint8_t*>(static_cast<void*>(
          &std::get<2>(inter_tuple))), sizeof(in_port_t)) !=
        sizeof(in_port_t))
      {
        _exit(EXIT_FAILURE);
      }
      std::vector<FcgiRequest> accept_buffer {};
      while(true)
      {
        accept_buffer = server_interface_uptr->AcceptRequests();
        accept_buffer.clear();
      }
    }
    // else, in parent. Loop.
  }
  // In parent.
  // Read the ports which are written back from the child processes. Note that
  // doing so synchronizes the parent so that it does not try to connect to the
  // FcgiServerInterface of the child until the interface has been created.
  // Close the descriptors for the write ends of the pipes.
  for(int i {0}; i != kChildCount; ++i)
  {
    close(pipe_return_array[i][1]);
  }
  in_port_t port_array[kChildCount] = {};
  for(int i {0}; i != kChildCount; ++i)
  {
    std::size_t read_return {};
    if((read_return = socket_functions::SocketRead(pipe_return_array[i][0],
      static_cast<std::uint8_t*>(static_cast<void*>(&(port_array[i]))),
      sizeof(in_port_t))) != sizeof(in_port_t))
    {
      for(int j {0}; j != kChildCount; ++j)
      {
        EXPECT_NO_FATAL_FAILURE(GTestFatalTerminateChild(child_id_array[j],
          __LINE__)) << "Iteration count: " << j;
      }
    }
    EXPECT_NE(close(pipe_return_array[i][0]), -1) << std::strerror(errno);
  }
  int local_connections[kChildCount] = {};
  for(int i {0}; i != kChildCount; ++i)
  {
    shared_connect_EINTR_return_flag = true;
    local_connections[i] = client_inter.Connect(kInternetAddresses[i],
      port_array[i]);
    EXPECT_NE(local_connections[i], -1) << std::strerror(errno);
  }
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace a_component
