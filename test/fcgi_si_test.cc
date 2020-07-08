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
#include <system_error>
#include <tuple>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "fcgi_si.h"
#include "test/fcgi_si_testing_utilities.h"
#include "external/socket_functions/include/socket_functions.h"

// Key:
// BAZEL DEPENDENCY       This marks use of a feature which is provided by the
//                        Bazel testing run-time environment. 

// Utility functions for interface tests.

std::string CaseSuffix(int test_case)
{
  std::string case_suffix {" case "};
  case_suffix += std::to_string(test_case);
  case_suffix += ".";
  return case_suffix;
}

// This function sets the disposition of SIGALRM to the default
// action (termination).
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
// Preconditions:
// 1) If domain == AF_UNIX, the length of the string pointed to by unix_path
//    including the terminating null byte must be less than or equal to the
//    path length limit of UNIX sockets.
//
// Exceptions:
// 1) Throws any exceptions thrown by the constructor of
//    fcgi_si::FCGIServerInterface. If a throw occurs, the interface socket
//    file descriptor was closed.
// 2) Throws std::system_error if a file for a UNIX socket was created and it
//    could not be removed when creation was unsuccessful.
//
// Resource allocation and caller responsibilities:
// 1) On return a listening socket was created. This socket should be closed
//    when the interface instance is no longer needed to prevent a file
//    descriptor leak.
// 2) If domain == AF_UNIX, on return a socket file given by the path string
//    pointed to by unix_path is present. This file should be removed from the
//    file system when the interface is no longer needed.
//
// Effects:
// 1) If creation was successful:
//    a) std::get<0> accesses a unique_ptr which points to the interface.
//    b) std::get<1> accesses the descriptor value of the listening socket of
//       the interface. 
//    c) std::get<2> accesses the port of the listening socket of the interface.
//       The value is in network byte order. When a UNIX domain socket was
//       created, zero is present.
// 2) If creation was not successful, the unique_ptr accessed by 
//    std::get<0> holds nullptr. If a socket was created, its descriptor
//    was closed. If a socket file was created, it was removed.
std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
CreateInterface(int domain, int max_connections, int max_requests,
  int app_status, const char* unix_path)
{
  std::unique_ptr<fcgi_si::FCGIServerInterface> interface_uptr {};
  if((domain == AF_UNIX) && !unix_path)
    return std::make_tuple(std::move(interface_uptr), -1, 0U);
  int socket_fd {socket(domain, SOCK_STREAM, 0)};
  bool unix_socket_bound {false};

  auto CleanupForFailure = [&interface_uptr, &unix_socket_bound, socket_fd, 
    unix_path]
    (std::string message, int errno_value)->
      std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
  {
    if(errno_value)
      ADD_FAILURE() << message << '\n' << std::strerror(errno_value);
    else
      ADD_FAILURE() << message;

    if(socket_fd != -1)
      close(socket_fd);
    if(unix_socket_bound)
    {
      if(unlink(unix_path) == -1)
      {
        ADD_FAILURE() << "The UNIX socket created by a call to CreateInterface "
          "could not be removed during cleanup.";
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "unlink"};
      }
    }
    return std::make_tuple(std::move(interface_uptr), socket_fd, 0U);
  };

  if(socket_fd < 0)
    return CleanupForFailure("A call to socket failed.", errno);
  if(domain == AF_UNIX)
  {
    struct sockaddr_un AF_UNIX_addr {};
    AF_UNIX_addr.sun_family = AF_UNIX;
    std::strcpy(AF_UNIX_addr.sun_path, unix_path);
    if(bind(socket_fd, 
      static_cast<struct sockaddr*>(static_cast<void*>(&AF_UNIX_addr)),
      sizeof(struct sockaddr_un)) == -1)
      return CleanupForFailure("A call to bind for a UNIX socket failed.",
        errno);
    unix_socket_bound = true;
  }  

  if(listen(socket_fd, 5) < 0)
    return CleanupForFailure("A call to listen failed.", errno);

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
        return CleanupForFailure("A call to getsockname failed.", errno);
    }
    else
      return CleanupForFailure("An invalid domain was given.", 0);
  }

  try
  {
    interface_uptr = std::unique_ptr<fcgi_si::FCGIServerInterface> 
      {new fcgi_si::FCGIServerInterface {socket_fd, max_connections, 
        max_requests, app_status}};
  }
  catch(...)
  {
    CleanupForFailure("interface construction", 0);
    throw;
  }
  
  return std::make_tuple(
    std::move(interface_uptr), 
    socket_fd, 
    (domain == AF_UNIX) ? 
      0U : 
      ((domain == AF_INET) ? 
        AF_INET_addr.sin_port :
        AF_INET6_addr.sin6_port)
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
    std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
    inter_tuple {CreateInterface(AF_INET, 1, 1, EXIT_FAILURE, nullptr)};

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
    if((connect(
      client_socket_fd, 
      static_cast<struct sockaddr*>(static_cast<void*>(&interface_addr)),
      sizeof(struct sockaddr_in)) != -1) || (errno != EINPROGRESS))
    {
      ADD_FAILURE() << "A call to connect failed in" << case_suffix
        << '\n' << std::strerror(errno);
      SocketClosure();
      return;
    }

    // Allow the interface to process the connection.
    // Set an alarm and clear it when AcceptRequests returns.
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

    // Confirm that observable interface state is as expected.
    EXPECT_EQ(std::get<0>(inter_tuple)->connection_count(), 1U);
    EXPECT_EQ(std::get<0>(inter_tuple)->interface_status(), true);
    EXPECT_EQ(std::get<0>(inter_tuple)->get_overload(), false);

    // Read the FCGI_GET_VALUES_RESULT response.
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
      ADD_FAILURE() << "The output from the interface was formatted "
        "incorrectly in" << case_suffix;
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
  // This test examines the behavior of a newly-created FCGIServerInterface
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
  // 1) socket_functions::SocketRead
  // 2) socker_functions::SocketWrite
  //
  // Modules whose testing depends on this module: none.

  SIGALRMHandlerInstaller();

  // An argument structure for TestCaseRunner.
  //
  // Preconditions: 
  // 1) overload_after > 0.
  struct TestArguments
  {
    int domain;
    int max_connections;
    int max_requests;
    int app_status;
    int initial_connections;
    int overload_after;
    const char* interface_path_ptr;
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
  //
  // Test side-effects relevant to other tests:
  // SIGPIPE will be ignored. The default disposition is restored at the end
  // of the test. Only non-fatal failures are used in the implementation
  // of TestCaseRunner to ensure that restoration takes place.
  auto TestCaseRunner = [](TestArguments args)->void
  {
    std::string case_suffix {CaseSuffix(args.test_case)};

    if(!((args.domain == AF_UNIX) || (args.domain == AF_INET) ||
         (args.domain == AF_INET6)))
    {
      ADD_FAILURE() << "Invalid domain argument in" << case_suffix;
      return;
    }
    // 92 comes from the lowest known size of sun_path in struct sockaddr_un
    // across distributions. One is added to the length as a terminating
    // null byte must be copied as well.
    if((args.domain == AF_UNIX) && 
       ((std::strlen(args.interface_path_ptr) + 1) > 92))
    {
      ADD_FAILURE() << "The interface path was too long in" << case_suffix;
      return;
    }

    int socket_pair_array[2] = {};
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair_array) == -1)
    {
      ADD_FAILURE() << "A call to socketpair failed in" << case_suffix << '\n'
        << std::strerror(errno);
      return;
    }
    
    int total_connections {args.initial_connections + 1};

    // Fork.

    pid_t fork_return {fork()};
    if(fork_return == -1)
    {
      ADD_FAILURE() << "A call to fork failed in" << case_suffix << '\n'
        << std::strerror(errno);
      return;
    }
    if(fork_return == 0) // child
    {
      // Close one of the sockets to allow proper recognition of the
      // termination of the parent process.
      close(socket_pair_array[0]);

      // Block until the parent writes to the socket. sizeof(in_port_t)
      // bytes are expected. These represent the port of the interface for the
      // internet domains and a ready signal for AF_UNIX.
      in_port_t port {};
      std::size_t socket_read {socket_functions::SocketRead(
        socket_pair_array[1],
        static_cast<std::uint8_t*>(static_cast<void*>(&port)), 
        sizeof(in_port_t)
      )};
      if(socket_read < sizeof(in_port_t))
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
      if(args.domain == AF_UNIX)
      {
        AF_UNIX_interface_addr.sun_family = AF_UNIX;
        std::strcpy(AF_UNIX_interface_addr.sun_path, 
          args.interface_path_ptr);
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
      if(args.domain == AF_INET6)
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

      // State for interprocess signalling.
      std::uint8_t null_byte {0U};
      std::uint8_t received_byte {0U};
      std::vector<int> client_socket_descriptor_list(
        total_connections, -1);
      for(int i {0}; i < total_connections; ++i)
      {
        // Create the client socket and connect to the interface.
        client_socket_descriptor_list[i] = socket(args.domain, SOCK_STREAM, 0);
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
        if((args.domain == AF_INET) || (args.domain == AF_INET6))
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
          if(((args.domain == AF_UNIX) && (errno != EAGAIN)) || 
             ((args.domain != AF_UNIX) && (errno != EINPROGRESS)))
            _exit(EXIT_FAILURE);
        }
        // Signal the interface process that a connection was made and wait
        // for the interface to signal that another connection can be made.
        std::size_t signal_to {socket_functions::SocketWrite(
          socket_pair_array[1], &null_byte, 1)};
        if(signal_to < 1)
          _exit(EXIT_FAILURE);
        std::size_t signal_from {socket_functions::SocketRead(
          socket_pair_array[1], &received_byte, 1)};
        if(signal_from < 1)
          _exit(EXIT_FAILURE);

        // Update address state for AF_INET as incremental IP addresses are
        // used.
        if(args.domain == AF_INET)
        {
          AF_INET_ip_address++;
          AF_INET_client_addr.sin_addr.s_addr = htonl(AF_INET_ip_address);
        }
      }
      // On loop exit, the interface signaled that it is ready.
      // Inspect the connections and send back a status report.
      std::vector<std::uint8_t> status_list {};
      for(int i {0}; i < total_connections; ++i)
      {
        std::size_t read_status {socket_functions::SocketRead(
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
      std::size_t status_report_transmission {socket_functions::SocketWrite(
        socket_pair_array[1], status_list.data(), status_list.size())};
      _exit((status_report_transmission < status_list.size()) ? 
        EXIT_FAILURE : EXIT_SUCCESS);
    }
    // else, parent.
    // Close the socket used by the client.
    close(socket_pair_array[1]);
    
    // Note that the interface socket needs to be closed after use when
    // construction succeeds.
    // For AF_UNIX, the socket file also needs to be removed from the
    // filesystem after use.
    std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
    inter_tuple {};
    const char* construction_ex_message {"An exception was thrown by "
      "CreateInterface in"};
    try
    {
      inter_tuple = CreateInterface(args.domain, args.max_connections, 
        args.max_requests, args.app_status, args.interface_path_ptr);
    }
    catch(std::system_error& error)
    {
      ADD_FAILURE() << construction_ex_message << case_suffix << '\n' 
        << "errno: "  << error.code().value() << '\n'
        << std::strerror(error.code().value()) << '\n' << error.what();
      return;
    }
    catch(std::exception& error)
    {
      ADD_FAILURE() << construction_ex_message << case_suffix << '\n'
        << error.what();
      return;
    }
    if(!std::get<0>(inter_tuple))
    {
      ADD_FAILURE() << "Interface construction failed in" << case_suffix;
      return;
    }

    auto CleanupForExit = [&args, &inter_tuple, &case_suffix]()->void
    {
      close(std::get<1>(inter_tuple));
      if(args.domain == AF_UNIX)
      {
        if(unlink(args.interface_path_ptr) == -1)
          ADD_FAILURE() << "An error occurred when an attempt was made to remove "
            "the UNIX socket file in" << case_suffix;
      }
    };

    // Write the port for internet domains and at least a byte for AF_UNIX.
    std::size_t port_write {socket_functions::SocketWrite(socket_pair_array[0], 
      static_cast<std::uint8_t*>(static_cast<void*>(&std::get<2>(inter_tuple))),
      sizeof(in_port_t))};
    if(port_write < sizeof(in_port_t))
    {
      ADD_FAILURE() << "An error occurred while sending the port to the "
      "process for client sockets in" << case_suffix << '\n'
        << std::strerror(errno);
      CleanupForExit();
      return;
    }
    std::uint8_t null_byte {0U};
    std::uint8_t received_byte {};
    for(int connection_count {1}; 
        connection_count <= total_connections;
        ++connection_count)
    {
      // Wait for client process readiness. A connection should be pending
      // on the interface.
      std::size_t client_signal {socket_functions::SocketRead(
        socket_pair_array[0], &received_byte, 1)};
      if(client_signal < 1)
      {
        if(errno == 0)
          ADD_FAILURE() << "The synchronization socket was found to be closed "
            "when checking for client process readiness in" << case_suffix;
        else
          ADD_FAILURE() << "An error occurred while reading from the "
            "synchronization socket when checking for client process "
            "readiness in" << case_suffix << '\n' << std::strerror(errno);
        CleanupForExit();
        return;
      }
      // Allow the interface to process the connection.
      std::vector<fcgi_si::FCGIRequest> result {};
      const char* exception_message {"An exception was caught when "
        "AcceptRequests was called in"};
      alarm(1U); // If AcceptRequests blocks, kill the process quickly.
      try
      {
        result = std::get<0>(inter_tuple)->AcceptRequests();
      }
      catch(std::system_error& error)
      {
        ADD_FAILURE() << exception_message << case_suffix << '\n' << "errno: " 
          << error.code().value() << '\n'
          << std::strerror(error.code().value()) << '\n' << error.what();
        CleanupForExit();
        return;
      }
      catch(std::exception& error)
      {
        ADD_FAILURE() << exception_message << case_suffix << '\n'
          << error.what();
        CleanupForExit();
        return;
      }
      alarm(0U);
      if(result.size())
      {
        ADD_FAILURE() << "An FCGIRequest object was returned when none was "
          "expected in" << case_suffix;
        CleanupForExit();
        return;
      } 
      if(connection_count == args.overload_after)
        std::get<0>(inter_tuple)->set_overload(true);
      // Signal that the interface processed the connection.
      std::size_t socket_write {socket_functions::SocketWrite(
        socket_pair_array[0], &null_byte, 1)};
      if(socket_write < 1)
      {
        if(errno == EPIPE)
          ADD_FAILURE() << "The synchronization socket was found to be closed "
            "when signalling interface readiness in" << case_suffix;
        else
          ADD_FAILURE() << "An error occurred while signalling interface "
            "in" << case_suffix << '\n' << std::strerror(errno);
        CleanupForExit();
        return;
      }
    }
    // Wait for the connection status report.
    std::vector<std::uint8_t> status_report(total_connections, 0U);
    // WARNING: writes directly to a vector buffer.
    std::size_t status_report_read {socket_functions::SocketRead(
      socket_pair_array[0], status_report.data(), 
      total_connections)};
    if(status_report_read < std::size_t(total_connections))
    {
      if(errno == 0)
        ADD_FAILURE() << "The client process closed the synchronization socket "
          "before the status report could be read in" << case_suffix;
      else
        ADD_FAILURE() << "An error occurred while reading the status report "
            "in" << case_suffix << '\n' << std::strerror(errno);
      CleanupForExit();
      return;
    }
    EXPECT_EQ(status_report, args.expected_status);
    // Verify expected observable interface state.
    EXPECT_EQ(std::get<0>(inter_tuple)->connection_count(), 
      args.initial_connections);
    EXPECT_EQ(std::get<0>(inter_tuple)->interface_status(), true);
    EXPECT_EQ(std::get<0>(inter_tuple)->get_overload(), 
      (args.overload_after == args.initial_connections));
    CleanupForExit();
  };

  if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
  {
    FAIL() << "FCGI_WEB_SERVER_ADDRS could not be cleared."
      << '\n' << std::strerror(errno);
    return;
  }

  // Ignore SIGPIPE. The disposition will be inherited by the child produced
  // by the fork below.
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

  const char* path {"/tmp/fcgi_si_test_UNIX_interface_socket"};

  // Case 1: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_UNIX.
  {
    struct TestArguments args {};
    args.domain              = AF_UNIX;
    args.max_connections     = 1;
    args.max_requests        = 1;
    args.app_status          = EXIT_FAILURE;
    args.initial_connections = 1;
    args.overload_after      = 5; // No overload. Overload should not be seen.
    args.interface_path_ptr  = path;
    args.expected_status     = std::vector<std::uint8_t> {1, 0};
    args.test_case           = 1;

    TestCaseRunner(args);
  }

  // Case 2: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET.
  {
    struct TestArguments args {};
    args.domain              = AF_INET;
    args.max_connections     = 1;
    args.max_requests        = 1;
    args.app_status          = EXIT_FAILURE;
    args.initial_connections = 1;
    args.overload_after      = 5; // No overload. Overload should not be seen.
    args.interface_path_ptr  = path;
    args.expected_status     = std::vector<std::uint8_t> {1, 0};
    args.test_case           = 2;

    TestCaseRunner(args);
  }

  // Case 3: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty. AF_INET6.
  {
    struct TestArguments args {};
    args.domain              = AF_INET6;
    args.max_connections     = 1;
    args.max_requests        = 1;
    args.app_status          = EXIT_FAILURE;
    args.initial_connections = 1;
    args.overload_after      = 5; // No overload. Overload should not be seen.
    args.interface_path_ptr  = path;
    args.expected_status     = std::vector<std::uint8_t> {1, 0};
    args.test_case           = 3;

    TestCaseRunner(args);
  }

  // Case 4: max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty.
  {
    struct TestArguments args {};
    args.domain              = AF_INET;
    args.max_connections     = 5;
    args.max_requests        = 10;
    args.app_status          = EXIT_FAILURE;
    args.initial_connections = 5;
    args.overload_after      = 10; // No overload. Overload should not be seen.
    args.interface_path_ptr  = path;
    args.expected_status     = std::vector<std::uint8_t> {1,1,1,1,1,0};
    args.test_case           = 4;

    TestCaseRunner(args);
  }
  
  // Case 5: max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty, a previous
  // connection was made, and the interface was placed in an overloaded state.
  {
    struct TestArguments args {};
    args.domain              = AF_INET;
    args.max_connections     = 5;
    args.max_requests        = 10;
    args.app_status          = EXIT_FAILURE;
    args.initial_connections = 1;
    args.overload_after      = 1; // Overload for connection 2. Overload seen.
    args.interface_path_ptr  = path;
    args.expected_status     = std::vector<std::uint8_t> {1,0};
    args.test_case           = 5;

    TestCaseRunner(args);
  }

  // 6) FCGI_WEB_SERVER_ADDRS contains the IPv4 loopback address 127.0.0.1. 
  //    A client with address 127.0.0.1 attempts to make a connection and it
  //    succeeds. A client with address 127.0.0.2 attempts to make a connection
  //    and it fails.
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "127.0.0.1", 1) != -1)
    {
      struct TestArguments args {};
      args.domain              = AF_INET;
      args.max_connections     = 5;
      args.max_requests        = 10;
      args.app_status          = EXIT_FAILURE;
      args.initial_connections = 1;
      args.overload_after      = 5; // No overload. Overload should not be seen.
      args.interface_path_ptr  = path;
      args.expected_status     = std::vector<std::uint8_t> {1,0};
      args.test_case           = 6;

      TestCaseRunner(args);

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
      struct TestArguments args {};
      args.domain              = AF_INET6;
      args.max_connections     = 100;
      args.max_requests        = 1000;
      args.app_status          = EXIT_FAILURE;
      args.initial_connections = 0;
      args.overload_after      = 10; // No overload. Overload should be unseen.
      args.interface_path_ptr  = path;
      args.expected_status     = std::vector<std::uint8_t> {0};
      args.test_case           = 7;

      TestCaseRunner(args);

      if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) == -1)
        ADD_FAILURE() << "The environment could not be restored by a call to "
          "setenv in case 7." << '\n' << std::strerror(errno);
    }
    else
      ADD_FAILURE() << "The environment could not be modified with by a call "
        "to setenv in case 7." << '\n' << std::strerror(errno);
  }
  
  // Restore the default SIGPIPE disposition.
  sigpipe_disp.sa_handler = SIG_DFL;
  if(sigaction(SIGPIPE, &sigpipe_disp, nullptr) == -1)
  {
    FAIL() << "A call to sigaction to restore the defailt SIGPIPE  behavior "
      "failed." << '\n' << std::strerror(errno);
  }
}
