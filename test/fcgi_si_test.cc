#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>         // <cstdlib> does not define setenv. 
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <string>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "fcgi_si.h"
#include "test/fcgi_si_testing_utilities.h"

// Key:
// BAZEL DEPENDENCY       This marks use of a feature which is provided by the
//                        Bazel testing run-time environment. 

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
  //    b) Where FCGI_WEB_SERVER_ADDRS is bound to internet addresses.
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
      FAIL() << "setenv failed" << '\n' << strerror(errno);
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
        << strerror(errno);
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
          << strerror(errno);
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
        << strerror(errno);
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
          << strerror(errno);
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
    std::string case_suffix {" case "};
    case_suffix += std::to_string(test_case);
    case_suffix += ".";

    if(setenv("FCGI_WEB_SERVER_ADDRS", address_list_ptr, 1) < 0)
      ADD_FAILURE() << "setenv failed in" << case_suffix << '\n' 
        << strerror(errno);
    else
    {
      int socket_fd {socket(domain, SOCK_STREAM, 0)};
      if(socket_fd < 0)
      {
        ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
          << strerror(errno);
        ClearFCGIWebServerAddrs();
      }
      else
      {
        if(listen(socket_fd, 5) < 0)
        {
          ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n'
            << strerror(errno);
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
    std::string case_suffix {" case "};
    case_suffix += std::to_string(test_case);
    case_suffix += ".";

    int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
        << strerror(errno);
    }
    else
    {
      if(listen(socket_fd, 5) < 0)
      { 
        ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n' 
          << strerror(errno);
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
        << strerror(errno);
    }
    else
    {
      if(listen(socket_fd, 5) < 0)
      { 
        ADD_FAILURE() << "A call to listen failed in case 12." << '\n' 
          << strerror(errno);
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
    std::string case_suffix {" case "};
    case_suffix += std::to_string(test_case);
    case_suffix += ".";

    int socket_fd {socket(domain, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in" << case_suffix << '\n' 
        << strerror(errno);
    }
    else
    {
      // An unnamed UNIX socket is used. A call to bind is not necessary.
      if(listen(socket_fd, 5) < 0)
      { 
        ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n' 
          << strerror(errno);
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
    std::string case_suffix {" case "};
    case_suffix += std::to_string(test_case);
    case_suffix += ".";

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
        << strerror(errno);
    }
    else
    {
      if(bind(socket_fd, 
        static_cast<struct sockaddr*>(static_cast<void*>(&unix_addr)),
        sizeof(struct sockaddr_un)) < 0)
      {
        ADD_FAILURE() << "A call to bind failed in" << case_suffix << '\n'
          << strerror(errno);
        close(socket_fd);
      }
      else
      {
        if(listen(socket_fd, 5) < 0)
        { 
          ADD_FAILURE() << "A call to listen failed in" << case_suffix << '\n' 
            << strerror(errno);
          close(socket_fd);
          if(unlink(unix_socket_path.data()) < 0)
            FAIL() << "The test Unix domain socket file could not be unlinked."
              << '\n' << strerror(errno); 
        }
        else
        {
          InterfaceGettersAndSetters(socket_fd, 1, 1, EXIT_FAILURE,
            case_suffix);
          close(socket_fd);
          if(unlink(unix_socket_path.data()) < 0)
            FAIL() << "The test Unix domain socket file could not be unlinked."
              << '\n' << strerror(errno);
        }
      }
    }
  };

  // Case 16: A Unix-domain socket is used. FCGI_WEB_SERVER_ADDRS is unbound.
  {
    if(unsetenv("FCGI_WEB_SERVER_ADDRS") < 0)
      ADD_FAILURE() << "FCGI_WEB_SERVER_ADDRS could not be removed from the "
        "environment in case 16." << '\n' << strerror(errno);
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
        << strerror(errno);
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
  // 2) Position of unknown names in the FastCGi name-value pair byte sequence.
  //    a) In the beginning.
  //    b) In the middle with a known name after an unknown name.
  // 3) 
  // 4) Subsets of the known names.
  // 5)  
  //
  // Test cases:
  //
  // 
  // Modules which testing depends on:
  // 1) 
  // 2)
  //
  // Other modules whose testing depends on this module: none.


}

// A signal handler and associated variable for use in 
// TEST(FCGIServerInterface, ConnectionAcceptanceAndRejection).
std::atomic<bool> alarm_flag {false};

void AlarmInterruptHandler(int sig)
{
  alarm_flag = true;
}

TEST(FCGIServerInterface, ConnectionAcceptanceAndRejection)
{
  // Testing explanation
  // Examined properties:
  // 1) Rejection of connections in excess of the limit set by the
  //    max_connections constructor argument.
  // 2) Rejection of connections when the interface was put into an overloaded
  //    state.
  // 3) Rejection of connections based on the presence of the address of the
  //    client in the list of addresses given by FCGI_WEB_SERVER_ADDRS.
  //
  // Test cases:
  // 1) max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty.
  // 2) max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty.
  // 3) max_connections == 5, FCGI_WEB_SERVER_ADDRS is empty, a previous
  //    connection was made, and the interface was placed in an overloaded
  //    state.
  // 4) FCGI_WEB_SERVER_ADDRS contains the loopback addresses 127.0.0.1 and
  //    127.0.0.2. A client with address 127.0.0.2 attempts to make a
  //    connection and it succeeds. A client with address 127.0.0.3 attempts
  //    to make a connection and it fails.

  // Ensure that operations on std::atomic<bool> are lock-free and can be
  // used in a signal handler.
  if(!std::atomic<bool>{}.is_lock_free())
    FAIL() << "Operations on std::atomic<bool> are not lock free.";

  // Save the previous signal handler information for SIGALRM so that it can
  // be restored.
  struct sigaction previous_sigalrm_disposition {};
  sigset_t empty_signal_set {};
  if(sigemptyset(&empty_signal_set) == -1)
    FAIL() << "A call to sigemptyset failed." << '\n' << strerror(errno);
  struct sigaction new_sigalrm_disposition {};
  new_sigalrm_disposition.sa_handler = &AlarmInterruptHandler;
  new_sigalrm_disposition.sa_mask = empty_signal_set;
  new_sigalrm_disposition.sa_flags = 0;
  if(sigaction(SIGALRM, &new_sigalrm_disposition, 
    &previous_sigalrm_disposition) == -1)
    FAIL() << "A call to sigaction failed" << '\n' << strerror(errno);

  // Case 1: max_connections == 1, FCGI_WEB_SERVER_ADDRS is empty.
  while(true)
  {
    if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
    {
      ADD_FAILURE() << "FCGI_WEB_SERVER_ADDRS could not be cleared in case 1."
        << '\n' << strerror(errno);
      break;
    }
    // Create a listening socket and extract the ephemeral port.
    int socket_fd {socket(AF_INET, SOCK_STREAM, 0)};
    if(socket_fd < 0)
    {
      ADD_FAILURE() << "A call to socket failed in case 1" << '\n' 
        << strerror(errno);
      break;
    }
    struct sockaddr_in socket_addr {};
    socklen_t socklen {sizeof(sockaddr_in)};
    if(getsockname(socket_fd, 
        static_cast<struct sockaddr*>(static_cast<void*>(&socket_addr)), 
        &socklen) < 0)
    {
      ADD_FAILURE() << "A call to getsockname failed in case 1." << '\n'
        << strerror(errno);
      break;
    }
    // Construct the interface, create a new socket which connects to it,
    // and have another socket try to connect and verify rejection.
    // EXPECT_NO_THROW(
    // {
    //   fcgi_si::FCGIServerInterface interface(socket_fd, 1, 1);
    //   int client_socket(socket());
    // });
    break;
  }

  // Restore the previous signal disposition for SIGALRM.
  if(sigaction(SIGALRM, &previous_sigalrm_disposition, nullptr) == -1)
    exit(EXIT_FAILURE);
}
