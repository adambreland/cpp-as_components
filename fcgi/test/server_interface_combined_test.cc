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

#include "server_interface_combined.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>         // <cstdlib> does not define setenv. 
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "external/as_components_testing/include/as_components_testing_utilities.h"
#include "external/as_components_testing/gtest/include/as_components_testing_gtest_utilities.h"
#include "external/googletest/googletest/include/gtest/gtest.h"
#include "external/socket_functions/include/socket_functions.h"

#include "test/include/fcgi_si_testing_utilities.h"

namespace as_components {
namespace fcgi {
namespace test {

// ConstructionExceptionsAndDirectlyObservableEffects
// Examined properties:
// (Let "positive" mean an exception was thrown.)
// Properties which should cause a throw during construction:
// ("true positive" or "false negative" determination: EXPECT_THROW)
// 1) Invalid socket properties:
//    a) listening_descriptor does not refer to a socket.
//    b) The socket type is not SOCK_STREAM.
//    c) The socket is not listening.
// 2) Invalid properties related to FCGI_WEB_SERVER_ADDRS.
//    a) FCGI_WEB_SERVER_ADDRS is bound and non-empty, the domain of the
//       socket is an internet domain, and no valid internet addresses are
//       present after the value of FCGI_WEB_SERVER_ADDRS was processed as
//       a comma-separated list of the appropriate internet addresses.
// 3) Invalid value of max_connections: less than zero, zero.
// 4) Invalid value of max_requests: less than zero, zero.
// 5) Singleton violation: an interface is present and a call to construct
//    another interface is made.
//
// Properties which should not cause a throw:
// ("false positive" or "true negative" determination: EXPECT_NO_THROW)
// 1) Maximum value of max_connections.
// 2) Maximum value of max_requests.
// 3) A non-default value for app_status_on_abort.
// 4) An internet domain socket which either has FCGI_WEB_SERVER_ADDRS
//    unbound or bound and empty.
// 5) A Unix domain socket:
//    a) Where FCGI_WEB_SERVER_ADDRS is unbound.
//    b) Where FCGI_WEB_SERVER_ADDRS is bound to i-nternet addresses.
//
// Additional properties for valid cases:
// 1) Non-blocking status of file description after use for interface
//    construction.
// 2) Initial value returned by connection_count: zero.
// 3) Initial value returned by get_overload: false.
// 4) Initial value returned by interface_status: true.
// 5) Action of set_overload: After the call set_overload(true), a call to
//    get_overload should return true.
//
// Test cases:
// Throw expected:
//  1) listening_descriptor refers to a file which is not a socket.
//  2) listening_descriptor refers to a datagram socket (SOCK_DGRAM).
//  3) listening_descriotor refers to a socket which not set to the listening
//     state.
//  4) The socket is of domain AF_INET and only IPv6 addresses are present.
//  5) The socket is of domain AF_INET6 and only IPv4 addresses are present.
//  6) The socket is of domain AF_INET and a combination of invalid IPv4
//     addresses and valid IPv6 addresses are present. "Invalid" means
//     malformed.
//  7) The socket is of domain AF_INET and only a comma is present.
//  8) max_connections == -1.
//  9) max_connections == 0.
// 10) max_requests == -1.
// 11) max_requests == 0.
// 12) An interface already exists and another call to the constructor is
//     made. The arguments to the second call are the same as the first.
//
// Throw not expected:
// 13) FCGI_WEB_SERVER_ADDRS is unbound. The descriptor is a valid socket.
// 14) FCGI_WEB_SERVER_ADDRS is bound and empty. The descriptor is a valid
//     socket.
// 15) max_connections == std::numeric_limits<int>::max() &&
//     max_requests    == std::numeric_limits<int>::max()
//     Also, a non-default value is provided for app_status_on_abort.
// 16) A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is unbound.
// 17) A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is bound and has
//     IPv4 address 127.0.0.1.
//
// Modules which testing depends on:
// 1) GTestNonFatalSingleProcessInterfaceAndClients
//
// Other modules whose testing depends on this module: none.
TEST(FcgiServerInterface, ConstructionExceptionsAndDirectlyObservableEffects)
{
  testing::FileDescriptorLeakChecker fdlc {};

  auto GTestFatalClearFcgiWebServerAddrs = [](int invocation_line)->void
  {
    ::testing::ScopedTrace tracer {__FILE__, invocation_line,
      "lambda GTestFatalClearFcgiWebServerAddrs"};
    if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
      FAIL() << "setenv failed" << '\n' << std::strerror(errno);
  };

  // Ensure that FCGI_WEB_SERVER_ADDRS is bound and empty to establish a
  // consistent start state.
  ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));

  // Case 1: listening_descriptor refers to a file which is not a socket.
  // Create a temporary regular file.
  {
    int temp_fd {};
    ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalCreateBazelTemporaryFile(
      &temp_fd, __LINE__));
    EXPECT_THROW(FcgiServerInterface(temp_fd, 1, 1), std::exception);
    close(temp_fd);
  }

  // Case 2: listening_descriptor refers to a datagram socket (SOCK_DGRAM).
  {
    int socket_fd {socket(AF_INET, SOCK_DGRAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in case 2." << '\n' 
        << std::strerror(errno);
    }
    else
    {
      struct sockaddr_in sa {};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(0U); // Use an available ephemeral port.
      sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      if(bind(socket_fd, static_cast<struct sockaddr*>(static_cast<void*>(&sa)),
        sizeof(sa)) < 0)
      { 
        ADD_FAILURE() << "A call to bind failed in case 2." << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        EXPECT_THROW(FcgiServerInterface(socket_fd, 1, 1),
          std::exception);
        close(socket_fd);
      }
    }
  }
    
  // Case 3: listening_descriotor refers to a socket which not set to the
  // listening state.
  {
    int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in case 3." << '\n' 
        << std::strerror(errno);
    }
    else
    {
      struct sockaddr_in sa {};
      sa.sin_family = AF_INET;
      sa.sin_port = htons(0U); // Use an available ephemeral port.
      sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      if(bind(socket_fd, static_cast<struct sockaddr*>(static_cast<void*>(&sa)),
        sizeof(sa)) < 0)
      { 
        ADD_FAILURE() << "A call to bind failed in case 3." << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        EXPECT_THROW(FcgiServerInterface(socket_fd, 1, 1),
          std::exception);
        close(socket_fd);
      }
    }
  }

  auto FcgiWebServerAddrsCase = 
    [&GTestFatalClearFcgiWebServerAddrs](const char* address_list_ptr,
      int domain, int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};

    if(setenv("FCGI_WEB_SERVER_ADDRS", address_list_ptr, 1) < 0)
      ADD_FAILURE() << "setenv failed in" << case_suffix << '\n' 
        << std::strerror(errno);
    else
    {
      int socket_fd {socket(domain, SOCK_STREAM, 0)};
      if(socket_fd < 0)
      {
        ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
          << std::strerror(errno);
        ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
      }
      else
      {
        if(listen(socket_fd, 5) < 0)
        {
          ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n'
            << std::strerror(errno);
          close(socket_fd);
          ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
        }
        else
        {
          EXPECT_THROW(FcgiServerInterface(socket_fd, 1, 1),
            std::exception);
          close(socket_fd);
          ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
        }
      }
    }
  };

  // Case 4: The socket is of domain AF_INET and only IPv6 addresses are
  // present.
  FcgiWebServerAddrsCase("::1", AF_INET, 4);

  // Case 5: The socket is of domain AF_INET6 and only IPv4 addresses are
  // present.
  FcgiWebServerAddrsCase("127.0.0.1", AF_INET6, 5);
  
  // Case 6: The socket is of domain AF_INET and a combination of invalid IPv4
  // addresses and valid IPv6 addresses are present. "Invalid" means malformed.
  FcgiWebServerAddrsCase("::,127.0.0.256,::1,0.0.0.0.0", AF_INET, 6);

  // Case 7: The socket is of domain AF_INET and only a comma is present.
  FcgiWebServerAddrsCase(",", AF_INET, 7);

  auto MaxConnectionsMaxRequestsCase = [](int max_connections,
    int max_requests, int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};

    int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
        << std::strerror(errno);
    }
    else
    {
      if(listen(socket_fd, 5) < 0)
      { 
        ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        EXPECT_THROW(FcgiServerInterface(socket_fd, max_connections, 
          max_requests), std::exception);
        close(socket_fd);
      }
    }
  };

  // Case 8: max_connections == -1.
  MaxConnectionsMaxRequestsCase(-1, 1, 8);
  
  // Case 9: max_connections == 0.
  MaxConnectionsMaxRequestsCase(0, 1, 9);
  
  // Case 10: max_requests == -1.
  MaxConnectionsMaxRequestsCase(1, -1, 10);

  // Case 11: max_requests == 0. 
  MaxConnectionsMaxRequestsCase(1, 0, 11);

  // 12) An interface already exists and another call to the constructor is
  //     made. The arguments to the second call are the same as the first.
  {
    int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in case 12." << '\n' 
        << std::strerror(errno);
    }
    else
    {
      if(listen(socket_fd, 5) < 0)
      { 
        ADD_FAILURE() << "A call to listen failed in case 12." << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        FcgiServerInterface interface {socket_fd, 1, 1};
        EXPECT_THROW(FcgiServerInterface(socket_fd, 1, 1),
          std::exception);
        close(socket_fd);
      }
    }
  }

  // Throw not expected:

  auto InterfaceGettersAndSetters = [](int socket_fd, int max_connections, 
    int max_requests, std::int32_t app_status_on_abort, 
    std::string case_suffix)->void
  {
    EXPECT_NO_THROW(
    {
      FcgiServerInterface interface(socket_fd, max_connections,
        max_requests, app_status_on_abort);
      int f_getfl_return {fcntl(socket_fd, F_GETFL)};
      if(f_getfl_return == -1)
      {
        ADD_FAILURE() << "A call to fcntl to inspect the blocking file status "
          "of the listening socket failed in" << case_suffix;
      }
      else
      {
        if(!(f_getfl_return & O_NONBLOCK))
          ADD_FAILURE() << "The listening socket was not made non-blocking "
            "in" << case_suffix;
      }
      EXPECT_EQ(interface.connection_count(), 0U) << "connection_count "
        "did not return zero upon construction in" << case_suffix;
      EXPECT_EQ(interface.get_overload(), false) << "The interface was "
        "in a overloaded state upon construction in" << case_suffix; 
      EXPECT_EQ(interface.interface_status(), true) << "The interface "
        "was in a bad state upon construction in" << case_suffix;
      interface.set_overload(true);
      EXPECT_EQ(interface.get_overload(), true) << "A call of "
        "set_overload(true) did not do so in" << case_suffix;
    }) << "Construction or a setter or getter threw in" << case_suffix;
  };

  auto ValidSocketCase = [&InterfaceGettersAndSetters](int domain,
    int max_connections, int max_requests, std::int32_t app_status_on_abort, 
    int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};

    int socket_fd {socket(domain, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
        << std::strerror(errno);
    }
    else
    {
      // An unnamed UNIX socket is used. A call to bind is not necessary.
      if(listen(socket_fd, 5) < 0)
      { 
        ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        InterfaceGettersAndSetters(socket_fd, max_connections, max_requests,
          app_status_on_abort, case_suffix);
        close(socket_fd);
      }
    }
  };

  // Case 13: FCGI_WEB_SERVER_ADDRS is unbound. The descriptor is a valid
  // socket.
  {
    if(unsetenv("FCGI_WEB_SERVER_ADDRS") < 0)
      ADD_FAILURE() << "FCGI_WEB_SERVER_ADDRS could not be removed from the "
        "environment in case 13.";
    else
    {
      ValidSocketCase(AF_INET, 1, 1, EXIT_FAILURE, 13);
      ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
    }
  }

  // Case 14: FCGI_WEB_SERVER_ADDRS is bound and empty. The descriptor is a
  // valid socket.
  {
    ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
    ValidSocketCase(AF_INET, 1, 1, EXIT_FAILURE, 14);
  }

  // Case 15: max_connections == std::numeric_limits<int>::max() &&
  //          max_requests    == std::numeric_limits<int>::max()
  // Also, a non-default value is provided for app_status_on_abort.
  {
    int max {std::numeric_limits<int>::max()};
    ValidSocketCase(AF_INET, max, max, -10, 15);
  }

  auto UnixValidSocketCase = [&InterfaceGettersAndSetters](int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};

    // The Bazel temporary directory has a path which is too long for
    // Unix domain sockets. This due to the struct sockaddr_un limit
    // on the size of sun_path (92 bytes historical, 108 bytes currently).
    std::string unix_socket_path {"/tmp/fcgi_si_test-Unix-socket_path"};
    struct sockaddr_un unix_addr {};
    unix_addr.sun_family = AF_UNIX;
    strcpy(unix_addr.sun_path, unix_socket_path.data());

    int socket_fd {socket(AF_UNIX, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
        << std::strerror(errno);
    }
    else
    {
      if(bind(socket_fd, 
        static_cast<struct sockaddr*>(static_cast<void*>(&unix_addr)),
        sizeof(unix_addr)) < 0)
      {
        ADD_FAILURE() << "A call to bind failed in" << case_suffix << '\n'
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        if(listen(socket_fd, 5) < 0)
        { 
          ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n' 
            << std::strerror(errno);
          close(socket_fd);
          if(unlink(unix_socket_path.data()) < 0)
            FAIL() << "The test Unix domain socket file could not be unlinked."
              << '\n' << std::strerror(errno); 
        }
        else
        {
          InterfaceGettersAndSetters(socket_fd, 1, 1, EXIT_FAILURE,
            case_suffix);
          close(socket_fd);
          if(unlink(unix_socket_path.data()) < 0)
            FAIL() << "The test Unix domain socket file could not be unlinked."
              << '\n' << std::strerror(errno);
        }
      }
    }
  };

  // Case 16: A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is unbound.
  {
    if(unsetenv("FCGI_WEB_SERVER_ADDRS") < 0)
      ADD_FAILURE() << "FCGI_WEB_SERVER_ADDRS could not be removed from the "
        "environment in case 16." << '\n' << std::strerror(errno);
    else
    {
      UnixValidSocketCase(16);
      ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
    }
  }

  // Case 17: A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is bound and
  // has IPv4 address 127.0.0.1.
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "127.0.0.1", 1))
      ADD_FAILURE() << "A call to setenv failed in case 17." << '\n'
        << std::strerror(errno);
    else
    {
      UnixValidSocketCase(17);
      ASSERT_NO_FATAL_FAILURE(GTestFatalClearFcgiWebServerAddrs(__LINE__));
    }
  }

  // Check for file descriptor leaks:
  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc, 
    "ConstructionExceptionsAndDirectlyObservableEffects", __LINE__);
}

