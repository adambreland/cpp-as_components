#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>         // <cstdlib> does not define setenv. 
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "fcgi_si.h"
#include "test/fcgi_si_testing_utilities.h"
#include "external/socket_functions/include/socket_functions.h"

// Key:
// BAZEL DEPENDENCY       This marks use of a feature which is provided by the
//                        Bazel testing run-time environment. 

std::string CaseSuffix(int test_case)
{
  std::string case_suffix {" case "};
  case_suffix += std::to_string(test_case);
  case_suffix += ".";
  return case_suffix;
}

void SIGALRMHandlerInstaller()
{
  sigset_t empty_signal_set {};
  if(sigemptyset(&empty_signal_set) == -1)
    FAIL() << "A call to sigemptyset failed." << '\n' << std::strerror(errno);
  struct sigaction new_sigalrm_disposition {};
  new_sigalrm_disposition.sa_handler = SIG_DFL;
  new_sigalrm_disposition.sa_mask = empty_signal_set;
  new_sigalrm_disposition.sa_flags = 0;
  if(sigaction(SIGALRM, &new_sigalrm_disposition, nullptr) == -1)
    FAIL() << "A call to sigaction failed" << '\n' << std::strerror(errno);
}

// Creates a listening socket for an interface and constructs an interface
// instance on the heap. Access is provided by a returned unique_ptr to the
// interface. The provided domain is used when the listening socket is created.
//
// Exceptions:
// 1) Throws any exceptions thrown by the constructor of
//    fcgi_si::FCGIServerInterface. If a throw occurs, the interface socket
//    file descriptor was closed.
//
// Resource allocation:
// 1) If successfully constructed, a listening socket is created. This socket
//    should be closed when the interface instance is no longer needed to
//    prevent a file descriptor leak.
//
// Effects:
// 1) If construction was successful:
//    a) std::get<0> accesses a unique_ptr which points to the interface.
//    b) std::get<1> accesses the descriptor value of the listening socket of
//       the interface. 
//    c) std::get<2> accesses the port of the listening socket of the interface.
//       The value is in network byte order. When a UNIX domain socket was
//       created, zero is present.
//    d) std::get<3> accesses the file path of the UNIX domain socket. When
//       an internet domain socket was created, the string is empty.
// 2) If construction was not successful, the unique_ptr accessed by 
//    std::get<0> holds nullptr. If a socket was created, its descriptor
//    was closed.
std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t, 
  std::string>
CreateInterface(int domain, int max_connections, int max_requests,
  int app_status)
{
  std::unique_ptr<fcgi_si::FCGIServerInterface> interface_uptr {};
  std::string unix_path {};
  int socket_fd {socket(domain, SOCK_STREAM, 0)};

  auto CleanupForReturn = [&interface_uptr, &unix_path, socket_fd]
    (std::string message, int errno_value)->
      std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t, 
        std::string>
  {
    if(errno_value)
      ADD_FAILURE() << message << '\n' << std::strerror(errno_value);
    else
      ADD_FAILURE() << message;
    if(socket_fd != -1)
      close(socket_fd);
    return std::make_tuple(std::move(interface_uptr), socket_fd, 0U, 
      std::move(unix_path));
  };

  if(socket_fd < 0)
    return CleanupForReturn("A call to socket failed.", errno);

  if(domain == AF_UNIX)
  {
    unix_path = "/tmp/fcgi_si_test_UNIX_interface_socket";
    struct sockaddr_un AF_UNIX_addr {};
    AF_UNIX_addr.sun_family = AF_UNIX;
    std::strcpy(AF_UNIX_addr.sun_path, unix_path.data());
    if(bind(socket_fd, 
      static_cast<struct sockaddr*>(static_cast<void*>(&AF_UNIX_addr)),
      sizeof(struct sockaddr_un)) == -1)
      return CleanupForReturn("A call to bind for a UNIX socket failed.",
        errno);
    if(unlink(unix_path.data()) == -1)
      return CleanupForReturn("A call to unlink for a UNIX socket failed.",
        errno);
  }  

  if(listen(socket_fd, 5) < 0)
    return CleanupForReturn("A call to listen failed.", errno);

  // Generic state to be used to extract the address of the listening
  // socket when an internet domain is used.
  struct sockaddr* address_ptr {nullptr};
  socklen_t*       length_ptr  {nullptr};
  
  // State for internet domain sockets.
  // AF_INET
  struct sockaddr_in AF_INET_addr {};
  socklen_t AF_INET_socklen {sizeof(sockaddr_in)};

  // AF_INET6
  struct sockaddr_in6 AF_INET6_addr {};
  socklen_t AF_INET6_socklen {sizeof(sockaddr_in6)};

  if(domain != AF_UNIX)
  {
    if((domain == AF_INET) || (domain == AF_INET6))
    {
      if(domain == AF_INET)
      {
        address_ptr = static_cast<struct sockaddr*>(static_cast<void*>(
          &AF_INET_addr));
        length_ptr = &AF_INET_socklen;
      }
      else if(domain == AF_INET6)
      {
        address_ptr = static_cast<struct sockaddr*>(static_cast<void*>(
          &AF_INET6_addr));
        length_ptr = &AF_INET6_socklen;
      }
      if(getsockname(socket_fd, address_ptr, length_ptr) < 0)
        return CleanupForReturn("A call to getsockname failed.", errno);
    }
    else
      return CleanupForReturn("An invalid domain was given.", 0);
  }

  try
  {
    interface_uptr = std::unique_ptr<fcgi_si::FCGIServerInterface> 
      {new fcgi_si::FCGIServerInterface {socket_fd, max_connections, 
        max_requests, app_status}};
  }
  catch(...)
  {
    close(socket_fd);
    throw;
  }
  
  return std::make_tuple(
    std::move(interface_uptr), 
    socket_fd, 
    (domain == AF_UNIX) ? 
      0U : 
      ((domain == AF_INET) ? 
        AF_INET_addr.sin_port :
        AF_INET6_addr.sin6_port), 
    std::move(unix_path)
  );
}