// FcgiGetValues
//    The FastCGI protocol requires applications to respond to
// FCGI_GET_VALUES management requests. Such a request includes a collection
// of name-value pairs which are encoded in the FastCGI name-value pair
// encoding. The values of these names are empty. Three names are defined by
// the protocol: FCGI_MAX_CONNS, FCGI_MAX_REQUESTS, and FCGI_MPXS_CONNS. Any
// name that is included in a request which is not understood by the
// application should be omitted in the application's response.
//    FCGI_GET_VALUES will usually occur immediately after a connection is
// made. This test examines the behavior of the interface in that situation.
//
// Examined properties:
// 1) Presence of unknown names.
// 2) Position of unknown names in the FastCGI name-value pair byte sequence.
//    a) In the beginning.
//    b) In the middle with a known name after an unknown name.
// 3) Unknown name which requires four bytes to be encoded in the FastCGI
//    name-value pair encoding.
// 4) Subsets of the known names.
// 5) An empty request.
// 6) Presence of an empty name.
// 7) An erroneous request body.
//
// Test cases: All cases use an interface which accepts a single request
// and a single connection at a time.
// 1) An empty request.
// 2) Only known names. All three known names.
// 3) Only known names. A single known name. The three variations given that
//    there are three known names.
// 4) Unknown name present. A single-byte unknown name in the first position.
//    All three known names follow. Then an empty name.
// 5) Unknown name present. A four-byte unknown name in the first position.
//    All three known names follow.
// 6) Unknown name present. A known name, then a single-byte unknown name,
//    then a known name.
// 7) Unknown name present. A known name, then a four-byte unknown name, then
//    a known name.
// 8) All unknown names.
// 9) A request with a known name and a terminal name-value pair encoding
//    error where more bytes are specified than are actually present.
//
// Modules which testing depends on:
// 1) EncodeNameValuePairs
// 2) ExtractBinaryNameValuePairs
// 3) PopulateHeader
// 4) as_components::socket_functions::ScatterGatherSocketWrite
// 5) as_components::socket_functions::SocketRead
// 6) GTestNonFatalSingleProcessInterfaceAndClients
//
// Other modules whose testing depends on this module: none.
TEST(FcgiServerInterface, FcgiGetValues)
{
  testing::FileDescriptorLeakChecker fdlc {};

  // Ensure that SIGALRM has its default disposition.
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGALRM,
    __LINE__));

  // Lambda functions for test case implementations.
  struct ScatterGatherSocketWriteArgs
  {
    struct iovec* iovec_ptr;
    int iovec_count;
    std::size_t number_to_write;
  };

  auto FcgiGetValuesTest = [](ScatterGatherSocketWriteArgs a,
    const std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>&
      expected_result, int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};

    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 1;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;
    
    GTestNonFatalSingleProcessInterfaceAndClients spiac {};
    try
    {
      spiac = GTestNonFatalSingleProcessInterfaceAndClients
        {inter_args, 1, __LINE__};
    }
    catch(const std::exception& e)
    {
      ADD_FAILURE() << "An exception was thrown when the normal "
        "GTestNonFatalSingleProcessInterfaceAndClients "
        "constructor was called in" << case_suffix << '\n' << e.what();
      return;
    }

    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[0], a.iovec_ptr, a.iovec_count, 
      a.number_to_write)) != 0U)
    {
      ADD_FAILURE() << "An error occurred while writing to the interface in"
        << case_suffix << '\n' << std::strerror(errno);
      return;
    }

    // Allow the interface to process the FCGI_GET_VALUES request (record).
    alarm(1U);
    std::vector<FcgiRequest> get_values_accept_return 
      {spiac.interface().AcceptRequests()};
    alarm(0U);
    if(get_values_accept_return.size())
    {
      ADD_FAILURE() << "A request was returned when none was expected in"
        << case_suffix;
      return;
    }

    // Confirm that observable interface state is as expected.
    EXPECT_EQ(spiac.interface().connection_count(), 1U);
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);

    // Read the FCGI_GET_VALUES_RESULT response.
    std::uint8_t read_buffer[128U];
    std::vector<std::uint8_t> returned_result {};
    bool read {true};
    while(read)
    {
      std::size_t read_return {as_components::socket_functions::SocketRead(
        spiac.client_descriptors()[0], read_buffer, 128U)};
      returned_result.insert(returned_result.end(), read_buffer, 
        read_buffer + read_return);
      if(read_return < 128U)
      {
        read = false;
        if((errno != EAGAIN) && (errno != EWOULDBLOCK))
        {
          ADD_FAILURE() << "An error occurred while reading the response from "
            "the interface in" << case_suffix << '\n' << std::strerror(errno);
          return;
        }
      }
    }

    if((returned_result.size() < FCGI_HEADER_LEN) || 
       (returned_result[1] != static_cast<std::uint8_t>(
         FcgiType::kFCGI_GET_VALUES_RESULT)))
    {
      ADD_FAILURE() << "The output from the interface was formatted "
        "incorrectly in" << case_suffix;
      return;
    }
    // As uint16_t is shorter than int in most cases, integral promotion to int
    // causes a narrowing warning if conversion to uint16_t is performed 
    // directly on the arguments to << and +.
    std::uint16_t record_length 
    {
      std::uint16_t(
         (std::int_fast32_t(returned_result[kHeaderContentLengthB1Index]) << 8) +
          std::int_fast32_t(returned_result[kHeaderContentLengthB0Index])
      )
    };
    if((returned_result.size() - FCGI_HEADER_LEN) < record_length)
    {
      ADD_FAILURE() << "A discrepancy between the specified content length and "
        "the actual content length of the FCGI_GET_VALUES_RESULT record was "
        "present in" << case_suffix;
      return;
    }
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
    returned_pairs {};
    if(record_length > 0)
    {
      returned_pairs = ExtractBinaryNameValuePairs(
        returned_result.data() + FCGI_HEADER_LEN,
        record_length);
      if(returned_pairs.size() == 0)
      {
        ADD_FAILURE() << "An encoding error was present in the binary name-"
          "value pair content returned by the interface in" << case_suffix;
        return;
      }
    }
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> result {};
    for(std::pair<std::vector<uint8_t>, std::vector<uint8_t>> pair : 
      returned_pairs)
      result.insert(std::move(pair));
    if(result != expected_result)
    {
      ADD_FAILURE() << "The output from the interface was incorrect in" 
        << case_suffix;
      return;
    }
  };

  using pair_vector = std::vector<std::pair<std::vector<std::uint8_t>, 
    std::vector<std::uint8_t>>>;

  // Case 1: Empty FCGI_GET_VALUES record
  {
    std::uint8_t header[FCGI_HEADER_LEN];
      PopulateHeader(header, FcgiType::kFCGI_GET_VALUES,
        0U, 0U, 0U);
    struct iovec iovec_array[1] = {};
    iovec_array[0].iov_base = &header;
    iovec_array[0].iov_len  = FCGI_HEADER_LEN;
    ScatterGatherSocketWriteArgs args {};
    args.iovec_ptr       = iovec_array;
    args.iovec_count     = 1;
    args.number_to_write = FCGI_HEADER_LEN;
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map {};
    FcgiGetValuesTest(args, pair_map, 1);
  }
  
  auto TestCaseRunner = [&FcgiGetValuesTest](pair_vector input_pairs, 
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> input_map,
    int test_case)->void
  {
    std::tuple<bool, std::size_t, std::vector<iovec>, int,
      std::vector<std::uint8_t>, std::size_t, pair_vector::iterator>
    returned_encoding_information {EncodeNameValuePairs(
      input_pairs.begin(), input_pairs.end(), 
      FcgiType::kFCGI_GET_VALUES, 0U, 0U)};
    if(EncodeNVPairSingleRecordFailure(returned_encoding_information,
      input_pairs.end()))
    {
      ADD_FAILURE() << "The name-value pairs given to EncodeNameValuePairs "
        "caused an error.";
      return;
    }
    ScatterGatherSocketWriteArgs args {};
    args.iovec_ptr       = std::get<2>(returned_encoding_information).data();
    args.iovec_count     = std::get<2>(returned_encoding_information).size();
    args.number_to_write = std::get<1>(returned_encoding_information);

    FcgiGetValuesTest(args, input_map, test_case);
  };

  // Case 2: Only known names. All three known names.
  {
    pair_vector nv_pairs 
      {{FCGI_MAX_CONNS, {}}, 
       {FCGI_MAX_REQS, {}}, 
       {FCGI_MPXS_CONNS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{FCGI_MAX_CONNS, {'1'}},
       {FCGI_MAX_REQS, {'1'}},
       {FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 2);
  }

  // Case 3: Only known names. A single known name. Three variations for each of
  // the known names.
  {
    // FCGI_MAX_CONNS
    pair_vector nv_pairs {{FCGI_MAX_CONNS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{FCGI_MAX_CONNS, {'1'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 3);

    // FCGI_MAX_REQS
    nv_pairs.clear();
    nv_pairs.push_back({FCGI_MAX_REQS, {}});
    pair_map.clear();
    pair_map.insert({FCGI_MAX_REQS, {'1'}});
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 3);

    // FCGI_MPXS_CONNS
    nv_pairs.clear();
    nv_pairs.push_back({FCGI_MPXS_CONNS, {}});
    pair_map.clear();
    pair_map.insert({FCGI_MPXS_CONNS, {'0'}});
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 3);
  }

  // Case 4: Unknown name present. A single-byte unknown name in the
  // first position. All three known names follow.
  {
    pair_vector nv_pairs 
      {{{'R', 'O', 'L', 'E'}, {}},
       {FCGI_MAX_CONNS, {}}, 
       {FCGI_MAX_REQS, {}}, 
       {FCGI_MPXS_CONNS, {}},
       {{}, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{FCGI_MAX_CONNS, {'1'}},
       {FCGI_MAX_REQS, {'1'}},
       {FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 4);
  }

  // Case 5: Unknown name present. A four-byte unknown name in the first 
  // position. All three known names follow.
  {
    pair_vector nv_pairs 
      {{std::vector<std::uint8_t>(200, 'A'), {}},
       {FCGI_MAX_CONNS, {}}, 
       {FCGI_MAX_REQS, {}}, 
       {FCGI_MPXS_CONNS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{FCGI_MAX_CONNS, {'1'}},
       {FCGI_MAX_REQS, {'1'}},
       {FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 5);
  }
  
  // Case 6: Unknown name present. A known name, then a single-byte unknown
  // name, then a known name.
  {
    pair_vector nv_pairs 
      {{FCGI_MAX_CONNS, {}},
       {{'S','E','R','V','E','R'}, {}},
       {FCGI_MAX_REQS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{FCGI_MAX_CONNS, {'1'}},
       {FCGI_MAX_REQS, {'1'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 6);
  }
  
  // Case 7: Unknown name present. A known name, then a four-byte unknown name,
  // then a known name.
  {
    pair_vector nv_pairs 
      {{FCGI_MAX_CONNS, {}},
       {std::vector<std::uint8_t>(200, 'A'), {}},
      {FCGI_MPXS_CONNS, {'0'}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
    {{FCGI_MAX_CONNS, {'1'}},
      {FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 7);
  }

  // 8) All unknown names.
  {
    pair_vector nv_pairs
      {{{'S','E','R','V','E','R'}, {}},
       {{'R', 'O', 'L', 'E'}, {}},
       {std::vector<std::uint8_t>(200, 'A'), {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map {};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 8);
  }

  // 9) A request with a known name and a terminal name-value pair encoding
  //    error where more bytes are specified than are actually present.
  {
    // Create an erroneous record.
    const char* error_name {"error"};
    std::size_t error_name_length {std::strlen(error_name)};
    std::size_t content_length {1U + 1U + FCGI_MAX_CONNS.size() + 1U +
      1U + error_name_length};
    std::size_t mod_length {content_length % 8};
    std::size_t padding_length {(mod_length) ? (8 - mod_length) : 0};
    std::uint8_t record[FCGI_HEADER_LEN + content_length +
      padding_length] = {};
    PopulateHeader(record, FcgiType::kFCGI_GET_VALUES,
      0U, content_length, padding_length);
    std::size_t offset {static_cast<std::size_t>(FCGI_HEADER_LEN)};
    record[offset] = FCGI_MAX_CONNS.size();
    offset++;
    record[offset] = 0U;
    offset++;
    std::memcpy(record + offset, FCGI_MAX_CONNS.data(),
      FCGI_MAX_CONNS.size());
    offset += FCGI_MAX_CONNS.size();
    record[offset] = error_name_length;
    offset++;
    record[offset] = 10U; // This is the erroneous value length.
    offset++;
    std::memcpy(record + offset, error_name, error_name_length);
    offset += error_name_length;
    offset += padding_length;
    struct iovec single_iovec {record, offset};

    struct ScatterGatherSocketWriteArgs args {&single_iovec, 1, offset};
    FcgiGetValuesTest(args, {}, 9);
  }

  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc,
    "FcgiGetValues", __LINE__);
}

// UnknownManagementRequests
// This test examines the behavior of a new interface to unknown management
// requests.
//
// Examined properties:
// 1) The type of the management request is not FCGI_GET_VALUES and either
//    is one of the defined types or not.
// 2) The unknown management request has content or not.
// 3) The alignment of the request is on an 8-byte boundary or not.
// 4) The presence or absence of padding in the request.
//
// Test cases:
// 1) The management request type is FCGI_STDIN. No content is present.
// 2) The management request type has value 25. No content is present.
// 3) The management request type has value 100. A body of bytes where
//    each byte has value 1 when interpreted as std::uint8_t is present.
//    The content is aligned on an 8-byte boundary.
// 4) As in 3, but the content is not aligned on an 8-byte boundary and
//    padding is used.
// 5) As in 3, but content is not aligned on an 8-byte boundary and no
//    padding is used.
//
// Modules which testing depends on:
// 1) as_components::socket_functions::SocketRead
// 2) as_components::socket_functions::SocketWrite
// 3) PopulateHeader
// 4) GTestNonFatalSingleProcessInterfaceAndClients
//
// Other modules whose testing depends on this module: none.
TEST(FcgiServerInterface, UnknownManagementRequests)
{
  testing::FileDescriptorLeakChecker fdlc {};

  auto UnknownManagementRecordTester = [](
    struct InterfaceCreationArguments args,
      std::uint8_t* buffer_ptr, std::size_t count, FcgiType type,
      int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};
    try
    {
      GTestNonFatalSingleProcessInterfaceAndClients spiac
        {args, 1, __LINE__};
      if(as_components::socket_functions::SocketWrite(
        spiac.client_descriptors()[0], buffer_ptr, count) < count)
      {
        ADD_FAILURE() << "Writing a request to the interface could not be "
          "performed in full in" << case_suffix << '\n' << std::strerror(errno);
        return;
      }
      // Allow the interface to process the request.
      spiac.interface().AcceptRequests();
      // Read the response.
      constexpr int_fast32_t response_length 
        {2 * FCGI_HEADER_LEN};
      std::uint8_t read_buffer[response_length] = {};
      if(as_components::socket_functions::SocketRead(
        spiac.client_descriptors()[0], read_buffer, response_length) 
        < (response_length))
      {
        ADD_FAILURE() << "Fewer than the expected number of bytes were read "
          "of the response by the interface to an unknown management request "
          "in" << case_suffix << '\n' << std::strerror(errno);
        return;
      }
     
      // Verify response information.
      EXPECT_EQ(1U, read_buffer[0]) << "The FastCGI protocol version was "
        "incorrect in the response in" << case_suffix;
      EXPECT_EQ(read_buffer[kHeaderTypeIndex], 
        static_cast<std::uint8_t>(FcgiType::kFCGI_UNKNOWN_TYPE)) <<
        "The type was not equal to FCGI_UNKNOWN_TYPE in" << case_suffix;
      EXPECT_EQ(0, (std::int_fast32_t(
        read_buffer[kHeaderRequestIDB1Index]) << 8) + 
        std::int_fast32_t(read_buffer[kHeaderRequestIDB0Index]))
        << "The request ID was not zero in the response in" << case_suffix;
      EXPECT_EQ(FCGI_HEADER_LEN, (std::int_fast32_t(
        read_buffer[kHeaderContentLengthB1Index]) << 8) + 
        std::int_fast32_t(read_buffer[kHeaderContentLengthB0Index]))
        << "The response contained more content than specified in" 
        << case_suffix;
      EXPECT_EQ(0U, read_buffer[kHeaderPaddingLengthIndex])
        << "Padding was present in the response in" << case_suffix;
      EXPECT_EQ(static_cast<std::uint8_t>(type),
        read_buffer[kHeaderReservedByteIndex + 1]) << "The type "
        "sent by the client was not correctly returned by the interface in"
        << case_suffix;
      
      // Ensure that unexpected information was not received.
      std::size_t read_return {as_components::socket_functions::SocketRead(
        spiac.client_descriptors()[0], read_buffer, 1)};
      if(read_return != 0U)
      {
        ADD_FAILURE() << "The interface sent unexpected data to a client "
        "when it responded to an unknown management request in" << case_suffix; 
        return;
      }
      if(!((errno == EAGAIN) || (errno == EWOULDBLOCK)))
      {
        ADD_FAILURE() << "An error occurred during the check for extra data "
          "in" << case_suffix << '\n' << std::strerror(errno);
        return;
      }

      // Verify observable interface state.
      EXPECT_EQ(spiac.interface().get_overload(), false) << "The interface was "
        "found to be overloaded in" << case_suffix;
      EXPECT_EQ(spiac.interface().connection_count(), 1U) << "The interface "
        "did not show one connection in" << case_suffix;
      EXPECT_EQ(spiac.interface().interface_status(), true) << "The interface "
        "was found to be in a bad state in" << case_suffix;
    }
    catch(const std::exception& e)
    {
      ADD_FAILURE() << "An exception was thrown in the implementation of "
        "UnknownManagementRecordTester in" << case_suffix << '\n' << e.what();
    }
  };

  // Case 1: The management request type is FCGI_STDIN. No content is present.
  {
    struct InterfaceCreationArguments args {};
    args.domain          = AF_INET;
    args.backlog         = 5;
    args.max_connections = 10;
    args.max_requests    = 10;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;
    
    std::uint8_t header[FCGI_HEADER_LEN] = {};
    PopulateHeader(header, FcgiType::kFCGI_STDIN,
      0U, 0U, 0U);
    UnknownManagementRecordTester(args, header, FCGI_HEADER_LEN, 
    FcgiType::kFCGI_STDIN, 1);
  }

  // Case 2: The management request type has value 25. No content is present.
  {
    struct InterfaceCreationArguments args {};
    args.domain          = AF_INET;
    args.backlog         = 5;
    args.max_connections = 10;
    args.max_requests    = 10;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;

    std::uint8_t header[FCGI_HEADER_LEN] = {};
    PopulateHeader(header, static_cast<FcgiType>(25),
      0U, 0U, 0U);
    UnknownManagementRecordTester(args, header, FCGI_HEADER_LEN, 
    static_cast<FcgiType>(25), 2);
  }

  // Allows tests which rely on the presence of IPv6 networking to be skipped
  // when the environment variable NO_IPV6 is set to any value.
  if(!std::getenv("NO_IPV6"))
  {
    // Case 3: The management request type has value 100. A body of bytes where
    // each byte has value 1 when interpreted as std::uint8_t is present. The
    // content is aligned on an 8-byte boundary.
    {
      struct InterfaceCreationArguments args {};
      args.domain          = AF_INET6;
      args.backlog         = 5;
      args.max_connections = 1000;
      args.max_requests    = 1000;
      args.app_status      = EXIT_FAILURE;
      args.unix_path       = nullptr;

      std::uint8_t header[2 * FCGI_HEADER_LEN] = {};
      PopulateHeader(header, static_cast<FcgiType>(100),
        0U, FCGI_HEADER_LEN, 0U);
      std::memset(header + FCGI_HEADER_LEN, 1, FCGI_HEADER_LEN);
      UnknownManagementRecordTester(args, header, 2 * FCGI_HEADER_LEN, 
      static_cast<FcgiType>(100) , 3);
    }

    // Case 4: As in 3, but the content is not aligned on an 8-byte boundary and
    // padding is used.
    {
      struct InterfaceCreationArguments args {};
      args.domain          = AF_INET6;
      args.backlog         = 5;
      args.max_connections = 1;
      args.max_requests    = 1;
      args.app_status      = EXIT_FAILURE;
      args.unix_path       = nullptr;

      std::uint8_t header[2 * FCGI_HEADER_LEN] = {};
      PopulateHeader(header, static_cast<FcgiType>(100),
        0U, 3U, 5U);
      std::memset(header + FCGI_HEADER_LEN, 1, 3);
      UnknownManagementRecordTester(args, header, 2 * FCGI_HEADER_LEN, 
      static_cast<FcgiType>(100) , 4);
    }

    // Case 5: As in 3, but content is not aligned on an 8-byte boundary and no
    // padding is used.
    {
      struct InterfaceCreationArguments args {};
      args.domain          = AF_INET6;
      args.backlog         = 5;
      args.max_connections = 1;
      args.max_requests    = 1;
      args.app_status      = EXIT_FAILURE;
      args.unix_path       = nullptr;

      std::uint8_t header[FCGI_HEADER_LEN + 3U] = {};
      PopulateHeader(header, static_cast<FcgiType>(100),
        0U, 3U, 0U);
      std::memset(header + FCGI_HEADER_LEN, 1, 3);
      UnknownManagementRecordTester(args, header, FCGI_HEADER_LEN + 3, 
      static_cast<FcgiType>(100) , 5);
    }
  }

  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc,
    "UnknownManagementRequests", __LINE__);
}

namespace {

// Preconditions: 
// 1) overload_after > 0.
struct ConnectionAcceptanceAndRejectionTestArguments
{
  struct InterfaceCreationArguments inter_args;
  std::size_t initial_connections;
  std::size_t overload_after;
  std::vector<std::uint8_t> expected_status;
  int test_case;
};

//   Creates an interface with the provided constructor arguments (the
// listening socket is implicitly created) and creates
// (initial_connections + 1) clients which connect to the interface.
// The overload integer indicates the connection number after which the
// interface is put into an overloaded state through a call to
// set_overload(true).
//    The status of each connection is determined. The status codes described
// below are used to summarize connection status. The expected list of 
// statuses is compared to the actual list. A test case fails if a
// discrepancy is present.
//
// For AF_UNIX:
//    Clients are not bound to a specific file path.
//
// For AF_INET:
//    Clients are given unique, incremented loopback IP addresses starting at
// 127.0.0.1. For example, the first client has address IP 127.0.0.1 and the
// second client has IP address 127.0.0.2.
//
// For AF_INET6:
//    All clients use the loopback address ::1.
//
// Code for connection status during the final inspection:
// 0: connection closed.
// 1: connection open, but no data was received.
// 2: connection open, data received.
class ConnectionAcceptanceAndRejectionTest
{
 public:
  void RunTest();

  ConnectionAcceptanceAndRejectionTest(
    struct ConnectionAcceptanceAndRejectionTestArguments args);
  ~ConnectionAcceptanceAndRejectionTest();

 private:
  // Test case information:
  struct ConnectionAcceptanceAndRejectionTestArguments args_;
  std::string case_suffix_;

  // Resource information for conditional deallocation:
  int socket_pair_array_[2] = {-1, -1};
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_tuple_ {};
};

ConnectionAcceptanceAndRejectionTest::
ConnectionAcceptanceAndRejectionTest(
  struct ConnectionAcceptanceAndRejectionTestArguments args)
{
  args_ = std::move(args);
  case_suffix_ = CaseSuffix(args_.test_case);

  if(!((args_.inter_args.domain == AF_UNIX)    || 
       (args_.inter_args.domain == AF_INET)    ||
       (args_.inter_args.domain == AF_INET6)))
  {
    ADD_FAILURE() << "Invalid domain argument in" << case_suffix_;
    throw std::logic_error {""};
  }
  // 92 comes from the lowest known size of sun_path in struct sockaddr_un
  // across distributions. One is added to the observed length as a terminating
  // null byte must be copied as well.
  if((args_.inter_args.domain == AF_UNIX) && 
      ((std::strlen(args_.inter_args.unix_path) + 1) > 92))
  {
    ADD_FAILURE() << "The interface path was too long in" << case_suffix_;
    throw std::logic_error {""};
  }
}

ConnectionAcceptanceAndRejectionTest::
~ConnectionAcceptanceAndRejectionTest()
{
  if(socket_pair_array_[0] != -1)
    close(socket_pair_array_[0]);
  if(socket_pair_array_[1] != -1)
    close(socket_pair_array_[1]);
  if(std::get<0>(inter_tuple_))
  {
    // Close the listening interface socket.
    close(std::get<1>(inter_tuple_));
    if(args_.inter_args.domain == AF_UNIX)
    {
      if(unlink(args_.inter_args.unix_path) == -1)
        ADD_FAILURE() << "An error occurred when an attempt was made to remove "
          "the UNIX socket file in" << case_suffix_;
    }
    // Note that the destructor of the interface is responsible for closing
    // the descriptors of the connections it accepted.
  }
}

void ConnectionAcceptanceAndRejectionTest::
RunTest()
{
  // Create a pair of sockets to allow synchronization of and communication
  // between the test process and a child process it creates below.
  if(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair_array_) == -1)
  {
    ADD_FAILURE() << "A call to socketpair failed in" << case_suffix_ << '\n'
      << std::strerror(errno);
    return;
  }
  
  std::size_t total_connections {args_.initial_connections + 1};

  // Fork.

  pid_t fork_return {fork()};
  if(fork_return == -1)
  {
    ADD_FAILURE() << "A call to fork failed in" << case_suffix_ << '\n'
      << std::strerror(errno);
    return;
  }
  if(fork_return == 0) // child
  {
    // _exit is used to terminate the child process. This, among other things,
    // prevents the destructor from being executed. The resources used by the
    // child are implicitly released when the process terminates.

    // Close one of the sockets to allow proper recognition of the
    // termination of the parent process.
    if(close(socket_pair_array_[0]) == -1)
      _exit(EXIT_FAILURE);

    // Block until the parent writes to the socket. sizeof(in_port_t)
    // bytes are expected. These represent the port of the interface for the
    // internet domains and a ready signal for AF_UNIX.
    in_port_t port {};
    std::size_t socket_read {as_components::socket_functions::SocketRead(
      socket_pair_array_[1],
      static_cast<std::uint8_t*>(static_cast<void*>(&port)), 
      sizeof(port)
    )};
    if(socket_read < sizeof(port))
    {
      // An error occurred while reading. Terminate the child process.
      _exit(EXIT_FAILURE);
    }
                
    // Prepare socket state for "clients." With internet domains, the clients
    // have a specific IP address and an ephemeral port. For AF_UNIX, the
    // clients are unnamed. For all domains, an interface address is needed 
    // for client connection.
    // AF_UNIX state.
    struct sockaddr_un AF_UNIX_interface_addr {};
    if(args_.inter_args.domain == AF_UNIX)
    {
      AF_UNIX_interface_addr.sun_family = AF_UNIX;
      std::strcpy(AF_UNIX_interface_addr.sun_path, 
        args_.inter_args.unix_path);
    }
    // AF_INET state.
    struct sockaddr_in AF_INET_interface_addr {};
    struct sockaddr_in AF_INET_client_addr {};
    // The numeric value of the loopback address is stored in host byte
    // order to allow incremental address binding.
    std::uint32_t AF_INET_ip_address {};
    if(args_.inter_args.domain == AF_INET)
    {
      // To allow clients to connect to the interface socket.
      AF_INET_interface_addr.sin_family      = AF_INET;
      AF_INET_interface_addr.sin_port        = port;
      AF_INET_interface_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      AF_INET_client_addr.sin_family      = AF_INET;
      AF_INET_client_addr.sin_port        = htons(0U);
      AF_INET_client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

      AF_INET_ip_address = ntohl(AF_INET_client_addr.sin_addr.s_addr);
    }
    // AF_INET6 state.
    struct sockaddr_in6 AF_INET6_interface_addr {};
    struct sockaddr_in6 AF_INET6_client_addr {};
    // All clients will use the loopback address.
    if(args_.inter_args.domain == AF_INET6)
    {
      // To allow clients to connect to the interface socket.
      AF_INET6_interface_addr.sin6_family = AF_INET6;
      AF_INET6_interface_addr.sin6_port   = port;
      AF_INET6_interface_addr.sin6_addr   = in6addr_loopback;

      AF_INET6_client_addr.sin6_family = AF_INET6;
      AF_INET6_client_addr.sin6_port   = htons(0U);
      AF_INET6_client_addr.sin6_addr   = in6addr_loopback;
    }

    struct sockaddr* interface_addr_ptr {nullptr};
    struct sockaddr* client_addr_ptr {nullptr};
    socklen_t socket_addr_length {};
    if(args_.inter_args.domain == AF_UNIX)
    {
      interface_addr_ptr = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_UNIX_interface_addr));
      socket_addr_length = sizeof(AF_UNIX_interface_addr);
    }
    else if(args_.inter_args.domain == AF_INET)
    {
      interface_addr_ptr = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET_interface_addr));
      client_addr_ptr    = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET_client_addr));
      socket_addr_length = sizeof(AF_INET_interface_addr);
    }
    else
    {
      interface_addr_ptr = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET6_interface_addr));
      client_addr_ptr    = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET6_client_addr));
      socket_addr_length = sizeof(AF_INET6_interface_addr);
    }

    // State for interprocess signalling.
    std::uint8_t null_byte {0U};
    std::uint8_t received_byte {0U};
    std::vector<int> client_socket_descriptor_list(
      total_connections, -1);
    for(std::size_t i {0}; i < total_connections; ++i)
    {
      // Create the client socket, make it non-blocking, and connect to the
      // interface.
      client_socket_descriptor_list[i] = 
        socket(args_.inter_args.domain, SOCK_STREAM, 0);
      if(client_socket_descriptor_list[i] < 0)
        _exit(EXIT_FAILURE);
      int f_getfl_return {fcntl(client_socket_descriptor_list[i], F_GETFL)};
      if(f_getfl_return == -1)
        _exit(EXIT_FAILURE);
      f_getfl_return |= O_NONBLOCK;
      if(fcntl(client_socket_descriptor_list[i], F_SETFL, f_getfl_return) 
        == -1)
        _exit(EXIT_FAILURE);
      // Bind the client to a specific address.
      if((args_.inter_args.domain == AF_INET) || 
        (args_.inter_args.domain == AF_INET6))
      {
        if(bind(client_socket_descriptor_list[i], client_addr_ptr, 
          socket_addr_length) == -1)
          _exit(EXIT_FAILURE);
      }
      //    Non-blocking UNIX sockets appear to at times successfully connect
      // instead of failing with errno == EAGAIN. This case is accepted.
      // Note that failure with errno == EAGAIN for non-blocking UNIX domain 
      // sockets is not documented in some man pages.
      //    Non-blocking internet sockets fail with errno == EINPROGRESS on
      // a call to connect when the listening socket hasn't accepted the
      // connection.
      int connect_return {connect(client_socket_descriptor_list[i], 
        interface_addr_ptr, socket_addr_length)};
      if(connect_return == -1)
      {
        if(((args_.inter_args.domain == AF_UNIX) && (errno != EAGAIN)) || 
            ((args_.inter_args.domain != AF_UNIX) && (errno != EINPROGRESS)))
          _exit(EXIT_FAILURE);
      }
      // Signal the interface process that a connection was made and wait
      // for the interface to signal that another connection can be made.
      std::size_t signal_to {as_components::socket_functions::SocketWrite(
        socket_pair_array_[1], &null_byte, 1)};
      if(signal_to < 1)
        _exit(EXIT_FAILURE);
      std::size_t signal_from {as_components::socket_functions::SocketRead(
        socket_pair_array_[1], &received_byte, 1)};
      if(signal_from < 1)
        _exit(EXIT_FAILURE);

      // Formally, a system call to make sure that the connection was completed
      // is necessary if EAGAIN or EINPROGRESS was returned.
      if(connect_return == -1)
      {
        fd_set descriptor_set {};
        FD_ZERO(&descriptor_set);
        FD_SET(client_socket_descriptor_list[i], &descriptor_set);
        // Ensure that select does not block for long --- blocking indicates
        // an error.
        alarm(1U);
        int select_return {select(client_socket_descriptor_list[i] + 1, 
          nullptr, &descriptor_set, nullptr, nullptr)};
        alarm(0U);
        if(select_return == -1)
          _exit(EXIT_FAILURE);
        int getsockopt_buffer {};
        socklen_t getsockopt_buffer_length {sizeof(getsockopt_buffer)};
        int getsockopt_return {getsockopt(client_socket_descriptor_list[i],
          SOL_SOCKET, SO_ERROR, &getsockopt_buffer, &getsockopt_buffer_length)};
        if((getsockopt_return == -1) || (getsockopt_buffer != 0))
          _exit(EXIT_FAILURE);
      }

      // Update address state for AF_INET as incremental IP addresses are
      // used.
      if(args_.inter_args.domain == AF_INET)
      {
        AF_INET_ip_address++;
        AF_INET_client_addr.sin_addr.s_addr = htonl(AF_INET_ip_address);
      }
    }
    // On loop exit, the interface signaled that it is ready.
    // Inspect the connections and send back a status report.
    std::vector<std::uint8_t> status_list {};
    for(std::size_t i {0}; i < total_connections; ++i)
    {
      std::size_t read_status {as_components::socket_functions::SocketRead(
        client_socket_descriptor_list[i], &received_byte, 1)};
      if(read_status != 0)
        status_list.push_back(2U); // Received data.
      else if((errno == EWOULDBLOCK) || (errno == EAGAIN))
        status_list.push_back(1U); // Connected and no data.
      else if(errno == 0)
        status_list.push_back(0U); // Disconnected.
      else
        _exit(EXIT_FAILURE);       // Error trying to read. 
    }
    std::size_t status_report_transmission {as_components::socket_functions::
      SocketWrite(socket_pair_array_[1], status_list.data(),
      status_list.size())};
    _exit((status_report_transmission < status_list.size()) ? 
      EXIT_FAILURE : EXIT_SUCCESS);
  }
  // else, parent.
  // Close the socket used by the client.
  int close_return {close(socket_pair_array_[1])};
  // Set the closed descriptor value to -1 to ensure that that destructor
  // does not try to close it again.
  socket_pair_array_[1] = -1;
  if(close_return == -1)
  {
    ADD_FAILURE() << "An error was encountered in the parent when it tried "
      "to close the socket for the child in" << case_suffix_
      << '\n' << std::strerror(errno);
    return;
  }
  
  const char* construction_ex_message {"An exception was thrown by "
    "CreateInterface in"};
  try
  {
    inter_tuple_ = GTestNonFatalCreateInterface(args_.inter_args, __LINE__);
  }
  catch(std::system_error& error)
  {
    ADD_FAILURE() << construction_ex_message << case_suffix_ << '\n' 
      << "errno: "  << error.code().value() << '\n'
      << std::strerror(error.code().value()) << '\n' << error.what();
    return;
  }
  catch(std::exception& error)
  {
    ADD_FAILURE() << construction_ex_message << case_suffix_ << '\n'
      << error.what();
    return;
  }
  if(!std::get<0>(inter_tuple_))
  {
    ADD_FAILURE() << "Interface construction failed in" << case_suffix_;
    return;
  }

  // Write the port for internet domains and at least a byte for AF_UNIX.
  std::size_t port_write {as_components::socket_functions::SocketWrite(
    socket_pair_array_[0], static_cast<std::uint8_t*>(static_cast<void*>(
    &std::get<2>(inter_tuple_))), sizeof(std::get<2>(inter_tuple_)))};
  if(port_write < sizeof(std::get<2>(inter_tuple_)))
  {
    ADD_FAILURE() << "An error occurred while sending the port to the "
    "process for client sockets in" << case_suffix_ << '\n'
      << std::strerror(errno);
    return;
  }
  std::uint8_t null_byte {0U};
  std::uint8_t received_byte {};
  for(std::size_t connection_count {1}; 
      connection_count <= total_connections;
      ++connection_count)
  {
    // Wait for client process readiness. A connection should be pending
    // on the interface.
    std::size_t client_signal {as_components::socket_functions::SocketRead(
      socket_pair_array_[0], &received_byte, 1)};
    if(client_signal < 1)
    {
      if(errno == 0)
        ADD_FAILURE() << "The synchronization socket was found to be closed "
          "when checking for client process readiness in" << case_suffix_;
      else
        ADD_FAILURE() << "An error occurred while reading from the "
          "synchronization socket when checking for client process "
          "readiness in" << case_suffix_ << '\n' << std::strerror(errno);
      return;     
    }
    // Allow the interface to process the connection.
    std::vector<FcgiRequest> result {};
    const char* exception_message {"An exception was caught when "
      "AcceptRequests was called in"};
    alarm(1U); // If AcceptRequests blocks, kill the process quickly.
    try
    {
      result = std::get<0>(inter_tuple_)->AcceptRequests();
    }
    catch(std::system_error& error)
    {
      ADD_FAILURE() << exception_message << case_suffix_ << '\n' << "errno: " 
        << error.code().value() << '\n'
        << std::strerror(error.code().value()) << '\n' << error.what();
      return;
    }
    catch(std::exception& error)
    {
      ADD_FAILURE() << exception_message << case_suffix_ << '\n'
        << error.what();
      return;
    }
    alarm(0U);
    if(result.size())
    {
      ADD_FAILURE() << "An FcgiRequest object was returned when none was "
        "expected in" << case_suffix_;
      return;
    } 
    if(connection_count == args_.overload_after)
      std::get<0>(inter_tuple_)->set_overload(true);
    // Signal that the interface processed the connection.
    std::size_t socket_write {as_components::socket_functions::SocketWrite(
      socket_pair_array_[0], &null_byte, 1)};
    if(socket_write < 1)
    {
      if(errno == EPIPE)
        ADD_FAILURE() << "The synchronization socket was found to be closed "
          "when signalling interface readiness in" << case_suffix_;
      else
        ADD_FAILURE() << "An error occurred while signalling interface "
          "in" << case_suffix_ << '\n' << std::strerror(errno);
      return;
    }
  }
  // Wait for the connection status report.
  std::vector<std::uint8_t> status_report(total_connections, 0U);
  // WARNING: writes directly to a vector buffer.
  std::size_t status_report_read {as_components::socket_functions::SocketRead(
    socket_pair_array_[0], status_report.data(), 
    total_connections)};
  if(status_report_read < std::size_t(total_connections))
  {
    if(errno == 0)
      ADD_FAILURE() << "The client process closed the synchronization socket "
        "before the status report could be read in" << case_suffix_;
    else
      ADD_FAILURE() << "An error occurred while reading the status report "
          "in" << case_suffix_ << '\n' << std::strerror(errno);
    return;
  }
  EXPECT_EQ(status_report, args_.expected_status);
  // Verify expected observable interface state.
  EXPECT_EQ(std::get<0>(inter_tuple_)->connection_count(), 
    args_.initial_connections);
  EXPECT_EQ(std::get<0>(inter_tuple_)->interface_status(), true);
  EXPECT_EQ(std::get<0>(inter_tuple_)->get_overload(), 
    (args_.overload_after == args_.initial_connections));

  // Reap the child process.
  int wait_info {};
  if(waitpid(fork_return, &wait_info, 0) == -1)
  {
    ADD_FAILURE() << "An error occurred when an attempt was made to reap "
      "the child process in" << case_suffix_ << '\n' << std::strerror(errno);
    return;
  }
  if(!WIFEXITED(wait_info))
  {
    ADD_FAILURE() << "The child process did not terminate normally in" 
      << case_suffix_;
    return;
  }
  if(WEXITSTATUS(wait_info) != EXIT_SUCCESS)
    ADD_FAILURE() << "The child process exited with an error in" 
      << case_suffix_;
  return;
}

} // namespace

// ConnectionAcceptanceAndRejection
//    This test examines the behavior of a newly-created FcgiServerInterface
// in relation to accepting and rejecting connections. No FastCGI requests
// are made of the interfaces constructed in this test.
//
// Examined properties:
// 1) Socket domain: AF_UNIX, AF_INET, and AF_INET6
// 2) Rejection of connections in excess of the limit set by the
//    max_connections constructor argument.
// 3) Rejection of connections when the interface was put into an overloaded
//    state.
// 4) Rejection of connections based on the presence of the address of the
//    client in the list of addresses given by FCGI_WEB_SERVER_ADDRS:
//    Both AF_INET and AF_INET6.
//
// Test cases:
// 1) max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_UNIX.
//    The second connection should be rejected.
// 2) max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
//    The second connection should be rejected.
// 3) max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET6.
//    The second connection should be rejected.
// 4) max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
//    The sixth connection should be rejected.
// 5) max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty, a previous
//    connection was made, and the interface was placed in an overloaded
//    state. The second connection should be rejected.
// 6) FCGI_WEB_SERVER_ADDRS contains the IPv4 loopback address 127.0.0.1.
//    A client with address 127.0.0.1 attempts to make a connection and it
//    succeeds. A client with address 127.0.0.2 attempts to make a connection
//    and it fails.
// 7) FCGI_WEB_SERVER_ADDRS contains the IPv6 loopback address fd00::1.
//    A client with IPv6 loopback address ::1 tries to make a connection
//    and it fails.
//
// Modules which testing depends on:
// 1) as_components::socket_functions::SocketRead
// 2) as_components::socker_functions::SocketWrite
//
// Modules whose testing depends on this module: none.
//
// Test side-effects relevant to other tests:
// SIGPIPE will be ignored. The default disposition is restored at the end
// of the test. Only non-fatal failures are used in the implementation
// of TestCaseRunner to ensure that restoration takes place.
TEST(FcgiServerInterface, ConnectionAcceptanceAndRejection)
{
  // Ensure that SIGALRM has its default disposition.
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGALRM,
    __LINE__));

  // Ignore SIGPIPE. The disposition will be inherited by the child produced
  // in the test.
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
    __LINE__));

  // Ensure that FCGI_WEB_SERVER_ADDRS has a fixed state (bound and empty).
  if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
  {
    FAIL() << "FCGI_WEB_SERVER_ADDRS could not be cleared."
      << '\n' << std::strerror(errno);
  }

  testing::FileDescriptorLeakChecker fdlc {};

  constexpr const char*const path {"/tmp/fcgi_si_test_UNIX_interface_socket"};

  // Case 1: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_UNIX.
  {
    struct ConnectionAcceptanceAndRejectionTestArguments args {};
    args.inter_args.domain          = AF_UNIX;
    args.inter_args.max_connections = 1;
    args.inter_args.max_requests    = 1;
    args.inter_args.app_status      = EXIT_FAILURE;
    args.inter_args.unix_path       = path;

    args.initial_connections        = 1;
    args.overload_after             = 5; // No overload.
    args.expected_status            = std::vector<std::uint8_t> {1, 0};
    args.test_case                  = 1;

   ConnectionAcceptanceAndRejectionTest test {std::move(args)};
   test.RunTest();
  }

  // Case 2: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
  {
    struct ConnectionAcceptanceAndRejectionTestArguments args {};
    args.inter_args.domain          = AF_INET;
    args.inter_args.max_connections = 1;
    args.inter_args.max_requests    = 1;
    args.inter_args.app_status      = EXIT_FAILURE;
    args.inter_args.unix_path       = path;

    args.initial_connections        = 1;
    args.overload_after             = 5; // No overload.
    args.expected_status            = std::vector<std::uint8_t> {1, 0};
    args.test_case                  = 2;

    ConnectionAcceptanceAndRejectionTest test {std::move(args)};
    test.RunTest();
  }

  // Case 3: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET6.
  {
    struct ConnectionAcceptanceAndRejectionTestArguments args {};
    args.inter_args.domain          = AF_INET6;
    args.inter_args.max_connections = 1;
    args.inter_args.max_requests    = 1;
    args.inter_args.app_status      = EXIT_FAILURE;
    args.inter_args.unix_path       = path;

    args.initial_connections        = 1;
    args.overload_after             = 5; // No overload.
    args.expected_status            = std::vector<std::uint8_t> {1, 0};
    args.test_case                  = 3;

    ConnectionAcceptanceAndRejectionTest test {std::move(args)};
    test.RunTest();
  }

  // Case 4: max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty.
  {
    struct ConnectionAcceptanceAndRejectionTestArguments args {};
    args.inter_args.domain          = AF_INET;
    args.inter_args.max_connections = 5;
    args.inter_args.max_requests    = 10;
    args.inter_args.app_status      = EXIT_FAILURE;
    args.inter_args.unix_path       = path;

    args.initial_connections        = 5;
    args.overload_after             = 10; // No overload.
    args.expected_status            = std::vector<std::uint8_t> {1,1,1,1,1,0};
    args.test_case                  = 4;

    ConnectionAcceptanceAndRejectionTest test {std::move(args)};
    test.RunTest();
  }
  
  // Case 5: max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty, a previous
  // connection was made, and the interface was placed in an overloaded state.
  {
    struct ConnectionAcceptanceAndRejectionTestArguments args {};
    args.inter_args.domain          = AF_INET;
    args.inter_args.max_connections = 5;
    args.inter_args.max_requests    = 10;
    args.inter_args.app_status      = EXIT_FAILURE;
    args.inter_args.unix_path       = path;

    args.initial_connections        = 1;
    args.overload_after             = 1; // Overload for connection 2. 
                                         // Overload seen.
    args.expected_status            = std::vector<std::uint8_t> {1,0};
    args.test_case                  = 5;

    ConnectionAcceptanceAndRejectionTest test {std::move(args)};
    test.RunTest();
  }

  // 6) FCGI_WEB_SERVER_ADDRS contains the IPv4 loopback address 127.0.0.1. 
  //    A client with address 127.0.0.1 attempts to make a connection and it
  //    succeeds. A client with address 127.0.0.2 attempts to make a connection
  //    and it fails.
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "127.0.0.1", 1) != -1)
    {
      struct ConnectionAcceptanceAndRejectionTestArguments args {};
      args.inter_args.domain          = AF_INET;
      args.inter_args.max_connections = 5;
      args.inter_args.max_requests    = 10;
      args.inter_args.app_status      = EXIT_FAILURE;
      args.inter_args.unix_path       = path;

      args.initial_connections        = 1;
      args.overload_after             = 5; // No overload.
      args.expected_status            = std::vector<std::uint8_t> {1,0};
      args.test_case                  = 6;

      ConnectionAcceptanceAndRejectionTest test {std::move(args)};
      test.RunTest();

      if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) == -1)
        ADD_FAILURE() << "The environment could not be restored by a call to "
          "setenv in case 6." << '\n' << std::strerror(errno);
    }
    else
      ADD_FAILURE() << "The environment could not be modified with by a call "
        "to setenv in case 6." << '\n' << std::strerror(errno);
  }

  // 7) FCGI_WEB_SERVER_ADDRS contains the IPv6 private address fd00::1.
  //    A client with IPv6 loopback address ::1 tries to make a connection
  //    and it fails.
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "fd00::1", 1) != -1)
    {
      struct ConnectionAcceptanceAndRejectionTestArguments args {};
      args.inter_args.domain          = AF_INET6;
      args.inter_args.max_connections = 100;
      args.inter_args.max_requests    = 1000;
      args.inter_args.app_status      = EXIT_FAILURE;
      args.inter_args.unix_path       = path;
      
      args.initial_connections        = 0;
      args.overload_after             = 10; // No overload.
      args.expected_status            = std::vector<std::uint8_t> {0};
      args.test_case                  = 7;

      ConnectionAcceptanceAndRejectionTest test {std::move(args)};
      test.RunTest();

      if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) == -1)
        ADD_FAILURE() << "The environment could not be restored by a call to "
          "setenv in case 7." << '\n' << std::strerror(errno);
    }
    else
      ADD_FAILURE() << "The environment could not be modified with by a call "
        "to setenv in case 7." << '\n' << std::strerror(errno);
  }
  
  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc,
    "ConnectionAcceptanceAndRejection", __LINE__);

  // Restore the default SIGPIPE disposition.
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGPIPE,
    __LINE__));
}