TEST(FCGIServerInterface, ConstructionExceptionsAndDirectlyObservableEffects)
{
  // Testing explanation
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
  // Modules which testing depends on: none.
  //
  // Other modules whose testing depends on this module: none.

  auto ClearFCGIWebServerAddrs = []()->void
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
      FAIL() << "setenv failed" << '\n' << std::strerror(errno);
  };

  // Ensure that FCGI_WEB_SERVER_ADDRS is bound and empty to establish a
  // consistent start state.
  ClearFCGIWebServerAddrs();

  // Case 1: listening_descriptor refers to a file which is not a socket.
  // Create a temporary regular file.
  {
    int temp_fd {};
    // BAZEL DEPENDENCY
    fcgi_si_test::CreateBazelTemporaryFile(&temp_fd);
    EXPECT_THROW(fcgi_si::FCGIServerInterface(temp_fd, 1, 1), std::exception);
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
        sizeof(struct sockaddr_in)) < 0)
      { 
        ADD_FAILURE() << "A call to bind failed in case 2." << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1),
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
        sizeof(struct sockaddr_in)) < 0)
      { 
        ADD_FAILURE() << "A call to bind failed in case 3." << '\n' 
          << std::strerror(errno);
        close(socket_fd);
      }
      else
      {
        EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1),
          std::exception);
        close(socket_fd);
      }
    }
  }

  auto FCGIWebServerAddrsCase = 
    [&ClearFCGIWebServerAddrs](const char* address_list_ptr, int domain, 
      int test_case)->void
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
        ClearFCGIWebServerAddrs();
      }
      else
      {
        if(listen(socket_fd, 5) < 0)
        {
          ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n'
            << std::strerror(errno);
          close(socket_fd);
          ClearFCGIWebServerAddrs();
        }
        else
        {
          EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1),
            std::exception);
          close(socket_fd);
          ClearFCGIWebServerAddrs();
        }
      }
    }
  };

  // Case 4: The socket is of domain AF_INET and only IPv6 addresses are
  // present.
  FCGIWebServerAddrsCase("::1", AF_INET, 4);

  // Case 5: The socket is of domain AF_INET6 and only IPv4 addresses are
  // present.
  FCGIWebServerAddrsCase("127.0.0.1", AF_INET6, 5);
  
  // Case 6: The socket is of domain AF_INET and a combination of invalid IPv4
  // addresses and valid IPv6 addresses are present. "Invalid" means malformed.
  FCGIWebServerAddrsCase("::,127.0.0.256,::1,0.0.0.0.0", AF_INET, 6);

  // Case 7: The socket is of domain AF_INET and only a comma is present.
  FCGIWebServerAddrsCase(",", AF_INET, 7);

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
        EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, max_connections, 
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
        fcgi_si::FCGIServerInterface interface {socket_fd, 1, 1};
        EXPECT_THROW(fcgi_si::FCGIServerInterface(socket_fd, 1, 1), 
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
      fcgi_si::FCGIServerInterface interface(socket_fd, max_connections,
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
      ClearFCGIWebServerAddrs();
    }
  }

  // Case 14: FCGI_WEB_SERVER_ADDRS is bound and empty. The descriptor is a
  // valid socket.
  {
    ClearFCGIWebServerAddrs();
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
        sizeof(struct sockaddr_un)) < 0)
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
      ClearFCGIWebServerAddrs();
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
      ClearFCGIWebServerAddrs();
    }
  }
}