// FcgiRequestGeneration
// Test space discussion:
// Notions related to sequences of received records:
// 1) Partial record receipt:
//    a) FastCGI records must be transmitted as complete units. In the
//       discussion of the concurrent transmission of FastCGI request data
//       below, individual FastCGI records are the indivisible units of data
//       whose transmission on a connection cannot be "interrupted" by the
//       transmission of other data on that connection.
//    b) A read operation on a connection may block. When blocking occurs,
//       a record may be in an incomplete state. In this case, while the
//       connection is blocked, the record remains in an incomplete or
//       partially-received state.
// 2) Interleaving of records can occur with respect to several record
//    properties. For example, records on a single connection could be
//    interleaved with respect to record type but not with respect to
//    request identity.
// 3) Record subsequences: Record receipt on a given connection defines a
//    sequence of records S. We can imagine a subsequence T of records of
//    sequence S where T is defined as the sequence of all records of S
//    that possess a given property. For example, we can define the
//    subsequence of records with a given request identity.
//
// Examined properties:
// 1) Broadly, connection multiplexing: Will the interface correctly read
//    FastCGI records when multiple clients are concurrently sending them?
//    a) All records for one or more requests are received on a given
//       connection before a read from the connection would block. In other
//       words, requests are received as whole units across connections.
//    b) Records for requests are interleaved in the sense that periods of
//       read blocking interrupt record receipt for a request on a given
//       connection and records are received on another connection during
//       these periods.
// 2) Broadly, request multiplexing: will the interface correctly read
//    FastCGI records when records for multiple requests are being
//    concurrently sent over the same connection?
// 3) Intra-request record type order and interleaving: Will a request object
//    be constructed correctly for each of the many variations in the record
//    sequence which may be used to convey the data of the request? While the
//    order of record receipt for a given record type is an invariant of
//    correct record transmission, differences in the order of the
//    transmission of records of different types and the potential for the
//    interleaving of record types allows many possible variations in the
//    record sequence which is used to transmit the data of a request.
// 4) Stream record type data paritioning: The data associated with a stream
//    record type can be arbitrarily partitioned between records provided
//    that an empty record only occurs as the terminal record of the stream.
// 5) Padding: Most records may have padding. Padding may be present
//    regardless of whether the record is aligned on an 8-byte boundary or
//    not.
// 6) Partial request receipt: multiple cycles of data transmission and
//    data processing are required to receive the request data in full.
// 7) Partial record receipt and connection blocking when no other
//    connections are ready for reading.
// 8) The occurrence of partial record receipt when the connection of the
//    record would block and another connection is ready.
//
// Test cases:
// Single connection:
// Note: Unless specified otherwise, the FCGI_KEEP_CONN flag is not set in
// the FCGI_BEGIN_REQUEST record of a request.
// 1) Minimal requests: No data is present for any of the streams.
//    a) Role: Responder. FCGI_PARAMS and FCGI_STDIN are terminated with
//       empty records. No record of type FCGI_DATA is sent.
//    b) Role: Responder. As a, but the FCGI_KEEP_CONN flag is set.
//    c) Role: Responder. A terminal FCGI_DATA record is sent before the
//       request is completed given the special completion logic for the
//       Responder role. Terminal FCGI_PARAMS and FCGI_STDIN records are then
//       sent.
//    d) Role: Responder. As a, but a terminal FCGI_DATA record which should
//       be ignored is sent after the request is completed.
//    e) Role: Authorizer. FCGI_PARAMS is terminated with an empty record.
//       No other records are sent.
//    f) Role: Authorizer. FCGI_STDIN is terminated before FCGI_PARAMS. Then
//       a terminal FCGI_PARAMS records is sent.
//    g) Role: Authorizer. FCGI_PARAMS is terminated with an empty record.
//       Then a terminal FCGI_STDIN record which should be ignored is sent.
//    h) Role: Filter. All three data streams are terminated with empty
//       records. The FCGI_DATA record is sent after the other records.
//       A request object should not be generated until it is received
//       according to the specified request completion logic for FCGI_DATA.
//    i) Role: Unknown: The role field has value 10. Otherwise as h.
// 2) Partial request data receipt on a call of AcceptRequests.
//    a) Role: Responder. No partial records. Several cycles of request data
//       transmission by a client and data processing by the interface are
//       to needed receive the request.
//    b) Role: Responder. Partial records.
// 3) Single request with varying record type orderings: Records of
//    different types are not interleaved. Rather, the record type order is
//    varied across requests.
//    a) Role: Responder. Data is present for FCGI_PARAMS and absent for
//       FCGI_STDIN. No record with type FCGI_DATA is sent. The FCGI_PARAMS
//       records are sent first.
//    b) As a, but the completing, empty FCGI_STDIN record is sent first.
//    c) Role: Responder. Data is present for both FCGI_PARAMS and FCGI_STDIN.
//       No records of type FCGI_DATA are sent. The records for FCGI_PARAMS
//       are sent before those for FCGI_STDIN.
//    d) As c, but arbitrary amounts of padding are present in the records
//       of both streams.
//    e) As c, but a different partitioning of the data among records is used
//       for the streams.
//    f) As c, but the order of FCGI_PARAMS and FCGI_STDIN is switched.
//    g) Role: Filter. Data is present for all of the streams. Data is sent
//       in the order: FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA. keep_conn is
//       true.
//    h) As g, but the order is: FCGI_DATA, FCGI_PARAMS, FCGI_STDIN.
// 4) Single request with record type interleavings:
//    a) Role: Responder. Data is present for FCGI_PARAMS and FCGI_STDIN.
//       No records of type FCGI_DATA are sent. The records of FCGI_PARAMS
//       and FCGI_STDIN are interleaved before the streams are completed.
// 5) Multiple requests with record interleaving:
//    a) A Responder request, an Authorizer request, and a Filter request are
//       sent on the same connection. Records for the requests are
//       interleaved arbitrarily. "Partial records" in the sense that data
//       receipt is interrupted with periods where reading would block and
//       the current record was not received in full are present. As multiple
//       requests are present, keep_conn is true.
//
// Multiple connections:
// 1) (No interleaving of request data receipt between connections;
//    homogenous request type; single request on each connection.)
//    Five connections. A Responder request is sent on each connection to the
//    interface. Each request contains unique FCGI_PARAMS and FCGI_STDIN
//    data. Activity is synchronized such that all data for the requests is
//    sent to the interface before a call to AcceptRequests is made on the
//    interface. This means that a request should be received in full for
//    each connection before the interface moves on to the next connection.
// 2) (No interleaving of request data receipt between connections; mixed
//    request type; single request on each connection.)
//    Five connections: A mix of Responder, Authorizer, and Filter requests
//    are sent. A single request is sent on each connection. As in 1, the
//    data for each request is sent in full before the interface begins
//    processing the requests.
// 3) (No interleaving of request data receipt between connections;
//    homogenous request type; multiple requests on a connection.)
//    Ten connections. Responder requests are sent on each connection. As in
//    1, each request has unique FCGI_PARAMS and FCGI_STDIN data. However,
//    for at least one of the connections, multiple requests are sent on the
//    same connection. As in 1, all data for each request is sent before the
//    interface begins processing data.
// 4) (Interleaving of request data receipt between connections. Partial
//    records. Mixed request types. Multiple requests on a connection.)
//    Two connections. Multiple Responder requests are sent on one
//    connections. A Filter request is sent on the other connection. Request
//    data is sent with partial records. Multiple cylces of data
//    transmission and data processing are required. Multiple Responder
//    requests are sent on one of the connections.
//
// Modules which testing depends on:
//
// Other modules whose testing depends on this module:
TEST(FcgiServerInterface, FcgiRequestGeneration)
{
  // Type aliases, user-defined types, and lambda functions for general use
  // and the first case.
  using map_type = std::map<std::vector<std::uint8_t>, 
      std::vector<std::uint8_t>>;

  // A type to hold data which describes a FastCGI request.
  struct RequestData
  {
    std::uint16_t Fcgi_id;
    std::uint16_t role;
    std::map<std::vector<uint8_t>, std::vector<uint8_t>> fcgi_params;
    std::vector<std::uint8_t> fcgi_stdin;
    std::vector<std::uint8_t> fcgi_data;
    bool fcgi_keep_conn;
  };

  //    A test function which compares an FcgiRequest object with a
  // RequestData instance. This lambda is used to determine if the
  // FcgiRequest output of an FcgiServerInterface instance faithfully
  // represents the request which was sent to the instance.
  //    The message parameter provides an error message to Google Test
  // assertions.
  auto RequestInspector = [](const FcgiRequest& output, 
    const struct RequestData& input, const std::string& message)->void
  {
    EXPECT_EQ(output.get_request_identifier().Fcgi_id(), 
      input.Fcgi_id)                                           << message;
    EXPECT_EQ(output.get_role(), input.role)                   << message;
    EXPECT_EQ(output.get_environment_map(), input.fcgi_params) << message;
    EXPECT_EQ(output.get_STDIN(), input.fcgi_stdin)            << message;
    EXPECT_EQ(output.get_DATA(), input.fcgi_data)              << message;
    EXPECT_EQ(output.get_keep_conn(), input.fcgi_keep_conn)    << message;
  };

  // A lambda used with class
  // GTestNonFatalSingleProcessInterfaceAndClients to accept
  // requests and move the output FcgiRequest instances to a vector of such.
  auto AcceptAndAddRequests = []
  (
    GTestNonFatalSingleProcessInterfaceAndClients* spiac_ptr,
    std::vector<FcgiRequest>* request_list_ptr
  )->void
  {
    std::vector<FcgiRequest> new_requests 
      {spiac_ptr->interface().AcceptRequests()};
    request_list_ptr->insert(
      request_list_ptr->end(), 
      std::move_iterator<std::vector<FcgiRequest>::iterator>(
        new_requests.begin()), 
      std::move_iterator<std::vector<FcgiRequest>::iterator>(
        new_requests.end()));
  };

  testing::FileDescriptorLeakChecker fdlc {};
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
    __LINE__));

                    // Single connection test cases.

         //
         // Single connection Test Case Set 1: Minimal requests
         //

  // This lambda is used in the implementation of the minimal request cases.
  // It creates an interface and a client. It then iteratively writes a record
  // to the interface through the client and calls AcceptRequests on the
  // interface after each record write. Record information is given by pairs.
  // When AcceptRequests returns, it checks if a request was generated or not
  // and compares the generation status to the expected status as determined by
  // request_acceptance. If a request was generated as expected, it verifies
  // the data of the request.
  using RecordPair = std::pair<const std::uint8_t*, std::size_t>;
  auto WriteAndAccept = [&RequestInspector, &AcceptAndAddRequests]
  (
    const struct RequestData&      request_data,
    const std::vector<RecordPair>& pairs,
    const std::vector<bool>&       request_acceptance,
    const std::string&             case_message
  )->void
  {
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 100;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 1, __LINE__};
    std::size_t pairs_size {pairs.size()};
    if(pairs_size != request_acceptance.size())
    {
      ADD_FAILURE() << "A mismatch in the sizes of pairs and "
        "request_acceptance was found in " << case_message;
      return;
    }
    // The vector requests cannot be in the loop. If it was located within the
    // loop, the FcgiRequest objects which it holds will be destroyed
    // when the loop iterates and requests is destroyed. if FCGI_KEEP_CONN was
    // not set for these requests, the interface will close its connection
    // for the request as the requests will not have been completed.
    //
    // Note that, if the connection was closed by the interface, a write on the
    // client socket derscriptor will succeed. This unusual behavior prevents
    // connection closure detection through errno == EPIPE. In this case,
    // a subsequent call to AcceptRequests will block as no data will be
    // received by the interface.
    std::vector<FcgiRequest> requests {};
    for(std::size_t i {0U}; i != pairs_size; ++i)
    {
      std::size_t previous_size {requests.size()};
      if(as_components::socket_functions::SocketWrite(
        spiac.client_descriptors().at(0U), pairs[i].first, pairs[i].second)
        < pairs[i].second)
      {
        ADD_FAILURE() << "A part of the request could not be written in full "
          "in " << case_message << '\n' << std::strerror(errno);
        return;
      }
      EXPECT_NO_THROW(AcceptAndAddRequests(&spiac, &requests)) << case_message;
      std::size_t current_size {requests.size()};
      bool new_requests {current_size > previous_size};
      if(( request_acceptance[i] && !new_requests) ||
         (!request_acceptance[i] &&  new_requests))
      {
        ADD_FAILURE() << "Production of an FcgiRequest object did not occur "
          "as expected in " << case_message;
        return;
      }
      if(request_acceptance[i] && new_requests)
      {
        RequestInspector(requests.back(), request_data, case_message);
      }
    }
  };

  // This lambda creates a sequence of RecordPair instances and populates
  // an associated buffer with FastCGI record data.
  //
  // It is assumed that an FCGI_BEGIN_REQUEST record is always first.
  auto MinimalRecordPairCreator = []
  (
    std::vector<std::uint8_t>*            record_list_ptr,
    const std::vector<FcgiType>& type_list,
    const RequestData&                    request_data
  )->std::vector<RecordPair>
  {
    if(!record_list_ptr)
    {
      throw std::logic_error {"A null pointer was present in a call to "
        "PopulateMinimalRecords."};
    }
    if((FCGI_HEADER_LEN * (2U + type_list.size())) >
       record_list_ptr->size())
    {
      throw std::logic_error {"record_list_ptr length mismatch in a call to "
        "PopulateMinimalRecords."};
    }
    std::vector<RecordPair> pair_list {};
    std::uint8_t* buffer_ptr {record_list_ptr->data()};
    PopulateBeginRequestRecord(buffer_ptr, request_data.Fcgi_id,
      request_data.role, request_data.fcgi_keep_conn);
    pair_list.push_back({buffer_ptr, 2U * FCGI_HEADER_LEN});
    buffer_ptr += (2U * FCGI_HEADER_LEN);
    for(auto i {type_list.begin()}; i != type_list.end(); ++i)
    {
      PopulateHeader(buffer_ptr, *i, 1U, 0U, 0U);
      pair_list.push_back({buffer_ptr, FCGI_HEADER_LEN});
      buffer_ptr += FCGI_HEADER_LEN;
    }
    return pair_list;
  };

  // a) Role: Responder; FCGI_PARAMS and FCGI_STDIN are terminated with
  //    empty records. No record of type FCGI_DATA is sent.
  {
    std::string case_message {"Test Case Set 1, test case a."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_PARAMS (1), and FCGI_STDIN (1) => 4.
    std::vector<std::uint8_t> request_records(4U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // b) Role: Responder. As a, but the FCGI_KEEP_CONN flag is set.
  {
    std::string case_message {"Test Case Set 1, test case b."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_keep_conn = true;

    // Begin request (2), FCGI_PARAMS (1), and FCGI_STDIN (1) => 4.
    std::vector<std::uint8_t> request_records(4U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN};  
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // c) Role: Responder. A terminal FCGI_DATA record is sent before the
  //    request is completed given the special completion logic for the
  //    Responder role. Terminal FCGI_PARAMS and FCGI_STDIN records are then
  //    sent.
  {
    std::string case_message {"Test Case Set 1, test case c."};
    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_DATA (1), FCGI_PARAMS (1), FCGI_STDIN (1) => 5.
    std::vector<std::uint8_t> request_records(5U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_DATA,
      FcgiType::kFCGI_PARAMS, FcgiType::kFCGI_STDIN};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // d) Role: Responder. As a, but a terminal FCGI_DATA record which should
  //    be ignored is sent after the request is completed.
  {
   std::string case_message {"Test Case Set 1, test case d."};
    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_PARAMS (1), FCGI_STDIN (1), and FCGI_DATA (1)
    // => 5.
    std::vector<uint8_t> request_records(5U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN, FcgiType::kFCGI_DATA};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, true, false};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // e) Role: Authorizer. FCGI_PARAMS is terminated with an empty record.
  //    No other records are sent.
  {
    std::string case_message {"Test Case Set 1, test case e."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_AUTHORIZER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2) and FCGI_PARAMS (1) => 3.
    std::vector<uint8_t> request_records(3U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // f) Role: Authorizer. FCGI_STDIN is terminated before FCGI_PARAMS. Then
  //    a terminal FCGI_PARAMS records is sent.
  {
     std::string case_message {"Test Case Set 1, test case f."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_AUTHORIZER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_STDIN (1), FCGI_PARAMS (1) => 4.
    std::vector<uint8_t> request_records(4U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_STDIN,
      FcgiType::kFCGI_PARAMS};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // g) Role: Authorizer. FCGI_PARAMS is terminated with an empty record.
  //    Then a terminal FCGI_STDIN record which should be ignored is sent.
  {
    std::string case_message {"Test Case Set 1, test case g."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_AUTHORIZER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_PARAMS (1), and FCGI_STDIN (1) => 4.
    std::vector<uint8_t> request_records(4U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, true, false};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // h) Role: Filter. All three data streams are terminated with empty
  //    records. The FCGI_DATA record is sent after the other records.
  //    A request object should not be generated until it is received
  //    according to the specified request completion logic for FCGI_DATA.
  {
    std::string case_message {"Test Case Set 1, test case h."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_FILTER;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_PARAMS (1), FCGI_STDIN (1), and FCGI_DATA (1)
    // => 5.
    std::vector<uint8_t> request_records(5U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN, FcgiType::kFCGI_DATA};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }

  // i) Role: Unknown: The role field has value 10. Otherwise as h.
  {
    std::string case_message {"Test Case Set 1, test case i."};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = 10U;
    request_data.fcgi_keep_conn = false;

    // Begin request (2), FCGI_PARAMS (1), FCGI_STDIN (1), and FCGI_DATA (1)
    // => 5.
    std::vector<uint8_t> request_records(5U * FCGI_HEADER_LEN);
    std::vector<FcgiType> type_list {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN, FcgiType::kFCGI_DATA};
    std::vector<RecordPair> record_info {MinimalRecordPairCreator(
      &request_records, type_list, request_data)};
    std::vector<bool> acceptance_info {false, false, false, true};
    WriteAndAccept(request_data, record_info, acceptance_info, case_message);
  }
                  //
                  // Single connection Test Case Set 2
                  //

  // Partial request data receipt on a call of AcceptRequests.

  // a) Role: Responder. No partial records. Several cycles of request data
  //    transmission by a client and data processing by the interface are
  //    needed to receive the request.
  do 
  { 
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 10;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;   
    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 1, __LINE__};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.fcgi_keep_conn = false;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_params    = map_type 
    {
      {
        {'C','O','N','T','E','N','T','_','T','Y','P','E'}, 
        {'t','e','x','t','/','h','t','m','l'}
      }, 
      {
        {'C','O','N','T','E','N','T','_','L','E','N','G','T','H'}, 
        {'3','1'}
      }
    };
    request_data.fcgi_stdin     = std::vector<std::uint8_t>
    {
      '<','!','D','O','C','T','Y','P','E',' ','h','t','m','l','>','\n',
      '<','h','t','m','l','>','\n',
      '<','/','h','t','m','l','>','\n'
    };

    const char* accept_error {"FcgiRequest objects were returned when none was "
      "expected."};
    std::uint8_t begin_record[2 * FCGI_HEADER_LEN]      = {};
    PopulateBeginRequestRecord(begin_record, request_data.Fcgi_id,
      request_data.role, request_data.fcgi_keep_conn);
    std::uint8_t terminal_params_record[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_params_record,
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U, 0U);
    std::uint8_t terminal_stdin_record[FCGI_HEADER_LEN]  = {};
    PopulateHeader(terminal_stdin_record,
      FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 0U, 0U);

    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0], 
      begin_record, 2 * FCGI_HEADER_LEN) 
      < 2 * FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "Incomplete begin request record write." << '\n'
        << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    };
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::iterator>
    pair_encoding_return 
      {EncodeNameValuePairs(request_data.fcgi_params.begin(),
        request_data.fcgi_params.end(), FcgiType::kFCGI_PARAMS,
        request_data.Fcgi_id, 0U)};
    if(EncodeNVPairSingleRecordFailure(pair_encoding_return,
      request_data.fcgi_params.end()))
    {
      ADD_FAILURE() << "An error occurred while encoding the name-value pairs.";
      break;
    }
    std::tuple<struct iovec*, int, std::size_t> sgsw_return 
      {as_components::socket_functions::ScatterGatherSocketWrite(
        spiac.client_descriptors()[0],
        std::get<2>(pair_encoding_return).data(), 
        std::get<2>(pair_encoding_return).size(), 
        std::get<1>(pair_encoding_return))};
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE() << "Not all of the encoded name-value pair information "
        "could be written." << '\n' << std::strerror(errno);
      break;
    }
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0], 
      terminal_params_record, FCGI_HEADER_LEN) != 
        FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "Incomplete terminal params record write." << '\n'
        << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    }
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::iterator>
    partition_return 
      {PartitionByteSequence(request_data.fcgi_stdin.begin(),
        request_data.fcgi_stdin.end(), FcgiType::kFCGI_STDIN,
        request_data.Fcgi_id)};
    if(std::get<3>(partition_return) != request_data.fcgi_stdin.end())
    {
      ADD_FAILURE() << "Not all of the stdin data could be encoded.";
      break;
    }
    std::tuple<struct iovec*, int, std::size_t> stdin_sgsw_return
      {as_components::socket_functions::ScatterGatherSocketWrite(
        spiac.client_descriptors()[0],
        std::get<1>(partition_return).data(), 
        std::get<1>(partition_return).size(), std::get<2>(partition_return))};
    if(std::get<2>(stdin_sgsw_return) != 0U)
    {
      ADD_FAILURE() << "An error occurred when writing stdin content."
        << '\n' << std::strerror(errno);
      break;
    }
    if(as_components::socket_functions::SocketWrite(
      spiac.client_descriptors()[0],
      terminal_stdin_record, FCGI_HEADER_LEN) < 
      FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "The terminal stdin record was not sent in full."
        << '\n' << std::strerror(errno);
      break;
    }
    std::vector<FcgiRequest> request_list 
      {spiac.interface().AcceptRequests()};
    if(request_list.size() != 1U)
    {
      ADD_FAILURE() << "An incorrect number of requests was returned.";
      break;
    }
    RequestInspector(request_list[0], request_data, "Single connection "
      "partial request data receipt");
    // Inspect observable interface state.
    EXPECT_EQ(spiac.interface().connection_count(), 1U);
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);

  // b) Role: Responder. Partial records.
  do 
  { 
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 10;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;   
    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 1, __LINE__};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.fcgi_keep_conn = false;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_params    = map_type 
    {
      {
        {'C','O','N','T','E','N','T','_','T','Y','P','E'}, 
        {'t','e','x','t','/','h','t','m','l'}
      }, 
      {
        {'C','O','N','T','E','N','T','_','L','E','N','G','T','H'}, 
        {'3','1'}
      }
    };
    request_data.fcgi_stdin     = std::vector<std::uint8_t>
    {
      '<','!','D','O','C','T','Y','P','E',' ','h','t','m','l','>','\n',
      '<','h','t','m','l','>','\n',
      '<','/','h','t','m','l','>','\n'
    };

    const char* accept_error {"FcgiRequest objects were returned when none was "
      "expected."};
    std::uint8_t begin_record[2 * FCGI_HEADER_LEN]      = {};
    PopulateBeginRequestRecord(begin_record, request_data.Fcgi_id,
      request_data.role, request_data.fcgi_keep_conn);
    std::uint8_t terminal_params_record[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_params_record,
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U, 0U);
    std::uint8_t terminal_stdin_record[FCGI_HEADER_LEN]  = {};
    PopulateHeader(terminal_stdin_record,
      FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 0U, 0U);
    
    // Write the FCGI_BEGIN_REQUEST record. 
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0], 
      begin_record, 3U) < 3U)
    {
      ADD_FAILURE() << "Incomplete begin request record write, first fragment."
        << '\n' << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    };
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0], 
      begin_record + 3U, (2*FCGI_HEADER_LEN) - 3U) < 
      ((2*FCGI_HEADER_LEN) - 3U))
    {
      ADD_FAILURE() << "Incomplete begin request record write, second "
        "fragment." << '\n' << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    };
  
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::iterator>
    pair_encoding_return
      {EncodeNameValuePairs(request_data.fcgi_params.begin(),
        request_data.fcgi_params.end(), FcgiType::kFCGI_PARAMS,
        request_data.Fcgi_id, 0U)};
    if(EncodeNVPairSingleRecordFailure(pair_encoding_return,
      request_data.fcgi_params.end()))
    {
      ADD_FAILURE() << "An error occurred while encoding the name-value pairs.";
      break;
    }
    // Write the FCGI_PARAMS record in two parts with an intervening call to
    // AcceptRequests.
      // integer division
    std::size_t partial_number {std::get<2>(pair_encoding_return).size() / 2};
    if(partial_number == 0U)
      partial_number = 1U;
    std::size_t partial_byte_count {0U};
    for(std::size_t i {0U}; i < partial_number; ++i)
      partial_byte_count += std::get<2>(pair_encoding_return)[i].iov_len;
    std::tuple<struct iovec*, int, std::size_t> sgsw_return 
      {as_components::socket_functions::ScatterGatherSocketWrite(
        spiac.client_descriptors()[0],
        std::get<2>(pair_encoding_return).data(), 
        partial_number, partial_byte_count)};
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE() << "Not all of the encoded name-value pair information "
        "could be written for the first fragment." << '\n' 
        << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    }
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[0], 
      std::get<2>(pair_encoding_return).data() + partial_number, 
      std::get<2>(pair_encoding_return).size() - partial_number, 
      std::get<1>(pair_encoding_return) - partial_byte_count);
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE() << "Not all of the encoded name-value pair information "
        "could be written for the second fragment." << '\n' 
        << std::strerror(errno);
      break;
    }
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0], 
      terminal_params_record, FCGI_HEADER_LEN) != 
        FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << "Incomplete terminal params record write." << '\n'
        << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    }
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::iterator>
    partition_return 
      {PartitionByteSequence(request_data.fcgi_stdin.begin(),
        request_data.fcgi_stdin.end(), FcgiType::kFCGI_STDIN,
        request_data.Fcgi_id)};
    if(std::get<3>(partition_return) != request_data.fcgi_stdin.end())
    {
      ADD_FAILURE() << "Not all of the stdin data could be encoded.";
      break;
    }
    std::tuple<struct iovec*, int, std::size_t> stdin_sgsw_return
      {as_components::socket_functions::ScatterGatherSocketWrite(
        spiac.client_descriptors()[0],
        std::get<1>(partition_return).data(), 
        std::get<1>(partition_return).size(), std::get<2>(partition_return))};
    if(std::get<2>(stdin_sgsw_return) != 0U)
    {
      ADD_FAILURE() << "An error occurred when writing stdin content." << '\n'
        << std::strerror(errno);
      break;
    }
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      terminal_stdin_record, 4U) < 4U)
    {
      ADD_FAILURE() << "The first fragment of the terminal stdin record was "
        "not sent in full." << '\n' << std::strerror(errno);
      break;
    }
    if(spiac.interface().AcceptRequests().size() != 0U)
    {
      ADD_FAILURE() << accept_error;
      break;
    }
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      terminal_stdin_record + 4U, FCGI_HEADER_LEN - 4U) < 
      (FCGI_HEADER_LEN - 4U))
    {
      ADD_FAILURE() << "The second fragment of the terminal stdin record was "
        "not sent in full." << '\n' << std::strerror(errno);
      break;
    }
    std::vector<FcgiRequest> request_list 
      {spiac.interface().AcceptRequests()};
    if(request_list.size() != 1U)
    {
      ADD_FAILURE() << "An incorrect number of requests was returned.";
      break;
    }
    RequestInspector(request_list[0], request_data, "Single connection "
      "partial request data receipt");
    // Inspect observable interface state.
    EXPECT_EQ(spiac.interface().connection_count(), 1U);
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);
                  //
                  // Single connection Test Case Set 3
                  //

  // Single requests with varying record type orderings: Records of different
  // types are not interleaved. Rather, the record type order is varied across
  // requests.

  auto SingleClientRecordWriterAndTester = [&RequestInspector]
  (
    GTestNonFatalSingleProcessInterfaceAndClients* spiac_ptr,
    const struct RequestData& request_data,
    const std::vector<std::pair<const std::uint8_t*, std::size_t>>& write_pairs,
    const std::string& test_case_name
  )->void
  {
    int write_count {0};
    for(std::pair<const std::uint8_t*, std::size_t> write_pair : write_pairs)
    {
      if(as_components::socket_functions::SocketWrite(
        spiac_ptr->client_descriptors()[0], write_pair.first, write_pair.second)
        < write_pair.second)
      {
        ADD_FAILURE() << "An error occurred while writing the request."
          << '\n' << std::strerror(errno);
        break;
      }
      write_count++;
    }
    if(write_count < 3)
      return;

    std::vector<FcgiRequest> request_list 
      {spiac_ptr->interface().AcceptRequests()};
    if(request_list.size() != 1U)
    {
      ADD_FAILURE() << "An unexpected number of requests was returned."
        << '\n' << request_list.size();
      return;
    }
    RequestInspector(request_list[0], request_data,
      test_case_name);
    EXPECT_EQ(spiac_ptr->interface().connection_count(), 1U);
    EXPECT_EQ(spiac_ptr->interface().interface_status(), true);
    EXPECT_EQ(spiac_ptr->interface().get_overload(), false);
  };

  auto RecordTypeOrderTester = [&RequestInspector]
  (const struct InterfaceCreationArguments& inter_args,
   const struct RequestData& request_data, 
   const std::vector<FcgiType>& type_sequence, 
   const std::string& test_case_name)->void
  {
    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 1, __LINE__};

    // Populate the FCGI_BEGIN_REQUEST record.
    std::uint8_t begin_record[2 * FCGI_HEADER_LEN] = {};
    PopulateBeginRequestRecord(begin_record, request_data.Fcgi_id,
      request_data.role, request_data.fcgi_keep_conn);

    // Populate the the FCGI_PARAMS records.
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::const_iterator>
    encoded_pairs_return {EncodeNameValuePairs(
      request_data.fcgi_params.cbegin(), request_data.fcgi_params.cend(),
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U)};
    if(EncodeNVPairSingleRecordFailure(encoded_pairs_return,
      request_data.fcgi_params.cend()))
    {
      ADD_FAILURE() << "An error occurred while encoding FCGI_PARAMS data in "
        << test_case_name;
      return;
    }
    std::uint8_t terminal_params[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_params, FcgiType::kFCGI_PARAMS,
      request_data.Fcgi_id, 0U, 0U);
    
    // Populate the FCGI_STDIN records.
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::const_iterator>
    encoded_stdin_return {PartitionByteSequence(
      request_data.fcgi_stdin.cbegin(), request_data.fcgi_stdin.cend(),
      FcgiType::kFCGI_STDIN, request_data.Fcgi_id)};
    if(std::get<3>(encoded_stdin_return) != request_data.fcgi_stdin.cend())
    {
      ADD_FAILURE() << "Not all of fcgi_stdin could be encoded in "
        << test_case_name;
      return;
    }
    std::uint8_t terminal_stdin[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_stdin, FcgiType::kFCGI_STDIN,
      request_data.Fcgi_id, 0U, 0U);

    // Populate the FCGI_DATA records.
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::const_iterator>
    encoded_data_return {PartitionByteSequence(
      request_data.fcgi_data.cbegin(), request_data.fcgi_data.cend(),
      FcgiType::kFCGI_DATA, request_data.Fcgi_id)};
    if(std::get<3>(encoded_data_return) != request_data.fcgi_data.cend())
    {
      ADD_FAILURE() << "Not all of fcgi_data could be encoded in "
        << test_case_name;
      return;
    }
    std::uint8_t terminal_data[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_data, FcgiType::kFCGI_DATA,
      request_data.Fcgi_id, 0U, 0U);

    // Write the begin record.
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      begin_record, 2 * FCGI_HEADER_LEN) < 
      (2 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "An error occurred while writing the FCGI_BEGIN_REQUEST "
        "record in " << test_case_name << '\n' << std::strerror(errno);
      return;
    }

    // FCGI_STDIN and FCGI_DATA record writer
    using partition_return_type = 
      std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::const_iterator>;
    auto StdinDataWriter = [&test_case_name, &spiac]
    (
      partition_return_type* part_return_ptr,
      const std::uint8_t* terminal_buffer_ptr,
      const std::string& type
    )->void
    {
      std::tuple<struct iovec*, int, std::size_t> sgsw_return 
        {as_components::socket_functions::ScatterGatherSocketWrite(
          spiac.client_descriptors()[0], 
          std::get<1>(*part_return_ptr).data(),
          std::get<1>(*part_return_ptr).size(),
          std::get<2>(*part_return_ptr))};
      if(std::get<2>(sgsw_return) != 0)
      {
        ADD_FAILURE() << "The " << type << " content was not written in full "
          "in " << test_case_name << '\n' << std::strerror(errno);
        return;
      }
      if(as_components::socket_functions::SocketWrite(
        spiac.client_descriptors()[0], terminal_buffer_ptr,
        FCGI_HEADER_LEN) < FCGI_HEADER_LEN)
      {
        ADD_FAILURE() << "The terminal FCGI_STDIN record could not be "
          "written in full in " << test_case_name << '\n'
          << std::strerror(errno);
        return;
      }
    };

    for(FcgiType type : type_sequence)
    {
      switch(type) {
        case FcgiType::kFCGI_PARAMS : {
          std::tuple<struct iovec*, int, std::size_t> sgsw_return
            {as_components::socket_functions::ScatterGatherSocketWrite(
              spiac.client_descriptors()[0],
              std::get<2>(encoded_pairs_return).data(),
              std::get<2>(encoded_pairs_return).size(),
              std::get<1>(encoded_pairs_return))};
          if(std::get<2>(sgsw_return) != 0U)
          {
            ADD_FAILURE() << "An error occurred when writing FCGI_PARAMS "
              "content in " << test_case_name << '\n' << std::strerror(errno);
            return;
          }
          if(as_components::socket_functions::SocketWrite(
            spiac.client_descriptors()[0], terminal_params,
            FCGI_HEADER_LEN) < FCGI_HEADER_LEN)
          {
            ADD_FAILURE() << "The terminal FCGI_PARAMS record was not sent in "
              "full in " << test_case_name << '\n' << std::strerror(errno);
            return;
          }
          break;
        }
        case FcgiType::kFCGI_STDIN : {
          StdinDataWriter(&encoded_stdin_return, terminal_stdin, "FCGI_STDIN");
          break;
        }
        case FcgiType::kFCGI_DATA : {
          StdinDataWriter(&encoded_data_return, terminal_data, "FCGI_DATA");
          break;
        }
        default : {
          ADD_FAILURE() << "An incorrect type was passed in the type sequence "
          "in " << test_case_name;
          return;
        }
      }
    }

    std::vector<FcgiRequest> request_list 
      {spiac.interface().AcceptRequests()};
    if(request_list.size() != 1U)
    {
      ADD_FAILURE() << "An unexpected number of FcgiRequest objects was "
        "returned in " << test_case_name << '\n' << request_list.size();
      return;
    }
    RequestInspector(request_list[0], request_data, test_case_name);
    EXPECT_EQ(spiac.interface().connection_count(), 1U);
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  };

  // a) Role: Responder. Data is present for FCGI_PARAMS and absent for
  //    FCGI_STDIN. No record with type FCGI_DATA is sent. The FCGI_PARAMS
  //    records are sent first.
  // b) As a, but the completing, empty FCGI_STDIN record is sent first.
  {
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET6;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 1;
    inter_args.app_status      = EXIT_FAILURE ;
    inter_args.unix_path       = nullptr;
    
    struct RequestData request_data {};
    request_data.role           = FCGI_RESPONDER;
    request_data.Fcgi_id        = 1U;
    request_data.fcgi_keep_conn = false;
    request_data.fcgi_params    = map_type 
    {
      {
        {'F','C','G','I','_','V','E','R','S','I','O','N','_','1'},
        {'1'},
      }
    };
    // request_data.fcgi_stdin is empty (value initialization)
    // request_data.fcgi_data is empty (value initialization)
  
    RecordTypeOrderTester(inter_args, request_data, 
      std::vector<FcgiType> {FcgiType::kFCGI_PARAMS, 
        FcgiType::kFCGI_STDIN}, "Record Type Order case a");

     RecordTypeOrderTester(inter_args, request_data, 
      std::vector<FcgiType> {FcgiType::kFCGI_STDIN,
        FcgiType::kFCGI_PARAMS}, "Record Type Order case b");
  }

  // c) Role: Responder. Data is present for both FCGI_PARAMS and FCGI_STDIN.
  //    No records of type FCGI_DATA are sent. The records for FCGI_PARAMS
  //    are sent before those for FCGI_STDIN.
  // d) As c, but arbitrary amounts of padding are present in the records
  //    of both streams.
  // e) As c, but a different partitioning of the data among records is used
  //    for the streams.
  // f) As c, but the order of FCGI_PARAMS and FCGI_STDIN is switched.
  {
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_UNIX;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 1;
    inter_args.app_status      = EXIT_FAILURE ;
    inter_args.unix_path       = "/tmp/fcgi_si_Single_Connection_Test_Case_Set"
      "_3_test_case_c";
    
    struct RequestData request_data {};
    request_data.role           = FCGI_RESPONDER;
    request_data.Fcgi_id        = 1U;
    request_data.fcgi_keep_conn = false;
    request_data.fcgi_params    = map_type 
    {
      {
        {'A'},
        {'1'},
      },
      {
        {'B'},
        {'2'}
      }
    };
    request_data.fcgi_stdin     = std::vector<std::uint8_t> {1,1,0,1}; 
    // request_data.fcgi_data is empty (value initialization)

    RecordTypeOrderTester(inter_args, request_data, 
      std::vector<FcgiType> {FcgiType::kFCGI_PARAMS,
        FcgiType::kFCGI_STDIN}, "Record Type Order case c");

    RecordTypeOrderTester(inter_args, request_data, 
      std::vector<FcgiType> {FcgiType::kFCGI_STDIN,
        FcgiType::kFCGI_PARAMS}, "Record Type Order case f");

    // Case d
    do
    {
      GTestNonFatalSingleProcessInterfaceAndClients spiac
        {inter_args, 1, __LINE__};

      constexpr std::size_t begin_length {2 * FCGI_HEADER_LEN};
      std::uint8_t begin_record[begin_length] = {};
      PopulateBeginRequestRecord(begin_record, request_data.Fcgi_id,
        request_data.role, request_data.fcgi_keep_conn);
      
      // The below record encoding is specific to the value of
      // request_data.fcgi_params.
      //
      // The content length value (8U) is derived from the value of
      // request_data.fcgi_params under the application of the FastCGI name-
      // value pair encoding format.
      // The padding length is arbitrary.
      constexpr std::size_t params_length {(3 * FCGI_HEADER_LEN) + 7};
      std::uint8_t params_record[params_length] = {};
      PopulateHeader(params_record, FcgiType::kFCGI_PARAMS,
        request_data.Fcgi_id, 8U, 7U);
      params_record[FCGI_HEADER_LEN]     = 1U;
      params_record[FCGI_HEADER_LEN + 1] = 1U;
      params_record[FCGI_HEADER_LEN + 2] = 'A';
      params_record[FCGI_HEADER_LEN + 3] = '1';
      params_record[FCGI_HEADER_LEN + 4] = 1U;
      params_record[FCGI_HEADER_LEN + 5] = 1U;
      params_record[FCGI_HEADER_LEN + 6] = 'B';
      params_record[FCGI_HEADER_LEN + 7] = '2';
      PopulateHeader(params_record + (2*FCGI_HEADER_LEN) + 7,
        FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U, 0U);

      // The below record encoding is specific to the value of
      // request_data.fcgi_stdin.
      //
      // The padding length is arbitrary.
      constexpr std::size_t stdin_length 
        {(2 * FCGI_HEADER_LEN) + 4 + 2};
      std::uint8_t stdin_record[stdin_length] = {};
      PopulateHeader(stdin_record, FcgiType::kFCGI_STDIN,
        request_data.Fcgi_id, 4U, 2U);
      stdin_record[FCGI_HEADER_LEN]     = 1U;
      stdin_record[FCGI_HEADER_LEN + 1] = 1U;
      stdin_record[FCGI_HEADER_LEN + 2] = 0U;
      stdin_record[FCGI_HEADER_LEN + 3] = 1U;
      PopulateHeader(stdin_record + FCGI_HEADER_LEN + 4 + 2,
        FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 0U, 0U);

      std::vector<std::pair<const std::uint8_t*, std::size_t>> write_pairs
      {
        {begin_record, begin_length},
        {params_record, params_length},
        {stdin_record, stdin_length}
      };
     SingleClientRecordWriterAndTester(&spiac, request_data, write_pairs,
      "Single Connection Test Case Set 3 Type Orderings Test Case d");
    } while(false);
    
    // Case e
    do
    {
      GTestNonFatalSingleProcessInterfaceAndClients spiac
        {inter_args, 1, __LINE__};

      constexpr std::size_t begin_length {2 * FCGI_HEADER_LEN};
      std::uint8_t begin_record[begin_length] = {};
      PopulateBeginRequestRecord(begin_record, request_data.Fcgi_id,
        request_data.role, request_data.fcgi_keep_conn);

      constexpr std::size_t params_length {4 * FCGI_HEADER_LEN};
      std::uint8_t params_record[params_length] = {};
      PopulateHeader(params_record,
        FcgiType::kFCGI_PARAMS, request_data.Fcgi_id,
        2, 0U);
      params_record[FCGI_HEADER_LEN]           = 1;
      params_record[FCGI_HEADER_LEN + 1]       = 1;
      PopulateHeader(params_record + FCGI_HEADER_LEN + 2,
        FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 6U, 0U);
      params_record[(2 * FCGI_HEADER_LEN) + 2] = 'A';
      params_record[(2 * FCGI_HEADER_LEN) + 3] = '1';
      params_record[(2 * FCGI_HEADER_LEN) + 4] = 1U;
      params_record[(2 * FCGI_HEADER_LEN) + 5] = 1U;
      params_record[(2 * FCGI_HEADER_LEN) + 6] = 'B';
      params_record[(2 * FCGI_HEADER_LEN) + 7] = '2';
      PopulateHeader(params_record + (3 * FCGI_HEADER_LEN),
        FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U, 0U);
      
      constexpr std::size_t stdin_length {(3 * FCGI_HEADER_LEN) + 4};
      std::uint8_t stdin_record[stdin_length] = {};
      PopulateHeader(stdin_record, FcgiType::kFCGI_STDIN,
        request_data.Fcgi_id, 1U, 0U);
      stdin_record[FCGI_HEADER_LEN] = 1U;
      PopulateHeader(stdin_record + FCGI_HEADER_LEN + 1,
        FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 3U, 0U);
      stdin_record[(2 * FCGI_HEADER_LEN) + 1] = 1U;
      stdin_record[(2 * FCGI_HEADER_LEN) + 2] = 0U;
      stdin_record[(2 * FCGI_HEADER_LEN) + 3] = 1U;
      PopulateHeader(stdin_record + (2 * FCGI_HEADER_LEN) + 4,
        FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 0, 0U);

      std::vector<std::pair<const std::uint8_t*, std::size_t>> write_pairs
      {
        {begin_record, begin_length},
        {params_record, params_length},
        {stdin_record, stdin_length}
      };
      SingleClientRecordWriterAndTester(&spiac, request_data, write_pairs,
        "Single Connection Test Case Set 3 Type Orderings test case e");
    } while(false); 
  }

  // g) Role: Filter. Data is present for all of the streams. Data is sent
  //    in the order: FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA. keep_conn is true.
  // h) As g, but the order is: FCGI_DATA, FCGI_PARAMS, FCGI_STDIN.
  {
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1000;
    inter_args.max_requests    = 1000;
    inter_args.app_status      = EXIT_FAILURE ;
    inter_args.unix_path       = nullptr;
    
    struct RequestData request_data {};
    request_data.role           = FCGI_FILTER;
    request_data.Fcgi_id        = 100U;
    request_data.fcgi_keep_conn = true;
    request_data.fcgi_params    = map_type 
    {
      {
        {'S','C','R','I','P','T','_','N','A','M','E'},
        {'u','s','e','r','a','u','t','h'},
      },
    };
    request_data.fcgi_stdin     = std::vector<std::uint8_t> {'k','e','y','1'}; 
    request_data.fcgi_data      = std::vector<std::uint8_t> {'k','e','y','2'};

    RecordTypeOrderTester(inter_args, request_data, 
      std::vector<FcgiType> {FcgiType::kFCGI_PARAMS,
      FcgiType::kFCGI_STDIN, FcgiType::kFCGI_DATA}, 
      "Record Type Order case g");

    RecordTypeOrderTester(inter_args, request_data, 
      std::vector<FcgiType> {FcgiType::kFCGI_DATA,
      FcgiType::kFCGI_PARAMS, FcgiType::kFCGI_STDIN}, 
      "Record Type Order case h");
  }
                //
                // Single Connection Test Case Set 4
                //
                
  // Single requests with record type interleavings.

  // a) Role: Responder. Data is present for FCGI_PARAMS and FCGI_STDIN.
  //    No records of type FCGI_DATA are sent. The records of FCGI_PARAMS
  //    and FCGI_STDIN are interleaved before the streams are completed.
  do
  {
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 5;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 1, __LINE__};

    struct RequestData request_data {};
    request_data.Fcgi_id        = 1U;
    request_data.role           = FCGI_RESPONDER;
    request_data.fcgi_keep_conn = false;
    request_data.fcgi_params    = map_type
    {
      {
        {'R','E','Q','E','U','S','T','_','M','E','T','H','O','D'},
        {'P','O','S','T'}
      },
    };
    request_data.fcgi_stdin     = std::vector<std::uint8_t> 
      {'n','a','m','e','=','f','c','g','i','+','1','&','i','d','=','1','2','3'};
    // request_data.fcgi_data is value initialized.
    
    // The following record encoding depends on the value of the fields of
    // request_data.
    constexpr std::size_t record_array_length {15 * FCGI_HEADER_LEN};
    std::uint8_t records[record_array_length] = {};
    PopulateBeginRequestRecord(records, request_data.Fcgi_id,
      request_data.role, request_data.fcgi_keep_conn);
    PopulateHeader(records + (2 * FCGI_HEADER_LEN),
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 2U, 6U);
    records[3 * FCGI_HEADER_LEN]       = 14U;
    records[(3 * FCGI_HEADER_LEN) + 1] = 4U;
    PopulateHeader(records + (4 * FCGI_HEADER_LEN), 
      FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 12U, 4U);
    std::memcpy(records + (5 * FCGI_HEADER_LEN), 
      request_data.fcgi_stdin.data(), 12U);
    PopulateHeader(records + (7 * FCGI_HEADER_LEN),
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 18U, 6U);
    std::memcpy(records + (8 * FCGI_HEADER_LEN), 
      request_data.fcgi_params.begin()->first.data(), 14U);
    std::memcpy(records + (8 * FCGI_HEADER_LEN) + 14, 
      request_data.fcgi_params.begin()->second.data(), 4U);
    PopulateHeader(records + (11 * FCGI_HEADER_LEN),
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U, 0U);
    PopulateHeader(records + (12 * FCGI_HEADER_LEN),
      FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 6U, 2U);
    std::memcpy(records + (13 * FCGI_HEADER_LEN), 
      request_data.fcgi_stdin.data() + 12, 6U);
    PopulateHeader(records + (14 * FCGI_HEADER_LEN),
      FcgiType::kFCGI_STDIN, request_data.Fcgi_id, 0U, 0U);

    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      records, record_array_length) < record_array_length)
    {
      ADD_FAILURE() << "An error occurred when writing the record sequence.";
      break;
    }
    
    std::vector<FcgiRequest> request_list
      {spiac.interface().AcceptRequests()};
    if(request_list.size() != 1U)
    {
      ADD_FAILURE() << "An unexpected number of requests was returned." << '\n'
        << request_list.size();
      break;
    }
    RequestInspector(request_list[0], request_data, "Single Connect Test Case "
      "Set 4 Record type inteavings, test case a");
    EXPECT_EQ(spiac.interface().connection_count(), 1U);
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);
                //
                // Single Connection Test Case Set 5
                //
                
  // Multiple requests with record interleaving:

  // a) A Responder request, an Authorizer request, and a Filter request are
  //    sent on the same connection. Records for the requests are
  //    interleaved arbitrarily. "Partial records" in the sense that data
  //    receipt is interrupted with periods where reading would block and
  //    the current record was not received in full are present. As multiple
  //    requests are present, keep_conn is true.
  do
  {
    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_UNIX;
    inter_args.backlog         = 5;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 3;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = "/tmp/fcgi_si_single_connection_test_case"
      "_set_5_multiple_request_record_interleaving";
    
    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 1, __LINE__};

    struct RequestData responder_request {};
    responder_request.Fcgi_id        = 1U;
    responder_request.role           = FCGI_RESPONDER;
    responder_request.fcgi_keep_conn = true;
    responder_request.fcgi_params    = map_type 
    {
      {
        {'Z'},
        {'2','6'}
      }
    };
    // responder_request.fcgi_stdin is empty.
    // responder_request.fcgi_data is empty.

    struct RequestData authorizer_request {};
    authorizer_request.Fcgi_id        = 2U;
    authorizer_request.role           = FCGI_AUTHORIZER;
    authorizer_request.fcgi_keep_conn = true;
    authorizer_request.fcgi_params    = map_type 
    {
      {
        {'Y'},
        {'2','5'}
      }
    };
    // authorizer_request.fcgi_stdin is empty.
    // authorizer_request.fcgi_data is empty.

    struct RequestData filter_request {};
    filter_request.Fcgi_id        = 3U;
    filter_request.role           = FCGI_FILTER;
    filter_request.fcgi_keep_conn = true;
    filter_request.fcgi_params    = map_type 
    {
      {
        std::vector<std::uint8_t>(200, 'X'), // A 4-byte name as per FCGI n-v.
        {}
      }
    };
    filter_request.fcgi_stdin = std::vector<std::uint8_t> 
      {'p','r','i','m','e','s'};
    filter_request.fcgi_data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    struct RequestData* request_ptr_array[] = {&responder_request,
      &authorizer_request, &filter_request};

    // Define the FCGI_BEGIN_REQUEST records.
    std::uint8_t begin_records[3][2 * FCGI_HEADER_LEN] = {};
    for(int i {0}; i < 3; ++i)
    {
      struct RequestData* request_ptr {request_ptr_array[i]};
      PopulateBeginRequestRecord(begin_records[i],
        request_ptr->Fcgi_id, request_ptr->role, request_ptr->fcgi_keep_conn);
    }
    
    // Encode and check the FCGI_PARAMS records.
    std::vector<std::tuple<bool, std::size_t, std::vector<struct iovec>,
      int, std::vector<std::uint8_t>, std::size_t, map_type::iterator>>
    params_encoding_list {};
    for(struct RequestData* request_data_ptr : request_ptr_array)
    {
      params_encoding_list.push_back(EncodeNameValuePairs(
      request_data_ptr->fcgi_params.begin(),
      request_data_ptr->fcgi_params.end(), FcgiType::kFCGI_PARAMS,
      request_data_ptr->Fcgi_id, 0U));
    }
    int number_params_correct {0};
    for(int i {0}; i < 3; ++i)
    {
      if(EncodeNVPairSingleRecordFailure(params_encoding_list[i],
        request_ptr_array[i]->fcgi_params.end()))
      {
        ADD_FAILURE() << "An error occurred while encoding the name-value "
          "pairs.";
        break;
      }
      number_params_correct++;
    }
    if(number_params_correct < 3)
      break;
    // Populate the terminal FCGI_PARAMS records.
    std::uint8_t terminal_params_records[3][FCGI_HEADER_LEN] = {};
    for(int i {0}; i < 3; ++i)
      PopulateHeader(terminal_params_records[i],
        FcgiType::kFCGI_PARAMS, request_ptr_array[i]->Fcgi_id, 0U, 0U);
    
    // Encode and check the FCGI_STDIN records.
    std::vector<std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::iterator>>
    stdin_encoding_list {};
    for(int i {0}; i < 3; i++)
    {
      stdin_encoding_list.push_back(PartitionByteSequence(
        request_ptr_array[i]->fcgi_stdin.begin(),
        request_ptr_array[i]->fcgi_stdin.end(), FcgiType::kFCGI_STDIN,
        request_ptr_array[i]->Fcgi_id));
    }
    int number_stdin_correct {0};
    for(int i {0}; i < 3; ++i)
    {
      if(std::get<3>(stdin_encoding_list[i]) != 
         request_ptr_array[i]->fcgi_stdin.end())
      {
        ADD_FAILURE() << "Some of the FCGI_STDIN information was too large "
          "to be encoded with a single call to PartitionByteSequence";
        break;
      }
      number_stdin_correct++;
    }
    if(number_stdin_correct < 3)
      break;
    
    // Populate terminal FCGI_STDIN record for filter_request.
    // PartitionByteSequence will produce temrinal records for
    // responder_request and authorizer_request as their FCGI_STDIN streams
    // have no content.
    std::uint8_t terminal_filter_stdin_record[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_filter_stdin_record,
      FcgiType::kFCGI_STDIN, filter_request.Fcgi_id, 0U, 0U);

    // Encode the FCGI_DATA records for the filter request.
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
      std::size_t, std::vector<std::uint8_t>::iterator>
    encoded_data_records {PartitionByteSequence(
      filter_request.fcgi_data.begin(), filter_request.fcgi_data.end(),
      FcgiType::kFCGI_DATA, filter_request.Fcgi_id)};
    if(std::get<3>(encoded_data_records) != filter_request.fcgi_data.end())
    {
      ADD_FAILURE() << "The FCGI_DATA information was too large to be encoded "
        "with a single call to PartitionByteSequence.";
      break;
    }
    // Populate the terminal FCGI_DATA record.
    std::uint8_t terminal_data_record[FCGI_HEADER_LEN] = {};
    PopulateHeader(terminal_data_record, 
      FcgiType::kFCGI_DATA, filter_request.Fcgi_id, 0U, 0U);
    
    // Begin writing to the interface. Sequence:
    // R-b, A-b, R-p, AcceptRequests, F-b, A-p, AcceptRequests, A-pe,
    // F-d, F-de, F-s, F-se, AcceptRequests, R-pe, R-s, F-p, F-pe
    // AcceptRequests
    // Key: R = Responder, A = Authorizer, F = Filter; b = FCGI_BEGIN_REQUEST,
    //      s = FCGI_STDIN, p = FCGI_PARAMS, d = FCGI_DATA, *e = end of stream
    const int& in {spiac.client_descriptors()[0]};
    std::vector<FcgiRequest> request_list {};
    // R-b, A-b
    if(as_components::socket_functions::SocketWrite(in, begin_records[0], 
       4 * FCGI_HEADER_LEN) < (4 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // R-p
    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
         in, 
         std::get<2>(params_encoding_list[0]).data(),
         std::get<2>(params_encoding_list[0]).size(),
         std::get<1>(params_encoding_list[0])))        != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);
    // F-b
    if(as_components::socket_functions::SocketWrite(in, begin_records[2], 
       2 * FCGI_HEADER_LEN) < (2 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // A-p
    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
         in, 
         std::get<2>(params_encoding_list[1]).data(),
         std::get<2>(params_encoding_list[1]).size(),
         std::get<1>(params_encoding_list[1])))        != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);
    // A-pe
    if(as_components::socket_functions::SocketWrite(in,
      terminal_params_records[1],  1 * FCGI_HEADER_LEN) < 
      (1 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // F-d
    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
         in, 
         std::get<1>(encoded_data_records).data(),
         std::get<1>(encoded_data_records).size(),
         std::get<2>(encoded_data_records)))        != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-de
     if(as_components::socket_functions::SocketWrite(in, terminal_data_record, 
       1 * FCGI_HEADER_LEN) < (1 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // F-s
    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
         in, 
         std::get<1>(stdin_encoding_list[2]).data(),
         std::get<1>(stdin_encoding_list[2]).size(),
         std::get<2>(stdin_encoding_list[2])))        != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-se
    if(as_components::socket_functions::SocketWrite(in,
      terminal_filter_stdin_record, 1 * FCGI_HEADER_LEN) <
      (1 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);
    // R-pe
    if(as_components::socket_functions::SocketWrite(in,
      terminal_params_records[0], 1 * FCGI_HEADER_LEN) <
      (1 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // R-s
    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
         in, 
         std::get<1>(stdin_encoding_list[0]).data(),
         std::get<1>(stdin_encoding_list[0]).size(),
         std::get<2>(stdin_encoding_list[0])))        != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-p
    if(std::get<2>(as_components::socket_functions::ScatterGatherSocketWrite(
         in, 
         std::get<2>(params_encoding_list[2]).data(),
         std::get<2>(params_encoding_list[2]).size(),
         std::get<1>(params_encoding_list[2])))        != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-pe
    if(as_components::socket_functions::SocketWrite(in,
      terminal_params_records[2], 1 * FCGI_HEADER_LEN) <
      (1 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "Write error" << '\n' << std::strerror(errno);
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);

    // Validate results.
    if(request_list.size() != 3U)
    {
      ADD_FAILURE() << "An unexpected number of FcgiRequest objects was "
        "returned." << '\n' << request_list.size();
      break;
    }
    for(int i {0}; i < 3; ++i)
    {
      RequestInspector(request_list[i], 
        *request_ptr_array[request_list[i].get_request_identifier().Fcgi_id() - 1],
        "Single Connection Test Case Set 5: Multiple requests with record "
          "interleaving");
    }
    EXPECT_EQ(spiac.interface().connection_count(), 1U);
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);

                // Multiple connection tests

  // FCGI_PARAMS records are sent last as every role, including an unknown role,
  // requires at least a terminal empty FCGI_PARAMS record to complete a
  // request. Sending records in this way allows data that would otherwise
  // be ignored because of request completion to be received.
  auto SendRequestData = []
  (
    int socket_descriptor,
    const struct RequestData& request_data,
    std::string test_case_name
  )->bool
  {
    if(socket_descriptor < 0)
    {
      ADD_FAILURE() << "socket_descriptor was less than zero in "
        << test_case_name;
      return false;
    }

    // Create an FCGI_BEGIN_RECORD and send it.
    constexpr std::size_t header_length 
      {std::size_t(2 * FCGI_HEADER_LEN)};
    std::uint8_t begin_record[header_length] = {};
    PopulateBeginRequestRecord(begin_record, request_data.Fcgi_id,
      request_data.role, request_data.fcgi_keep_conn);
    if(as_components::socket_functions::SocketWrite(socket_descriptor,
      begin_record, header_length) < header_length)
    {
      ADD_FAILURE() << "An error occurred while sending the FCGI_BEGIN_REQUEST "
        "record in " << test_case_name;
      return false;
    }
 
    // A pointer-to-member is used to specify if either fcgi_stdin or fcgi_data
    // of a RequestData instance should be sent.
    auto EncodeAndSendStdinOrData = [&request_data, &test_case_name,
      socket_descriptor]
    (
      std::vector<std::uint8_t> RequestData::* data_or_stdin_ptm
    )->bool
    {
      FcgiType type {};
      const char* stdin_description {"FCGI_STDIN"};
      const char* data_description {"FCGI_DATA"};
      const char* type_description 
        {(data_or_stdin_ptm == &RequestData::fcgi_stdin) ?
          stdin_description : data_description};
      type = (data_or_stdin_ptm == &RequestData::fcgi_stdin) ? 
        FcgiType::kFCGI_STDIN : FcgiType::kFCGI_DATA;

      std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, 
        std::size_t,  std::vector<std::uint8_t>::const_iterator>
      encoding_return {PartitionByteSequence(
        (request_data.*data_or_stdin_ptm).cbegin(), 
        (request_data.*data_or_stdin_ptm).cend(),
        type, request_data.Fcgi_id
      )};
      if(std::get<3>(encoding_return) != 
        (request_data.*data_or_stdin_ptm).cend())
      {
        ADD_FAILURE() << "Not all of the " << type_description 
          << " data could be encoded by a call to "
             "PartitionByteSequence in " << test_case_name;
        return false;
      }

      std::string message {"An error occurred while sending the "};
      (message += type_description) += " data in ";
      message += test_case_name;

      std::tuple<struct iovec*, int, std::size_t> sgsw_return
        {as_components::socket_functions::ScatterGatherSocketWrite(
          socket_descriptor,
          std::get<1>(encoding_return).data(),
          std::get<1>(encoding_return).size(),
          std::get<2>(encoding_return))};
      if(std::get<2>(sgsw_return) != 0U)
      {
        ADD_FAILURE() << message << '\n' << std::strerror(errno);
        return false;
      }

      if((request_data.*data_or_stdin_ptm).size() != 0U)
      {
        std::uint8_t end[FCGI_HEADER_LEN] = {};
        PopulateHeader(end, type, request_data.Fcgi_id, 0U, 0U);
        if(as_components::socket_functions::SocketWrite(socket_descriptor, end,
          FCGI_HEADER_LEN) < FCGI_HEADER_LEN)
        {
          ADD_FAILURE() << message << '\n' << std::strerror(errno);
          return false;
        }
      }

      return true;
    };

    // Encode and send an FCGI_STDIN data.
    if(!((request_data.role == FCGI_AUTHORIZER) && 
         (request_data.fcgi_stdin.size() == 0U)))
    {
      if(!EncodeAndSendStdinOrData(&RequestData::fcgi_stdin))
        return false;
    }

    // Conditionally encode and send FCGI_DATA data.
    if(!(((request_data.role == FCGI_RESPONDER)  || 
          (request_data.role == FCGI_AUTHORIZER))   &&
         (request_data.fcgi_data.size() == 0U)))
    {
      if(!EncodeAndSendStdinOrData(&RequestData::fcgi_data))
        return false;
    }

    // Encode and send FCGI_PARAMS data.
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::const_iterator>
    params_encoding_return {EncodeNameValuePairs(
      request_data.fcgi_params.cbegin(), request_data.fcgi_params.cend(),
      FcgiType::kFCGI_PARAMS, request_data.Fcgi_id, 0U
    )};
    if(EncodeNVPairSingleRecordFailure(params_encoding_return,
      request_data.fcgi_params.cend()))
    {
      ADD_FAILURE() << "An error occurred while encoding the name-value "
        "pairs in " << test_case_name;
      return false;
    }
    const char* params_message {"An error occurred while sending the "
      "FCGI_PARAMS data in "};
    std::tuple<struct iovec*, int, std::size_t> params_sgsw_return
      {as_components::socket_functions::ScatterGatherSocketWrite(
        socket_descriptor,
        std::get<2>(params_encoding_return).data(),
        std::get<2>(params_encoding_return).size(),
        std::get<1>(params_encoding_return))};
    if(std::get<2>(params_sgsw_return) != 0U)
    {
      ADD_FAILURE() << params_message << test_case_name << '\n' 
        << std::strerror(errno);
      return false;
    }
    std::uint8_t params_end[FCGI_HEADER_LEN] = {};
    PopulateHeader(params_end, FcgiType::kFCGI_PARAMS,
      request_data.Fcgi_id, 0U, 0U);
    if(as_components::socket_functions::SocketWrite(socket_descriptor, params_end,
      FCGI_HEADER_LEN) < FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << params_message << test_case_name << '\n'
        << std::strerror(errno);
      return false;
    }

    return true;
  };

  // 1) (No interleaving of request data receipt between connections; 
  //    homogenous request type; single request on each connection.) 
  //    Five connections. A Responder request is sent on each connection to the
  //    interface. Each request contains unique FCGI_PARAMS and FCGI_STDIN
  //    data. Activity is synchronized such that all data for the requests is
  //    sent to the interface before a call to AcceptRequests is made on the
  //    interface. This means that a request should be received in full for
  //    each connection before the interface moves on to the next connection.
  do
  {
    const char* case_name {"Multiple Clients Test Case 1"};
    constexpr int client_number {5};

    InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 10;
    inter_args.max_connections = 10;
    inter_args.max_requests    = 10;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, client_number, __LINE__};

    struct RequestData request_array[client_number] = {};
    std::uint8_t params_name {'1'};
    std::uint8_t stdin_value {'A'};
    for(int i {0}; i < client_number; ++i)
    {
      request_array[i].role           = FCGI_RESPONDER;
        // Assign unique FCGI_id values for identification for comparison.
      request_array[i].Fcgi_id        = std::uint16_t(i + 1); 
      request_array[i].fcgi_keep_conn = false;
      request_array[i].fcgi_params    = map_type {{{params_name}, {}}};
      request_array[i].fcgi_stdin     = std::vector<std::uint8_t> {stdin_value};

      ++params_name;
      ++stdin_value;
    }

    bool transmission_success {true};
    for(int i {0}; i < client_number; ++i)
    {
      if(!(transmission_success = SendRequestData(spiac.client_descriptors()[i],
        request_array[i], case_name)))
        break;
    }
    if(!transmission_success)
      break;

    std::vector<FcgiRequest> fcgi_request_list 
      {spiac.interface().AcceptRequests()};
    if(fcgi_request_list.size() != client_number)
    {
      ADD_FAILURE() << "An unexpected number of FcgiRequest objects was found."
        << '\n' << "Request number: " << fcgi_request_list.size();
      break;
    }
    for(int i {0}; i < client_number; ++i)
    {
      std::uint16_t request_id
        {fcgi_request_list[i].get_request_identifier().Fcgi_id()};
      RequestInspector(fcgi_request_list[i], request_array[request_id - 1],
        (("A request mismatch occurred for request " + 
        std::to_string(request_id - 1)) + "in ") + case_name);
    }
    EXPECT_EQ(spiac.interface().connection_count(), std::size_t(client_number));
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);

  // 2) (No interleaving of request data receipt between connections; mixed
  //    request type; single request on each connection.) 
  //    Five connections: A mix of Responder, Authorizer, and Filter requests
  //    are sent. A single request is sent on each connection. As in 1, the
  //    data for each request is sent in full before the interface begins
  //    processing the requests.
  do
  {
    const char* case_name {"Multiple Clients Test Case 2"};
    constexpr int client_number {5};

    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 10;
    inter_args.max_connections = 10;
    inter_args.max_requests    = 10;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, client_number, __LINE__};

    struct RequestData request_array[client_number] = {};
    std::uint8_t params_name {'1'};
    std::uint8_t stdin_value {'A'};

    // Responder requests.
    for(int i {0}; i < 2; ++i)
    {
      request_array[i].role           = FCGI_RESPONDER;
        // Assign unique Fcgi_id values for identification for comparison.
      request_array[i].Fcgi_id        = std::uint16_t(i + 1); 
      request_array[i].fcgi_keep_conn = false;
      request_array[i].fcgi_params    = map_type {{{params_name}, {}}};
      request_array[i].fcgi_stdin     = std::vector<std::uint8_t> {stdin_value};

      ++params_name;
      ++stdin_value;
    }

    // Authorizer requests
    for(int i {2}; i < 4; ++i)
    {
      request_array[i].role           = FCGI_AUTHORIZER;
        // Assign unique Fcgi_id values for identification for comparison.
      request_array[i].Fcgi_id        = std::uint16_t(i + 1); 
      request_array[i].fcgi_keep_conn = false;
      request_array[i].fcgi_params    = map_type {{{params_name}, {}}};
      request_array[i].fcgi_stdin     = std::vector<std::uint8_t> {stdin_value};

      ++params_name;
      ++stdin_value;
    }

    //  Filter request
    request_array[4].role           = FCGI_FILTER;
    request_array[4].Fcgi_id        = 5;
    request_array[4].fcgi_keep_conn = false;
    request_array[4].fcgi_params    = map_type {{{params_name}, {}}};
    request_array[4].fcgi_stdin     = std::vector<std::uint8_t> {stdin_value};
    request_array[4].fcgi_data      = std::vector<std::uint8_t> {1,2,3};

    bool transmission_success {true};
    for(int i {0}; i < client_number; ++i)
    {
      if(!(transmission_success = SendRequestData(spiac.client_descriptors()[i],
        request_array[i], case_name)))
        break;
    }
    if(!transmission_success)
      break;

    std::vector<FcgiRequest> fcgi_request_list 
      {spiac.interface().AcceptRequests()};
    if(fcgi_request_list.size() != client_number)
    {
      ADD_FAILURE() << "An unexpected number of FcgiRequest objects was found."
        << '\n' << "Request number: " << fcgi_request_list.size();
      break;
    }
    for(int i {0}; i < client_number; ++i)
    {
      std::uint16_t request_id
        {fcgi_request_list[i].get_request_identifier().Fcgi_id()};
      RequestInspector(fcgi_request_list[i], request_array[request_id - 1],
        (("A request mismatch occurred for request " + 
        std::to_string(request_id - 1)) + "in ") + case_name);
    }
    EXPECT_EQ(spiac.interface().connection_count(), std::size_t(client_number));
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);

  // 3) (No interleaving of request data receipt between connections; 
  //    homogenous request type; multiple requests on a connection.)
  //    Ten connections. Responder requests are sent on each connection. As in
  //    1, each request has unique FCGI_PARAMS and FCGI_STDIN data. However,
  //    for at least one of the connections, multiple requests are sent on the
  //    same connection. As in 1, all data for each request is sent before the
  //    interface begins processing data.
  do
  {
    constexpr int client_number {10};
    constexpr int request_number {client_number + 4};

    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 100;
    inter_args.max_connections = 100;
    inter_args.max_requests    = 100;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, client_number, __LINE__};

    // Five requests for the first client and one request for other clients.
    struct RequestData request_array[request_number] = {};
    std::uint8_t params_name {'1'};
    std::uint8_t stdin_value {'A'};

    // Responder requests for the client with multiple requests.
    for(int i {0}; i < request_number; ++i)
    {
      request_array[i].role           = FCGI_RESPONDER;
        // Assign unique Fcgi_id values for identification for comparison.
      request_array[i].Fcgi_id        = std::uint16_t(i + 1);
        // The presence of multiple requests on a connections will usually
        // cause keep_conn to be set.
      request_array[i].fcgi_keep_conn = true;
      request_array[i].fcgi_params    = map_type {{{params_name}, {}}};
      request_array[i].fcgi_stdin     = std::vector<std::uint8_t> {stdin_value};

      ++params_name;
      ++stdin_value;
    }

    const char* case_name {"Multiple Clients Test Case 3"};
    bool transmission_success {true};
    for(int i {0}; i < 5; ++i)
    {
      if(!(transmission_success = SendRequestData(spiac.client_descriptors()[0],
          request_array[i], case_name)))
          break;
    }
    if(!transmission_success)
      break;
    for(int i {1}; i < client_number; ++i)
    {
      if(!(transmission_success = SendRequestData(spiac.client_descriptors()[i],
        request_array[i + 4], case_name)))
        break;
    }
    if(!transmission_success)
      break;

    std::vector<FcgiRequest> fcgi_request_list 
      {spiac.interface().AcceptRequests()};
    if(fcgi_request_list.size() != request_number)
    {
      ADD_FAILURE() << "An unexpected number of FcgiRequest objects was found."
        << '\n' << "Request number: " << fcgi_request_list.size();
      break;
    }
    for(int i {0}; i < request_number; ++i)
    {
      std::uint16_t request_id
        {fcgi_request_list[i].get_request_identifier().Fcgi_id()};
      RequestInspector(fcgi_request_list[i], request_array[request_id - 1],
         (("A request mismatch occurred for request " + 
        std::to_string(request_id - 1)) + "in ") + case_name);
    }
    EXPECT_EQ(spiac.interface().connection_count(), std::size_t(client_number));
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
  } while(false);

  // 4) (Interleaving of request data receipt between connections. Partial
  //    records. Mixed request types. Multiple requests on a connection.)
  //    Two connections. Multiple Responder requests are sent on one 
  //    connections. A Filter request is sent on the other connection. Request
  //    data is sent with partial records. Multiple cylces of data
  //    transmission and data processing are required. Multiple Responder
  //    requests are sent on one of the connections.  
  do
  {
    std::string case_name {"Multiple Clients Test Case 4"};

    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.backlog         = 10;
    inter_args.max_connections = 10;
    inter_args.max_requests    = 10;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    GTestNonFatalSingleProcessInterfaceAndClients spiac
      {inter_args, 2, __LINE__};

    struct RequestData responder_request_1 {};
    responder_request_1.role = FCGI_RESPONDER;
    responder_request_1.Fcgi_id = 1U;
    responder_request_1.fcgi_keep_conn = true;
    responder_request_1.fcgi_params = map_type
    {
      {
        {'Q','U','E','R','Y','_','S','T','R','I','N','G'}, 
        {'i','d','=','1','2','1','4'} 
      }
    };
    // fcgi_stdin and fcgi_data are empty.

    struct RequestData responder_request_2 {};
    responder_request_2.role = FCGI_RESPONDER;
    responder_request_2.Fcgi_id = 2U;
    responder_request_2.fcgi_keep_conn = true;
    responder_request_2.fcgi_params = map_type
    {
      {
        {'Q','U','E','R','Y','_','S','T','R','I','N','G'}, 
        {'i','d','=','3','4','4','9'} 
      }
    };
    // fcgi_stdin and fcgi_data are empty.

    struct RequestData filter_request {};
    filter_request.role           = FCGI_FILTER;
    filter_request.Fcgi_id        = 14U;
    filter_request.fcgi_keep_conn = false;
    filter_request.fcgi_params    = map_type
    {
      {
        {'F','I','L','T','E','R','_','T','Y','P','E'},
        {'r','e','g','e','x'}
      }
    };
    filter_request.fcgi_stdin     = std::vector<std::uint8_t> 
      {'<','(','.','*','\?',')','>','(','.','*','\?',')','<','/','\\','1','>'};
    filter_request.fcgi_data      = std::vector<std::uint8_t> 
    {
        '<','!','-','-','E','x','a','m','p','l','e','-','-','>','\n',
        '<','c','>','\n',
          '<','p','>','A','<','/','p','>','\n',
        '<','/','c','>'
    };

    // Encode the requests.
    std::uint8_t responder_1_begin[2 * FCGI_HEADER_LEN] = {};
    PopulateBeginRequestRecord(responder_1_begin,
      responder_request_1.Fcgi_id, responder_request_1.role,
      responder_request_1.fcgi_keep_conn);
    std::uint8_t responder_1_end_records[2 * FCGI_HEADER_LEN] = {};
    PopulateHeader(responder_1_end_records,
      FcgiType::kFCGI_PARAMS, responder_request_1.Fcgi_id, 0U, 0U);
    PopulateHeader(responder_1_end_records + FCGI_HEADER_LEN,
      FcgiType::kFCGI_STDIN, responder_request_1.Fcgi_id, 0U, 0U);
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::iterator>
    encoded_responder_1_params {EncodeNameValuePairs(
      responder_request_1.fcgi_params.begin(), 
      responder_request_1.fcgi_params.end(),
      FcgiType::kFCGI_PARAMS, responder_request_1.Fcgi_id, 0U)};
    if(EncodeNVPairSingleRecordFailure(encoded_responder_1_params,
      responder_request_1.fcgi_params.end()))
    {
      ADD_FAILURE() << "An error occurred in the encoding of the FCGI_PARAMS "
        "information of a responder request.";
      break;
    }

    std::uint8_t responder_2_begin[2 * FCGI_HEADER_LEN] = {};
    PopulateBeginRequestRecord(responder_2_begin,
      responder_request_2.Fcgi_id, responder_request_2.role,
      responder_request_2.fcgi_keep_conn);
    std::uint8_t responder_2_end_records[2 * FCGI_HEADER_LEN] = {};
    PopulateHeader(responder_2_end_records,
      FcgiType::kFCGI_PARAMS, responder_request_2.Fcgi_id, 0U, 0U);
    PopulateHeader(responder_2_end_records + FCGI_HEADER_LEN,
      FcgiType::kFCGI_STDIN, responder_request_2.Fcgi_id, 0U, 0U);
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::iterator>
    encoded_responder_2_params {EncodeNameValuePairs(
      responder_request_2.fcgi_params.begin(), 
      responder_request_2.fcgi_params.end(),
      FcgiType::kFCGI_PARAMS, responder_request_2.Fcgi_id, 0U)};
    if(EncodeNVPairSingleRecordFailure(encoded_responder_2_params,
      responder_request_2.fcgi_params.end()))
    {
      ADD_FAILURE() << "An error occurred in the encoding of the FCGI_PARAMS "
        "information of a responder request.";
      break;
    }

    std::uint8_t filter_begin[2 * FCGI_HEADER_LEN] = {};
    PopulateBeginRequestRecord(filter_begin, filter_request.Fcgi_id,
      filter_request.role, filter_request.fcgi_keep_conn);
    std::uint8_t filter_end_records[3 * FCGI_HEADER_LEN] = {};
    PopulateHeader(filter_end_records, 
      FcgiType::kFCGI_PARAMS, filter_request.Fcgi_id, 0U, 0U);
    PopulateHeader(filter_end_records + FCGI_HEADER_LEN, 
      FcgiType::kFCGI_STDIN, filter_request.Fcgi_id, 0U, 0U);
    PopulateHeader(filter_end_records + (2 * FCGI_HEADER_LEN), 
      FcgiType::kFCGI_DATA, filter_request.Fcgi_id, 0U, 0U);
    std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
      std::vector<std::uint8_t>, std::size_t, map_type::iterator>
    encoded_filter_params {EncodeNameValuePairs(
      filter_request.fcgi_params.begin(), 
      filter_request.fcgi_params.end(),
      FcgiType::kFCGI_PARAMS, filter_request.Fcgi_id, 0U)};
    if(EncodeNVPairSingleRecordFailure(encoded_filter_params,
      filter_request.fcgi_params.end()))
    {
      ADD_FAILURE() << "An error occurred in the encoding of the FCGI_PARAMS "
        "information of the filter request.";
      break;
    }
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
      std::size_t, std::vector<std::uint8_t>::iterator>
    encoded_filter_stdin {PartitionByteSequence(
      filter_request.fcgi_stdin.begin(), filter_request.fcgi_stdin.end(),
      FcgiType::kFCGI_STDIN, filter_request.Fcgi_id)};  
    if(std::get<3>(encoded_filter_stdin) != 
       filter_request.fcgi_stdin.end())
    {
      ADD_FAILURE() << "An error occurred in the encoding of the FCGI_STDIN "
        "information of the filter request.";
      break;
    }
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
      std::size_t, std::vector<std::uint8_t>::iterator>
    encoded_filter_data {PartitionByteSequence(
      filter_request.fcgi_data.begin(), filter_request.fcgi_data.end(),
      FcgiType::kFCGI_DATA, filter_request.Fcgi_id)};  
    if(std::get<3>(encoded_filter_data) != 
       filter_request.fcgi_data.end())
    {
      ADD_FAILURE() << "An error occurred in the encoding of the FCGI_DATA "
        "information of the filter request.";
      break;
    }

    // Begin sending partial records and records which are interleaved between
    // clients. Sequence:
    // R1-b-p, F-b, AcceptRequests, R1-b-c, R2-b, R2-p-p, F-p, F-s-p,
    // AcceptRequests, F-s-c, R2-p-c, R2-se, R2-pe, R1-p, R1-pe, F-d,
    // F-pe, F-se, F-de, R1-se, AcceptRequests.
    // Key: R1 = responder_request_1, R2 = responder_request_2, 
    //      F = filter_request, b = FCGI_BEGIN_REQUEST record, p = FCGI_PARAMS
    //      record, s = FCGI_STDIN record, *-p = partial record, 
    //      *-c = completion of previous partial record, *e = terminal record
    //      for the appropriate stream.
    std::vector<FcgiRequest> request_list {};
    std::tuple<struct iovec*, int, std::size_t> sgsw_return {};
    // R1-b-p
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0], 
      responder_1_begin, 3U) < 3U)
    {
      ADD_FAILURE() << std::strerror(errno);
      break;
    }
    // F-b
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[1], 
      filter_begin, 2 * FCGI_HEADER_LEN) < 
      2 * FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << std::strerror(errno);
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);
    // R1-b-c
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      responder_1_begin + 3U, (2 * FCGI_HEADER_LEN) - 3U) < 
      ((2 * FCGI_HEADER_LEN) - 3U))
    {
      ADD_FAILURE() << std::strerror(errno);
      break;
    }
    // R2-b
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      responder_2_begin, 2 * FCGI_HEADER_LEN) < 
      2 * FCGI_HEADER_LEN)
    {
      ADD_FAILURE() << std::strerror(errno);
      break;
    }
    // R2-p-p
    int R2pp_partial_iovec_count {std::max<int>(1,
      std::get<2>(encoded_responder_2_params).size() / 2)};
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[0], 
      std::get<2>(encoded_responder_2_params).data(),
      R2pp_partial_iovec_count);
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-p
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[1], 
      std::get<2>(encoded_filter_params).data(),
      std::get<2>(encoded_filter_params).size(), 
      std::get<1>(encoded_filter_params));
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-s-p
    int Fsp_partial_iovec_count {std::max<int>(1,
      std::get<1>(encoded_filter_stdin).size() / 2)};
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[1],
      std::get<1>(encoded_filter_stdin).data(),
      Fsp_partial_iovec_count);
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);
    // F-s-c
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[1],
      std::get<1>(encoded_filter_stdin).data() + Fsp_partial_iovec_count,
      int(std::get<1>(encoded_filter_stdin).size()) - Fsp_partial_iovec_count);
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // R2-p-c
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[0], 
      std::get<2>(encoded_responder_2_params).data() +
        R2pp_partial_iovec_count,
      int(std::get<2>(encoded_responder_2_params).size()) -
        R2pp_partial_iovec_count);
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // R2-se
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      responder_2_end_records + FCGI_HEADER_LEN,
      FCGI_HEADER_LEN) < FCGI_HEADER_LEN)
    {
      ADD_FAILURE();
      break;
    }
    // R2-pe
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      responder_2_end_records, FCGI_HEADER_LEN) <
      FCGI_HEADER_LEN)
    {
      ADD_FAILURE();
      break;
    }
    // R1-p
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[0], 
      std::get<2>(encoded_responder_1_params).data(),
      std::get<2>(encoded_responder_1_params).size(),
      std::get<1>(encoded_responder_1_params));
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // R1-pe
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      responder_1_end_records, FCGI_HEADER_LEN) <
      FCGI_HEADER_LEN)
    {
      ADD_FAILURE();
      break;
    }
    // F-d
    sgsw_return = as_components::socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[1], 
      std::get<1>(encoded_filter_data).data(),
      std::get<1>(encoded_filter_data).size(),
      std::get<2>(encoded_filter_data));
    if(std::get<2>(sgsw_return) != 0U)
    {
      ADD_FAILURE();
      break;
    }
    // F-pe
    // F-se
    // F-de
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[1],
      filter_end_records, 3 * FCGI_HEADER_LEN) <
      (3 * FCGI_HEADER_LEN))
    {
      ADD_FAILURE();
      break;
    }
    // R1-se
    if(as_components::socket_functions::SocketWrite(spiac.client_descriptors()[0],
      responder_1_end_records + FCGI_HEADER_LEN,
      FCGI_HEADER_LEN) < FCGI_HEADER_LEN)
    {
      ADD_FAILURE();
      break;
    }
    // AcceptRequests
    AcceptAndAddRequests(&spiac, &request_list);
    if(request_list.size() != 3U)
    {
      ADD_FAILURE() << "An unexpected number of FcgiRequest objects was "
        "returned." << '\n' << "Request count: " << request_list.size();
      break;
    }
    for(int i {0}; i < 3; ++i)
    {
      switch(request_list[i].get_request_identifier().Fcgi_id()) {
        case std::uint16_t(1U) : {
          RequestInspector(request_list[i], responder_request_1,
            "responder_request_1 in " + case_name);
          break;
        }
        case std::uint16_t(2U) : {
          RequestInspector(request_list[i], responder_request_2,
            "responder_request_2 in " + case_name);
          break;
        }
        case std::uint16_t(14U) : {
          RequestInspector(request_list[i], filter_request,
            "filter_request in " + case_name);
          break;
        }
        default : {
          ADD_FAILURE() << "An Fcgi_id value that was not present in the "
            "requests was returned.";
          break;
        }
      }
    }
    EXPECT_EQ(spiac.interface().connection_count(), std::size_t(2U));
    EXPECT_EQ(spiac.interface().interface_status(), true);
    EXPECT_EQ(spiac.interface().get_overload(), false);
   } while(false);

  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc,
    "FcgiRequestGeneration", __LINE__);
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGPIPE,
    __LINE__));
}

// RequestAcceptanceAndRejection
// Examined properties:
// 1) Request limit (value of max_requests passed to the interface
//    constructor): 1 or greater than 1.
// 2) Presence of previous connections when the interface is placed into an
//    overloaded state: requests are present or not.
// 3) Incomplete requests vs. requests for which an FcgiRequest object
//    has been produced.
// 4) Multiple connections and separate request tallies.
// 5) Request number tracking as requests are sent and completed.
//
// Test Cases:
// Single connection:
// 1) max_connections == 1, max_requests == 1. A single request has been
//    received in full. A new application request should be rejected with:
//    protocol_status == FCGI_CANT_MPX_CONN and
//    application_status == EXIT_FAILURE. A management request should
//    receive an appropriate response.
// 2) As 1, but the previous request has not been received in full.
// 3) As 2, but the interface was put into an overloaded state before the
//    FCGI_BEGIN_REQUEST record of the request was received. The
//    protocol_status of the FCGI_END_REQUEST record sent in response should
//    be equal to FCGI_CANT_MPX_CONN as this status can apply. The
//    application_status of the response should be EXIT_FAILURE. Data for
//    the partially-received request should be accepted.
// 4) max_connection == 1, max_requests == 1. The interface is put into an
//    overloaded state. No requests have been received. A request should be
//    rejected with protocol_status == FCGI_OVERLOADED and
//    application_status == EXIT_FAILURE. A management request should be
//    handled normally.
//
// Multiple connections:
//    The protcol_status in all cases should be FCGI_OVERLOADED.
//    The application_status in all cases should be EXIT_FAILURE.
// 5) max_connections == 10, max_requests = 5. Two connections are present.
//    One connection has received no requests. The other connection has
//    received 5 requests in full. A request sent to the connection at the
//    request limit should be rejected. A request sent to the connection
//    without requests should be accepted. A management request sent to the
//    connection at the request limit should be handled normally.
// 6) As 6, but the connection at the request limit has a combination of
//    partially-received requests and fully-received requests.
// 7) As 7, but the interface is placed into an overloaded state. New
//    requests on both connections should be rejected. Management requests
//    on both connections should be handled normally. Data for partially-
//    received requests should be accepted.
// 8) max_connections == 10, max_requests = 5. Two connections are present.
//    Neither connection has received requests. The interface is placed into
//    an overloaded state. New requests on either connection should be
//    rejected. Management requests should be handled normally.
//
// Request number tracking:
// 9) max_connections = 10, max_requests = 2. One connection is idle.
//    Another connections receives two requests. A third request should then
//    be rejected. One of the two requests is completed. A fourth request
//    should then be accepted.
//
// Modules which testing depends on:
//
// Other modules whose testing depends on this module:
TEST(FcgiServerInterface, RequestAcceptanceAndRejection)
{
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
    __LINE__));

  testing::FileDescriptorLeakChecker fdlc {};




  testing::gtest::GTestNonFatalCheckAndReportDescriptorLeaks(&fdlc,
    "RequestAcceptanceAndRejection", __LINE__);
  ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalRestoreSignal(SIGPIPE,
    __LINE__));
}