TEST(FCGIServerInterface, FCGIGetValues)
{
  // Testing explanation
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
  //
  // Test cases: All cases use an interface which accepts a single request
  // and a single connection at a time.
  // 1) An empty request.
  // 2) Only known names. All three known names.
  // 3) Only known names. A single known name. Three variations for each of
  //    the known names.
  // 4) Unknown name present. A single-byte unknown name in the first position.
  //    All three known names follow. Then an empty name.
  // 5) Unknown name present. A four-byte unknown name in the first position.
  //    All three known names follow.
  // 6) Unknown name present. A known name, then a single-byte unknown name,
  //    then a known name.
  // 7) Unknown name present. A known name, then a four-byte unknown name, then
  //    a known name.
  // 8) All unknown names.
  // 
  // Modules which testing depends on:
  // 1) fcgi_si::EncodeNameValuePairs
  // 2) fcgi_si::ExtractBinaryNameValuePairs
  // 3) fcgi_si::PopulateHeader
  // 4) socket_functions::ScatterGatherSocketWrite
  // 5) socket_functions::SocketRead
  //
  // Other modules whose testing depends on this module: none.

  SIGALRMHandlerInstaller();

  // Lambda functions for test case implementations.
  struct ScatterGatherSocketWriteArgs
  {
    struct iovec* iovec_ptr;
    int iovec_count;
    std::size_t number_to_write;
  };

  auto FCGIGetValuesTest = [](ScatterGatherSocketWriteArgs a,
    const std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>&
      expected_result, int test_case)->void
  {

    // Generate string " case test_case." for case x.
    std::string case_suffix {CaseSuffix(test_case)};

    int client_socket_fd {-1};
    std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t,
      std::string>
    inter_tuple {CreateInterface(AF_INET, 1, 1, EXIT_FAILURE)};

    auto SocketClosure = [&inter_tuple, client_socket_fd]()->void
    {
      if(std::get<1>(inter_tuple) >= 0)
        close(std::get<1>(inter_tuple));
      if(client_socket_fd >= 0)
        close(client_socket_fd);
    };

    if(!std::get<0>(inter_tuple))
    {
      ADD_FAILURE() << "Construction of the interface failed in" << case_suffix;
      return;
    }

    client_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(client_socket_fd < 0)
    {
      ADD_FAILURE() << "Creation of a socket for the client failed in" << 
        case_suffix << '\n' << std::strerror(errno);
      SocketClosure();
      return;
    }

    int f_getfl_return {fcntl(client_socket_fd, F_GETFL)};
    if(f_getfl_return == -1)
    {
      ADD_FAILURE() << "A call to fcntl with F_GETFL for the client socket "
        "failed in" << case_suffix << '\n' << std::strerror(errno);
      SocketClosure();
      return;
    }
    f_getfl_return |= O_NONBLOCK;
    if(fcntl(client_socket_fd, F_SETFL, f_getfl_return) == -1)
    {
      ADD_FAILURE() << "A call to fcntl with F_SETFL for the client socket "
        "failed in" << case_suffix << '\n' << std::strerror(errno);
      SocketClosure();
      return;
    }
    
    struct sockaddr_in interface_addr {};
    interface_addr.sin_family = AF_INET;
    interface_addr.sin_port = std::get<2>(inter_tuple);
    interface_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if(connect(
      client_socket_fd, 
      static_cast<struct sockaddr*>(static_cast<void*>(&interface_addr)),
      sizeof(struct sockaddr_in)) != -1 || errno != EINPROGRESS)
    {
      ADD_FAILURE() << "A call to connect failed in" << case_suffix
        << '\n' << std::strerror(errno);
      SocketClosure();
      return;
    }

    // Allow the interface to process the connection.
    // Set an alarm and clear it when AcceptRequests returns. The SIGALRM
    // handler installed at the start of the test will cause normal program
    // termination if the alarm goes off.
    alarm(1U);
    std::vector<fcgi_si::FCGIRequest> accept_return 
      {std::get<0>(inter_tuple)->AcceptRequests()};
    alarm(0U);
    if(accept_return.size())
    {
      ADD_FAILURE() << "A request was returned when none was expected in"
        << case_suffix;
      SocketClosure();
      return;
    }

    // The connection process should have completed.

    if(std::get<2>(socket_functions::ScatterGatherSocketWrite(
      client_socket_fd, a.iovec_ptr, a.iovec_count, a.number_to_write)) != 0U)
    {
      ADD_FAILURE() << "An error occurred while writing to the interface in"
        << case_suffix << '\n' << std::strerror(errno);
      SocketClosure();
      return;
    }

    // Allow the interface to process the FCGI_GET_VALUES request (record).
    alarm(1U);
    std::vector<fcgi_si::FCGIRequest> get_values_accept_return 
      {std::get<0>(inter_tuple)->AcceptRequests()};
    alarm(0U);
    if(get_values_accept_return.size())
    {
      ADD_FAILURE() << "A request was returned when none was expected in"
        << case_suffix;
      SocketClosure();
      return;
    }

    std::uint8_t read_buffer[128U];
    std::vector<std::uint8_t> returned_result {};
    bool read {true};
    while(read)
    {
      std::size_t read_return 
        {socket_functions::SocketRead(client_socket_fd, read_buffer, 128U)};
      returned_result.insert(returned_result.end(), read_buffer, 
        read_buffer + read_return);
      if(read_return < 128U)
      {
        read = false;
        if((errno != EAGAIN) && (errno != EWOULDBLOCK))
        {
          ADD_FAILURE() << "An error occurred while reading the response from "
            "the interface in" << case_suffix << '\n' << std::strerror(errno);
          SocketClosure();
          return;
        }
      }
    }

    if((returned_result.size() < fcgi_si::FCGI_HEADER_LEN) || 
       (returned_result[1] != static_cast<std::uint8_t>(
         fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT)))
    {
      ADD_FAILURE() << "The output from the interface was incorrect in" 
        << case_suffix;
      SocketClosure();
      return;
    }
    // As uint16_t is shorter than int in most cases, integral promotion to int
    // causes a narrowing warning if conversion to uint16_t is performed 
    // directly on the arguments to << and +.
    std::uint16_t record_length 
    {
      std::uint16_t(
         (std::int_fast32_t(returned_result[fcgi_si::kHeaderContentLengthB1Index]) << 8) +
          std::int_fast32_t(returned_result[fcgi_si::kHeaderContentLengthB0Index])
      )
    };
    if((returned_result.size() - fcgi_si::FCGI_HEADER_LEN) < record_length)
    {
      ADD_FAILURE() << "A discrepancy between the specified content length and "
        "the actual content length of the FCGI_GET_VALUES_RESULT record was "
        "present in" << case_suffix;
      SocketClosure();
      return;
    }
    std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
    returned_pairs {};
    if(record_length > 0)
    {
      returned_pairs = fcgi_si::ExtractBinaryNameValuePairs(
        returned_result.data() + fcgi_si::FCGI_HEADER_LEN,
        record_length);
      if(returned_pairs.size() == 0)
      {
        ADD_FAILURE() << "An encoding error was present in the binary name-"
          "value pair content returned by the interface in" << case_suffix;
        SocketClosure();
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
      SocketClosure();
      return;
    }
  };

  using pair_vector = std::vector<std::pair<std::vector<std::uint8_t>, 
    std::vector<std::uint8_t>>>;

  // Case 1: Empty FCGI_GET_VALUES record
  {
    std::uint8_t header[fcgi_si::FCGI_HEADER_LEN];
      fcgi_si::PopulateHeader(header, fcgi_si::FCGIType::kFCGI_GET_VALUES,
        0U, 0U, 0U);
    struct iovec iovec_array[1] = {};
    iovec_array[0].iov_base = &header;
    iovec_array[0].iov_len  = fcgi_si::FCGI_HEADER_LEN;
    ScatterGatherSocketWriteArgs args {};
    args.iovec_ptr       = iovec_array;
    args.iovec_count     = 1;
    args.number_to_write = fcgi_si::FCGI_HEADER_LEN;
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map {};
    FCGIGetValuesTest(args, pair_map, 1);
  }
  
  auto TestCaseRunner = [&FCGIGetValuesTest](pair_vector input_pairs, 
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> input_map,
    int test_case)
  {
    std::tuple<bool, std::size_t, std::vector<iovec>, 
      const std::vector<std::uint8_t>, std::size_t, pair_vector::iterator>
    returned_encoding_information {fcgi_si::EncodeNameValuePairs(
      input_pairs.begin(), input_pairs.end(), fcgi_si::FCGIType::kFCGI_GET_VALUES,
      0U, 0U)};
    if((!std::get<0>(returned_encoding_information)) || 
       (std::get<4>(returned_encoding_information) != 0U))
    {
      ADD_FAILURE() << "The name-value pairs given to EncodeNameValuePairs "
        "caused an error.";
      return;
    }
    ScatterGatherSocketWriteArgs args {};
    args.iovec_ptr       = std::get<2>(returned_encoding_information).data();
    args.iovec_count     = std::get<2>(returned_encoding_information).size();
    args.number_to_write = std::get<1>(returned_encoding_information);

    FCGIGetValuesTest(args, input_map, test_case);
  };

  // Case 2: Only known names. All three known names.
  {
    pair_vector nv_pairs 
      {{fcgi_si::FCGI_MAX_CONNS, {}}, 
       {fcgi_si::FCGI_MAX_REQS, {}}, 
       {fcgi_si::FCGI_MPXS_CONNS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{fcgi_si::FCGI_MAX_CONNS, {'1'}},
       {fcgi_si::FCGI_MAX_REQS, {'1'}},
       {fcgi_si::FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 2);
  }

  // Case 3: Only known names. A single known name. Three variations for each of
  // the known names.
  {
    // FCGI_MAX_CONNS
    pair_vector nv_pairs {{fcgi_si::FCGI_MAX_CONNS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{fcgi_si::FCGI_MAX_CONNS, {'1'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 3);

    // FCGI_MAX_REQS
    nv_pairs.clear();
    nv_pairs.push_back({fcgi_si::FCGI_MAX_REQS, {}});
    pair_map.clear();
    pair_map.insert({fcgi_si::FCGI_MAX_REQS, {'1'}});
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 3);

    // FCGI_MPXS_CONNS
    nv_pairs.clear();
    nv_pairs.push_back({fcgi_si::FCGI_MPXS_CONNS, {}});
    pair_map.clear();
    pair_map.insert({fcgi_si::FCGI_MPXS_CONNS, {'0'}});
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 3);
  }

  // Case 4: Unknown name present. A single-byte unknown name in the
  // first position. All three known names follow.
  {
    pair_vector nv_pairs 
      {{{'R', 'O', 'L', 'E'}, {}},
       {fcgi_si::FCGI_MAX_CONNS, {}}, 
       {fcgi_si::FCGI_MAX_REQS, {}}, 
       {fcgi_si::FCGI_MPXS_CONNS, {}},
       {{}, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{fcgi_si::FCGI_MAX_CONNS, {'1'}},
       {fcgi_si::FCGI_MAX_REQS, {'1'}},
       {fcgi_si::FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 4);
  }

  // Case 5: Unknown name present. A four-byte unknown name in the first 
  // position. All three known names follow.
  {
    pair_vector nv_pairs 
      {{std::vector<std::uint8_t>(200, 'A'), {}},
       {fcgi_si::FCGI_MAX_CONNS, {}}, 
       {fcgi_si::FCGI_MAX_REQS, {}}, 
       {fcgi_si::FCGI_MPXS_CONNS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{fcgi_si::FCGI_MAX_CONNS, {'1'}},
       {fcgi_si::FCGI_MAX_REQS, {'1'}},
       {fcgi_si::FCGI_MPXS_CONNS, {'0'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 5);
  }
  
  // Case 6: Unknown name present. A known name, then a single-byte unknown
  // name, then a known name.
  {
    pair_vector nv_pairs 
      {{fcgi_si::FCGI_MAX_CONNS, {}},
       {{'S','E','R','V','E','R'}, {}},
       {fcgi_si::FCGI_MAX_REQS, {}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
      {{fcgi_si::FCGI_MAX_CONNS, {'1'}},
       {fcgi_si::FCGI_MAX_REQS, {'1'}}};
    TestCaseRunner(std::move(nv_pairs), std::move(pair_map), 6);
  }
  
  // Case 7: Unknown name present. A known name, then a four-byte unknown name,
  // then a known name.
  {
    pair_vector nv_pairs 
      {{fcgi_si::FCGI_MAX_CONNS, {}},
       {std::vector<std::uint8_t>(200, 'A'), {}},
      {fcgi_si::FCGI_MPXS_CONNS, {'0'}}};
    std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>> pair_map
    {{fcgi_si::FCGI_MAX_CONNS, {'1'}},
      {fcgi_si::FCGI_MPXS_CONNS, {'0'}}};
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
}

TEST(FCGIServerInterface, ConnectionAcceptanceAndRejection)
{
  // Testing explanation
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
  // 2) max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
  // 3) max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET6.
  // 4) max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
  // 5) max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty, a previous
  //    connection was made, and the interface was placed in an overloaded
  //    state.
  // 6) FCGI_WEB_SERVER_ADDRS contains the IPv4 loopback address 127.0.0.1. 
  //    A client with address 127.0.0.1 attempts to make a connection and it
  //    succeeds. A client with address 127.0.0.2 attempts to make a connection
  //    and it fails.
  // 7) FCGI_WEB_SERVER_ADDRS contains the IPv6 loopback address fd00::1.
  //    A client with IPv6 loopback address ::1 tries to make a connection
  //    and it fails.
  // 
  // Modules which testing depends on:
  // 1) fcgi_si::PopulateHeader
  // 2) socket_functions::SocketRead
  //
  // Modules whose testing depends on this module: none.

  SIGALRMHandlerInstaller();

  // Ignore SIGPIPE.
  struct sigaction sigpipe_disp {};
  sigpipe_disp.sa_handler = SIG_IGN;
  if(sigemptyset(&sigpipe_disp.sa_mask) == -1)
  {
    FAIL() << "A call to sigemptyset failed." << '\n' << std::strerror(errno);
  }
  sigpipe_disp.sa_flags = 0;
  if(sigaction(SIGPIPE, &sigpipe_disp, nullptr) == -1)
  {
    FAIL() << "A call to sigaction to ignore SIGPIPE failed." << '\n'
      << std::strerror(errno);
  }

  // An arugment structure for TestCaseRunner.
  struct TestArguments
  {
    int domain;
    int max_connections;
    int max_requests;
    int app_status;
    int initial_connections;
    int overload_after;
    int test_case;
  };

  //   Creates an interface with the provided construction arguments (the
  // listening socket is implicitly created) and creates
  // initial_connections + 1 clients which connect to the interface.
  // The overload integer indicates the connection number after which the
  // interface is put into an overloaded state through a call to
  // set_overload(true).
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
  auto TestCaseRunner = [](TestArguments args)->void
  {
    std::string case_suffix {CaseSuffix(args.test_case)};

     if(!((args.domain == AF_UNIX) || (args.domain == AF_INET) ||
         (args.domain == AF_INET6)))
    {
      ADD_FAILURE() << "Invalid domain argument in" << case_suffix;
      return;
    }

    auto ConditionalClosure = [](int descriptor)->void
    {
      if(descriptor >= 0)
        close(descriptor);
    };

    // Note that the interface socket needs to be closed after use when
    // construction succeeds.
    std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t,
      std::string>
    inter_tuple {CreateInterface(args.domain, args.max_connections, 
      args.max_requests, args.app_status)};
    if(!std::get<0>(inter_tuple))
    {
      ADD_FAILURE() << "Interface construction failed in" << case_suffix;
      return;
    }

    // Address state to allow connection to the interface and to allow
    // "clients" to be bound to specific addresses for three address families.
    // AF_UNIX state.
    struct sockaddr_un AF_UNIX_interface_addr {};
    if(args.domain == AF_UNIX)
    {
      AF_UNIX_interface_addr.sun_family = AF_UNIX;
      std::strcpy(AF_UNIX_interface_addr.sun_path, 
        std::get<3>(inter_tuple).data());
    }
    // AF_INET state.
    struct sockaddr_in AF_INET_interface_addr {};
    struct sockaddr_in AF_INET_client_addr {};
      // The numeric value of the loopback address is stored in host byte
      // order to allow incremental address binding.
    std::uint32_t AF_INET_ip_address {};
    if(args.domain == AF_INET)
    {
      // To allow clients to connect to the interface socket.
      AF_INET_interface_addr.sin_family      = AF_INET;
      AF_INET_interface_addr.sin_port        = std::get<2>(inter_tuple);
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
    if(args.domain == AF_INET6)
    {
      // To allow clients to connect to the interface socket.
      AF_INET6_interface_addr.sin6_family = AF_INET6;
      AF_INET6_interface_addr.sin6_port   = std::get<2>(inter_tuple);
      AF_INET6_interface_addr.sin6_addr   = in6addr_loopback;

      AF_INET6_client_addr.sin6_family = AF_INET6;
      AF_INET6_client_addr.sin6_port   = htons(0U);
      AF_INET6_client_addr.sin6_addr   = in6addr_loopback;
    }

    std::vector<int> client_socket_descriptor_list(args.initial_connections + 1,
      -1);

    // Performs closure of any valid client sockets and the interface socket.
    auto DescriptorCleanup = [&ConditionalClosure, &inter_tuple,
      &client_socket_descriptor_list]()->void
    {
      std::for_each(client_socket_descriptor_list.begin(),
        client_socket_descriptor_list.end(), ConditionalClosure);
      close(std::get<1>(inter_tuple));
    };

    struct sockaddr* interface_addr_ptr {nullptr};
    struct sockaddr* client_addr_ptr {nullptr};
    socklen_t socket_addr_length {};
    if(args.domain == AF_UNIX)
    {
      interface_addr_ptr = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_UNIX_interface_addr));
      socket_addr_length = sizeof(struct sockaddr_un);
    }
    else if(args.domain == AF_INET)
    {
      interface_addr_ptr = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET_interface_addr));
      client_addr_ptr    = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET_client_addr));
      socket_addr_length = sizeof(struct sockaddr_in);
    }
    else
    {
      interface_addr_ptr = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET6_interface_addr));
      client_addr_ptr    = static_cast<struct sockaddr*>(
        static_cast<void*>(&AF_INET6_client_addr));
      socket_addr_length = sizeof(struct sockaddr_in6);
    }

    for(int i {0}; i < (args.initial_connections + 1); ++i)
    {
      if(i == args.overload_after)
        std::get<0>(inter_tuple)->set_overload(true);

      client_socket_descriptor_list[i] = socket(args.domain, SOCK_STREAM, 0);
      if(client_socket_descriptor_list[i] < 0)
      {
        ADD_FAILURE() << "An error occurred when creating a client socket in"
          << case_suffix << '\n' << std::strerror(errno);
        DescriptorCleanup();
        return;
      }
      int f_getfl_return {fcntl(client_socket_descriptor_list[i], F_GETFL)};
      if(f_getfl_return == -1)
      {
        ADD_FAILURE() << "A call to fcntl to get the socket descriptor file "
          "status flags failed in" << case_suffix << '\n' 
          << std::strerror(errno);
        DescriptorCleanup();
        return;
      }
      f_getfl_return |= O_NONBLOCK;
      if(fcntl(client_socket_descriptor_list[i], F_SETFL, f_getfl_return) 
         == -1)
      {
        ADD_FAILURE() << "A call to fcntl to make a client socket non-blocking "
          "failed in" << case_suffix << '\n' << std::strerror(errno);
        DescriptorCleanup();
        return;
      }
      // Bind the client to a specific address.
      if((args.domain == AF_INET) || (args.domain == AF_INET6))
      {
        if(bind(client_socket_descriptor_list[i], client_addr_ptr, 
          socket_addr_length) == -1)
        { 
          ADD_FAILURE() << "A client socket could not be bound to a loopback "
            << "address in" << case_suffix << '\n' << strerror(errno);
          DescriptorCleanup();
          return;
        }
      }
      if((connect(client_socket_descriptor_list[i], interface_addr_ptr, 
        socket_addr_length) != -1) || (errno != EINPROGRESS))
      {
        ADD_FAILURE() << "A call to connect did not return EINPROGRESS as "
          "expected in" << case_suffix << '\n' << strerror(errno);
        DescriptorCleanup();
        return;
      }
      // Allow the interface to process the connection.
      alarm(1U);
      try
      {
        std::get<0>(inter_tuple)->AcceptRequests();
      }
      catch(std::system_error& error)
      {
        FAIL() << "An excpetion was caught when AcceptRequests was called."
          << '\n' << "errno: " << error.code().value() << '\n' 
          << std::strerror(error.code().value()) << '\n' << error.what();
      }
      catch(std::exception& error)
      {
        FAIL() << "An excpetion was caught when AcceptRequests was called."
          << error.what();
      }
      alarm(0U);
      // The connection should have occurred.

      // Update address state.
      // No action for AF_UNIX.
      if(args.domain == AF_INET)
      {
        AF_INET_ip_address++;
        AF_INET_client_addr.sin_addr.s_addr = htonl(AF_INET_ip_address);
      }
      // No action for AF_INET6.
    }

    // Note that, since the client socket is non-blocking, a write will succeed
    // in most cases regardless of whether the peer (interface) closed the
    // its side of the connection. A read is performed to detect connection
    // closure. socket_functions::SocketRead reports this as errno == 0 with
    // a short count.
    std::uint8_t read_buffer[fcgi_si::FCGI_HEADER_LEN] = {};
    std::size_t read_return {socket_functions::SocketRead(
      client_socket_descriptor_list[args.initial_connections], read_buffer,
      fcgi_si::FCGI_HEADER_LEN)};
    if(read_return > 0 || (read_return == 0 && (errno != 0)))
    {
      ADD_FAILURE() << "The connection was not rejected as expected in"
        << case_suffix << '\n' << "Connection count: " 
        << int(std::get<0>(inter_tuple)->connection_count());
      if(read_return == 0)
        ADD_FAILURE() << std::strerror(errno);
    }
    DescriptorCleanup();
  };

  if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
  {
    FAIL() << "FCGI_WEB_SERVER_ADDRS could not be cleared."
      << '\n' << std::strerror(errno);
    return;
  }

  // // Case 1: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_UNIX.
  // {
  //   struct TestArguments args {};
  //   args.domain              = AF_UNIX;
  //   args.max_connections     = 1;
  //   args.max_requests        = 1;
  //   args.app_status          = EXIT_FAILURE;
  //   args.initial_connections = 1;
  //   args.overload_after      = 1;
  //   args.test_case           = 1;

  //   TestCaseRunner(args);
  // }

  // Case 2: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
  {
    struct TestArguments args {};
    args.domain              = AF_INET;
    args.max_connections     = 1;
    args.max_requests        = 1;
    args.app_status          = EXIT_FAILURE;
    args.initial_connections = 1;
    args.overload_after      = 2; // No overload.
    args.test_case           = 2;

    TestCaseRunner(args);
  }

  // // Case 3: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET6.
  // {
  //   struct TestArguments args {};
  //   args.domain              = AF_INET6;
  //   args.max_connections     = 1;
  //   args.max_requests        = 1;
  //   args.app_status          = EXIT_FAILURE;
  //   args.initial_connections = 1;
  //   args.overload_after      = 2; // No overload.
  //   args.test_case           = 3;

  //   TestCaseRunner(args);
  // }

  // // Case 4: max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty.
  // {
  //   struct TestArguments args {};
  //   args.domain              = AF_INET;
  //   args.max_connections     = 5;
  //   args.max_requests        = 10;
  //   args.app_status          = EXIT_FAILURE;
  //   args.initial_connections = 5;
  //   args.overload_after      = 6; // No overload.
  //   args.test_case           = 4;

  //   TestCaseRunner(args);
  // }
  

  // Case 3: max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty, a previous
  // connection was made, and the interface was placed in an overloaded state.


  // 4) FCGI_WEB_SERVER_ADDRS contains the loopback addresses 127.0.0.1. 
  //    A client with address 127.0.0.1 attempts to make a
  //    connection and it succeeds. A client with address 127.0.0.2 attempts
  //    to make a connection and it fails.

  // Restore the default SIGPIPE disposition.
  sigpipe_disp.sa_handler = SIG_DFL;
  if(sigaction(SIGPIPE, &sigpipe_disp, nullptr) == -1)
  {
    FAIL() << "A call to sigaction to restore the defailt SIGPIPE  behavior "
      "failed." << '\n' << std::strerror(errno);
  }
}