// ConnectionClosureAndAbortRequests
// Examined properties:
// 1) Proper behavior when it is discovered that a client closed a
//    connection. After reacting to the closure:
//    a) The value returned by a call to connection_count should be one less
//       than the value returned by an immediately-preceding call.
//    b) If the interface was at its connection limit, a new connection
//       should be accepted.
//    c) FcgiRequest objects should be updated appropriately.
//       1) A call to AbortStatus should return true.
//       2) Calls to Complete, RejectRole, Write, and WriteError should
//          return false.
//       3) A call to get_completion should return true.
// 2) Proper behavior reacting to connection closure by a client when the
//    interface is in an overloaded state.
// 3) Proper behavior when a request is completed.
//    a) The completion of a request whose FCGI_BEGIN_REQUEST record did not
//       have its FCGI_KEEP_CONN flag set should cause the interface to close
//       the connection when the request is completed. In this case:
//       1) A call to connection_count should return the appropriate number.
//       2) If the interface was at its connection limit, a new connection
//          should be accepted.
//       3) If other FCGIRequest objects are present, their state should be
//          updated to reflect connection closure.
//           a) A call to AbortStatus should return true.
//           b) Calls to Complete, RejectRole, Write, and WriteError should
//              return false.
// 4) Proper behavior reacting to request completion in an overloaded state.
// 5) Proper behavior reacting to FCGI_ABORT_REQUEST records.
//    a) If a partially-received request had the FCGI_KEEP_CONN flag set in
//       its FCGI_BEGIN_REQUEST record and an FCGI_ABORT_REQUEST record
//       was received for the request, then:
//       1) The request should be removed from the interface. An observation
//          of this change, such as the acceptance of a request when others
//          were previously rejected due to the connection being at its
//          request limit, should be made.
//       2) An FCGI_END_REQUEST record should be sent by the interface to the
//          client. The application status of the record should be that given
//          by the value of app_status_on_abort when the interface was
//          constructed.
//    b) If a partially-received request did not have the FCGI_KEEP_CONN
//       flag set in its FCGI_BEGIN_REQUEST record and an FCGI_ABORT_REQUEST
//       record was received for the request, then the connection should be
//       closed. In this case:
//       1) An FCGI_END_REQUEST record should be sent to the client before
//          connection closure. The application status of the record should
//          be that given by the value of app_status_on_abort when the
//          interface was constructed.
//    c) If a request was completely received and an FCGI_ABORT_STATUS record
//       was received for the request, the state of the FcgiRequest object
//       for the request should be appropriately updated.
//       1) A call to AbortStatus should return true.
//       2) Calls to Complete, RejectRole, Write, and WriteError should
//          function as normal.
//       3) A call to get_completion should return false.
//
// Test cases:
//
// Modules which testing depends on:
//
// Other modules whose testing depends on this module:
TEST(FcgiServerInterface, ConnectionClosureAndAbortRequests)
{
  
}

// FcgiRequestDataTransmissionAndCompletion
// Examined properties:
// 1) Behavior of Complete and RejectRole in several cases:
//    a) When no data has been sent with an output function.
//    b) When data has been sent by calls to Write and WriteError.
//    c) The occurrence of one or more than one call to Write and
//       WriteError.
//    Behavior of the FcgiRequest object and interface pair includes:
//    a) The transmission of terminal records for FCGI_STDIN and FCGI_STDOUT.
//    b) The transmission of an FCGI_END_REQUEST record with the
//       provided value of app_status and the specified value for the
//       protocol status.
//    c) Changes to interface state.
//       1) The ConnectionClosureAndAbortRequests test examines connection
//          closure related to request completion.
//       2) The RequestAcceptanceAndRejection test examines updates to the
//          request tally upon request completion by observing how the
//          interface behaves relative to request acceptance or rejection
//          when the request limit was reached for a connection and a request
//          was completed on that connection.
// 2) Behavior of output methods when multiple requests are present
//    on the same connection and when multiple connections with requests
//    are present. Does interleaving of method calls across request identity
//    or connection identity affect the behavior of the calls?
// 3) Behavior of non-output methods before request completion. In
//    particular, AbortStatus.
// 4) Behavior of FcgiRequest methods after the completion of the request
//    by a call to Complete or RejectRole. This includes output methods and
//    other methods.
//
// Test cases:
//
// Modules which testing depends on:
//
// Other modules whose testing depends on this module:
TEST(FcgiServerInterface, FcgiRequestDataTransmissionAndCompletion)
{
  
}

// FcgiServerInterfaceDestructionNotSynchronization
// Testing explanation
// Examined properties:
// 1) Destruction of completed and uncompleted FcgiRequest objects followed
//    by destruction of the interface.
// 2) Destruction of the interface while completed and uncompleted
//    FcgiRequest objects are present. Method calls on FcgiRequest objects
//    should behave as specified.
// 3) Creation of a new interface which creates new requests while completed
//    and uncompleted requests from the previous interface are present.
//    Method calls on all requests should function as specified. In
//    particular, the clients which sent the new requests should be able to
//    receive correct responses.
//
// Test cases:
//
//
// Modules which testing depends on:
//
// Other modules whose testing depends on this module:
TEST(FcgiServerInterface, FcgiServerInterfaceDestructionNotSynchronization)
{

}

} // namespace test
} // namespace fcgi
} // namespace as_components
