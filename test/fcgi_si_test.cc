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

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "fcgi_si.h"
#include "test/fcgi_si_testing_utilities.h"
#include "external/socket_functions/include/socket_functions.h"

// Key:
// BAZEL DEPENDENCY       This marks use of a feature which is provided in the
//                        Bazel testing environment. 

// Utility functions and classes for interface tests.

namespace {

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

struct InterfaceCreationArguments
{
  int domain;
  int max_connections;
  int max_requests;
  int app_status;
  const char* unix_path;
};

void CheckAndReportDescriptorLeaks(
  fcgi_si_test::FileDescriptorLeakChecker* fdlc_ptr, 
  const std::string& test_name)
{
  std::pair<fcgi_si_test::FileDescriptorLeakChecker::const_iterator, 
    fcgi_si_test::FileDescriptorLeakChecker::const_iterator> iter_pair 
    {fdlc_ptr->Check()};
  if(iter_pair.first != iter_pair.second)
  {
    std::string message {"File descriptors were leaked in "};
    (message += test_name) += ": ";
    for(/*no-op*/; iter_pair.first != iter_pair.second; ++(iter_pair.first))
    {
      (message += std::to_string(*(iter_pair.first))) += " ";
    } 
    ADD_FAILURE() << message;
  }
}

// Creates a listening socket for an interface, and constructs an interface
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
//    fcgi_si::FCGIServerInterface. 
//    a) The interface socket file descriptor was closed.
//    b) The pointer to the interface is null.
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
//    d) For the internet domains, the listening socket is bound to the default
//       address and an ephemeral port.
// 2) If creation was not successful, the unique_ptr accessed by 
//    std::get<0> holds nullptr. If a socket was created, its descriptor
//    was closed. If a socket file was created, it was removed.
std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
CreateInterface(struct InterfaceCreationArguments args)
{
  std::unique_ptr<fcgi_si::FCGIServerInterface> interface_uptr {};
  if((args.domain == AF_UNIX) && !args.unix_path)
    return std::make_tuple(std::move(interface_uptr), -1, 0U);
  int socket_fd {socket(args.domain, SOCK_STREAM, 0)};
  bool unix_socket_bound {false};

  auto CleanupForFailure = [&interface_uptr, &unix_socket_bound, socket_fd, 
    &args]
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
      if(unlink(args.unix_path) == -1)
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
  if(args.domain == AF_UNIX)
  {
    struct sockaddr_un AF_UNIX_addr {};
    AF_UNIX_addr.sun_family = AF_UNIX;
    std::strcpy(AF_UNIX_addr.sun_path, args.unix_path);
    if(bind(socket_fd, 
      static_cast<struct sockaddr*>(static_cast<void*>(&AF_UNIX_addr)),
      sizeof(AF_UNIX_addr)) == -1)
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
  socklen_t AF_INET_socklen {sizeof(AF_INET_addr)};

  // AF_INET6
  struct sockaddr_in6 AF_INET6_addr {};
  socklen_t AF_INET6_socklen {sizeof(AF_INET6_addr)};

  if(args.domain != AF_UNIX)
  {
    if((args.domain == AF_INET) || (args.domain == AF_INET6))
    {
      if(args.domain == AF_INET)
      {
        address_ptr = static_cast<struct sockaddr*>(static_cast<void*>(
          &AF_INET_addr));
        length_ptr = &AF_INET_socklen;
      }
      else if(args.domain == AF_INET6)
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
      {new fcgi_si::FCGIServerInterface {socket_fd, args.max_connections, 
        args.max_requests, args.app_status}};
  }
  catch(...)
  {
    CleanupForFailure("interface construction", 0);
    throw;
  }
  
  return std::make_tuple(
    std::move(interface_uptr), 
    socket_fd, 
    (args.domain == AF_UNIX) ? 
      0U : 
      ((args.domain == AF_INET) ? 
        AF_INET_addr.sin_port :
        AF_INET6_addr.sin6_port)
  );
}

// This class creates an interface with the parameters provided in inter_args.
// client_number sockets are created and connected to the interface. These
// sockets are made non-blocking to facilitate testing FCGIServerInterface
// and related classes using a single process.
//
// The client socket descriptors, the interface, and interface information
// are made available through accessors.
//
// All socket descriptors which are associated with an instance are closed
// by the destructor. If inter_args.domain == AF_UNIX, the socket file is
// removed by the destructor.
class SingleProcessInterfaceAndClients
{
 public:
  inline fcgi_si::FCGIServerInterface& interface()
  {
    return *std::get<0>(inter_tuple_);
  }

  inline int interface_descriptor()
  {
    return std::get<1>(inter_tuple_);
  }

  inline struct sockaddr* interface_address_ptr()
  {
    return interface_addr_ptr_;
  }

  inline socklen_t interface_address_length()
  {
    return socket_addr_length_;
  }

  inline const std::vector<int>& client_descriptors()
  {
    return client_descriptors_;
  }

  // No copy.
  SingleProcessInterfaceAndClients() = default;
  SingleProcessInterfaceAndClients(SingleProcessInterfaceAndClients&&) 
    = default;
  SingleProcessInterfaceAndClients(const SingleProcessInterfaceAndClients&)
    = delete;
  
  SingleProcessInterfaceAndClients(
    struct InterfaceCreationArguments inter_args,  int client_number);

  SingleProcessInterfaceAndClients&
  operator=(SingleProcessInterfaceAndClients&&) = default;

  SingleProcessInterfaceAndClients&
  operator=(const SingleProcessInterfaceAndClients&) = delete;

  inline ~SingleProcessInterfaceAndClients()
  {
    CleanUp();
  }

 private:
  void CleanUp();

  struct InterfaceCreationArguments inter_args_;
  std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
  inter_tuple_;
  struct sockaddr* interface_addr_ptr_;
  socklen_t socket_addr_length_;
  std::vector<int> client_descriptors_;  
};

void SingleProcessInterfaceAndClients::
CleanUp()
{
  // Cleanup interface state.
  if(std::get<0>(inter_tuple_))
  {
    close(std::get<1>(inter_tuple_));
    if(inter_args_.domain == AF_UNIX)
    {
      if(unlink(inter_args_.unix_path) == -1)
        ADD_FAILURE() << "A call to unlink encountered an error when " 
          "destroying an instance of SingleProcessInterfaceAndClients."
          << std::strerror(errno); 
    }
  }
  // Cleanup client state.
  for(int descriptor : client_descriptors_)
  {
    if(descriptor >= 0)
      if(close(descriptor) == -1)
        ADD_FAILURE() << "A call to close on a client descriptor failed "
          "when destroying an instance of "
          "SingleProcessInterfaceAndClients." << '\n' 
          << std::strerror(errno);
  }
}

SingleProcessInterfaceAndClients::
SingleProcessInterfaceAndClients(
  struct InterfaceCreationArguments inter_args,  int client_number)
{
  inter_args_ = inter_args;

  // Verify interface listening socket parameters.
  if(!((inter_args_.domain == AF_UNIX)    || 
       (inter_args_.domain == AF_INET)    ||
       (inter_args_.domain == AF_INET6)))
  {
    ADD_FAILURE() << "Invalid domain argument in a call to "
      "SingleProcessInterfaceAndClients.";
    throw std::logic_error {""};
  }
  // 92 comes from the lowest known size of sun_path in struct sockaddr_un
  // across distributions. One is added to the length as a terminating
  // null byte must be copied as well.
  if((inter_args_.domain == AF_UNIX) && 
     ((std::strlen(inter_args.unix_path) + 1) > 92))
  {
    ADD_FAILURE() << "The interface path was too long.";
    throw std::logic_error {""};
  }

  try
  {
    inter_tuple_ = CreateInterface(inter_args_);
  }
  catch(const std::exception& e)
  {
    ADD_FAILURE() << "A call to CreateInterface threw an exception."
      << '\n' << e.what();
    throw e;
  }
  if(!std::get<0>(inter_tuple_))
  {
    ADD_FAILURE() << "An interface was not constructed by a call to "
      "CreateInterface when constructing an instance of "
      "SingleProcessInterfaceAndClients.";
    throw std::logic_error {""};
  }

  // Prepare the interface address so a client can connect.
  struct sockaddr_un AF_UNIX_interface_address {};
  if(inter_args_.domain == AF_UNIX)
  {
    AF_UNIX_interface_address.sun_family = AF_UNIX;
    std::strcpy(AF_UNIX_interface_address.sun_path, inter_args.unix_path);
    interface_addr_ptr_ = static_cast<struct sockaddr*>(static_cast<void*>(
      &AF_UNIX_interface_address));
    socket_addr_length_ = sizeof(AF_UNIX_interface_address);
  }

  struct sockaddr_in AF_INET_interface_address {};
  if(inter_args_.domain == AF_INET)
  {
    AF_INET_interface_address.sin_family      = AF_INET;
    AF_INET_interface_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    AF_INET_interface_address.sin_port        = std::get<2>(inter_tuple_);
    interface_addr_ptr_ = static_cast<struct sockaddr*>(static_cast<void*>(
      &AF_INET_interface_address));
    socket_addr_length_ = sizeof(AF_INET_interface_address);
  }

  struct sockaddr_in6 AF_INET6_interface_address {};
  if(inter_args_.domain == AF_INET6)
  {
    AF_INET6_interface_address.sin6_family = AF_INET6;
    AF_INET6_interface_address.sin6_addr   = in6addr_loopback;
    AF_INET6_interface_address.sin6_port   = std::get<2>(inter_tuple_);
    interface_addr_ptr_ = static_cast<struct sockaddr*>(static_cast<void*>(
      &AF_INET6_interface_address));
    socket_addr_length_ = sizeof(AF_INET6_interface_address);
  }

  try
  {
    client_descriptors_ = std::vector<int>(client_number, -1);
    for(int i {0}; i < client_number; ++i)
    {
      // Create a client socket and make it non-blocking.
      client_descriptors_[i] = socket(inter_args_.domain, SOCK_STREAM, 0);
      if(client_descriptors_[i] == -1)
      {
        ADD_FAILURE() << "A call to socket failed when constructing an instance "
          "of SingleProcessInterfaceAndClients." << '\n'
          << std::strerror(errno);
        throw std::exception {};
      }
      int f_getfl_return {fcntl(client_descriptors_[i], F_GETFL)};
      if(f_getfl_return == -1)
      {
        ADD_FAILURE() << "A call to fcntl with F_GETFL failed when "
          "constructing an instance of SingleProcessInterfaceAndClients." 
          << '\n' << std::strerror(errno);
        throw std::exception {};
      }
      f_getfl_return |= O_NONBLOCK;
      if(fcntl(client_descriptors_[i], F_SETFL, f_getfl_return) == -1)
      {
        ADD_FAILURE() << "A call to fcntl with F_SETFL failed when "
          "constructing an instance of SingleProcessInterfaceAndClients." 
          << '\n' << std::strerror(errno);
        throw std::exception {};
      }

      // Connect the socket to the interface.
      int connect_return {connect(client_descriptors_[i], 
        interface_addr_ptr_, socket_addr_length_)};
      if(connect_return == -1)
      {
        if(((inter_args_.domain == AF_UNIX) && (errno != EAGAIN)) || 
            ((inter_args_.domain != AF_UNIX) && (errno != EINPROGRESS)))
        {
          ADD_FAILURE() << "A call to connect failed with an unexpected "
            "error when constructing an instance of "
            "SingleProcessInterfaceAndClients." << '\n'
            << std::strerror(errno);
          throw std::exception {};
        }
      }

      // Allow the interface to process the connection.
      alarm(1U);
      std::get<0>(inter_tuple_)->AcceptRequests();
      alarm(0U);

      // Ensure connection readiness.
      if(connect_return == -1)
      {
        fd_set descriptor_set {};
        FD_ZERO(&descriptor_set);
        FD_SET(client_descriptors_[i], &descriptor_set);
        // Ensure that select does not block for long --- blocking indicates
        // an error.
        alarm(1U);
        int select_return {select(client_descriptors_[i] + 1, 
          nullptr, &descriptor_set, nullptr, nullptr)};
        alarm(0U);
        if(select_return == -1)
        {
          ADD_FAILURE() << "A call to select failed when constructing an "
            "instance of SingleProcessInterfaceAndClients." << '\n'
            << std::strerror(errno);
          throw std::exception();
        }
        int getsockopt_buffer {};
        socklen_t getsockopt_buffer_length {sizeof(getsockopt_buffer)};
        int getsockopt_return {getsockopt(client_descriptors_[i],
          SOL_SOCKET, SO_ERROR, &getsockopt_buffer, &getsockopt_buffer_length)};
        if((getsockopt_return == -1) || (getsockopt_buffer != 0))
        {
          ADD_FAILURE() << "A call to getsocket either failed or returned "
            " a failed connection status after a call to select when "
            "constructing an instance of "
            "SingleProcessInterfaceAndClients." 
            << '\n' << std::strerror(errno);
          throw std::exception();
        }
      }
    }
  }
  catch(const std::exception& e)
  {
    // Cleanup state before the exception leaves the constructor.
    ADD_FAILURE() << "An exception was thrown when constructing an instance "
      "of SingleProcessInterfaceAndClients." << '\n' << e.what();
    CleanUp();
    throw e;
  }
}

} // namespace

                          ////// Tests //////

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
  // Modules which testing depends on:
  // 1) SingleProcessInterfaceAndClients (defined locally)
  //
  // Other modules whose testing depends on this module: none.

  // Leak checker
  fcgi_si_test::FileDescriptorLeakChecker fdlc {};

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
        sizeof(sa)) < 0)
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
        sizeof(sa)) < 0)
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

  // Check for file descriptor leaks:
  CheckAndReportDescriptorLeaks(&fdlc, 
    "ConstructionExceptionsAndDirectlyObservableEffects");
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
  // 
  // Modules which testing depends on:
  // 1) fcgi_si::EncodeNameValuePairs
  // 2) fcgi_si::ExtractBinaryNameValuePairs
  // 3) fcgi_si::PopulateHeader
  // 4) socket_functions::ScatterGatherSocketWrite
  // 5) socket_functions::SocketRead
  // 6) SingleProcessInterfaceAndClients (defined locally)
  //
  // Other modules whose testing depends on this module: none.

  fcgi_si_test::FileDescriptorLeakChecker fdlc {};

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
    std::string case_suffix {CaseSuffix(test_case)};

    struct InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 1;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;
    
    SingleProcessInterfaceAndClients spiac {};
    try
    {
      spiac = SingleProcessInterfaceAndClients {inter_args, 1};
    }
    catch(const std::exception& e)
    {
      ADD_FAILURE() << "An exception was thrown when the normal "
        "SingleProcessInterfaceAndClients constructor was called in"
        << case_suffix << '\n' << e.what();
      return;
    }

    if(std::get<2>(socket_functions::ScatterGatherSocketWrite(
      spiac.client_descriptors()[0], a.iovec_ptr, a.iovec_count, 
      a.number_to_write)) != 0U)
    {
      ADD_FAILURE() << "An error occurred while writing to the interface in"
        << case_suffix << '\n' << std::strerror(errno);
      return;
    }

    // Allow the interface to process the FCGI_GET_VALUES request (record).
    alarm(1U);
    std::vector<fcgi_si::FCGIRequest> get_values_accept_return 
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
      std::size_t read_return {socket_functions::SocketRead(
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

    if((returned_result.size() < fcgi_si::FCGI_HEADER_LEN) || 
       (returned_result[1] != static_cast<std::uint8_t>(
         fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT)))
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
         (std::int_fast32_t(returned_result[fcgi_si::kHeaderContentLengthB1Index]) << 8) +
          std::int_fast32_t(returned_result[fcgi_si::kHeaderContentLengthB0Index])
      )
    };
    if((returned_result.size() - fcgi_si::FCGI_HEADER_LEN) < record_length)
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
      returned_pairs = fcgi_si::ExtractBinaryNameValuePairs(
        returned_result.data() + fcgi_si::FCGI_HEADER_LEN,
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

  CheckAndReportDescriptorLeaks(&fdlc, "FCGIGetValues");
}

TEST(FCGIServerInterface, UnknownManagementRequests)
{
  // Testing explanation
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
  // 1) socket_functions::SocketRead
  // 2) socket_functions::SocketWrite
  // 3) fcgi_si::PopulateHeader
  // 4) SingleProcessInterfaceAndClients (defined locally)
  // 
  // Other modules whose testing depends on this module: none.

  fcgi_si_test::FileDescriptorLeakChecker fdlc {};

  auto UnknownManagementRecordTester = [](
    struct InterfaceCreationArguments args, std::uint8_t* buffer_ptr,
    std::size_t count, fcgi_si::FCGIType type, int test_case)->void
  {
    std::string case_suffix {CaseSuffix(test_case)};
    try
    {
      SingleProcessInterfaceAndClients spiac {args, 1};
      if(socket_functions::SocketWrite(spiac.client_descriptors()[0], 
        buffer_ptr, count) < count)
      {
        ADD_FAILURE() << "Writing a request to the interface could not be "
          "performed in full in" << case_suffix << '\n' << std::strerror(errno);
        return;
      }
      // Allow the interface to process the request.
      spiac.interface().AcceptRequests();
      // Read the response.
      constexpr int_fast32_t response_length 
        {2 * fcgi_si::FCGI_HEADER_LEN};
      std::uint8_t read_buffer[response_length] = {};
      if(socket_functions::SocketRead(spiac.client_descriptors()[0],
        read_buffer, response_length) < (response_length))
      {
        ADD_FAILURE() << "Fewer than the expected number of bytes were read "
          "of the response by the interface to an unknown management request "
          "in" << case_suffix << '\n' << std::strerror(errno);
        return;
      }
     
      // Verify response information.
      EXPECT_EQ(1U, read_buffer[0]) << "The FastCGI protocol version was "
        "incorrect in the response in" << case_suffix;
      EXPECT_EQ(read_buffer[fcgi_si::kHeaderTypeIndex], 
        static_cast<std::uint8_t>(fcgi_si::FCGIType::kFCGI_UNKNOWN_TYPE)) <<
        "The type was not equal to FCGI_UNKNOWN_TYPE in" << case_suffix;
      EXPECT_EQ(0, (std::int_fast32_t(
        read_buffer[fcgi_si::kHeaderRequestIDB1Index]) << 8) + 
        std::int_fast32_t(read_buffer[fcgi_si::kHeaderRequestIDB0Index]))
        << "The request ID was not zero in the response in" << case_suffix;
      EXPECT_EQ(fcgi_si::FCGI_HEADER_LEN, (std::int_fast32_t(
        read_buffer[fcgi_si::kHeaderContentLengthB1Index]) << 8) + 
        std::int_fast32_t(read_buffer[fcgi_si::kHeaderContentLengthB0Index]))
        << "The response contained more content than specified in" 
        << case_suffix;
      EXPECT_EQ(0U, read_buffer[fcgi_si::kHeaderPaddingLengthIndex])
        << "Padding was present in the response in" << case_suffix;
      EXPECT_EQ(static_cast<std::uint8_t>(type),
        read_buffer[fcgi_si::kHeaderReservedByteIndex + 1]) << "The type "
        "sent by the client was not correctly returned by the interface in"
        << case_suffix;
      
      // Ensure that unexpected information was not received.
      std::size_t read_return {socket_functions::SocketRead(
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
    args.max_connections = 10;
    args.max_requests    = 10;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;
    
    std::uint8_t header[fcgi_si::FCGI_HEADER_LEN] = {};
    fcgi_si::PopulateHeader(header, fcgi_si::FCGIType::kFCGI_STDIN,
      0U, 0U, 0U);
    UnknownManagementRecordTester(args, header, fcgi_si::FCGI_HEADER_LEN, 
    fcgi_si::FCGIType::kFCGI_STDIN, 1);
  }

  // Case 2: The management request type has value 25. No content is present.
  {
    struct InterfaceCreationArguments args {};
    args.domain          = AF_INET;
    args.max_connections = 10;
    args.max_requests    = 10;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;

    std::uint8_t header[fcgi_si::FCGI_HEADER_LEN] = {};
    fcgi_si::PopulateHeader(header, static_cast<fcgi_si::FCGIType>(25),
      0U, 0U, 0U);
    UnknownManagementRecordTester(args, header, fcgi_si::FCGI_HEADER_LEN, 
    static_cast<fcgi_si::FCGIType>(25), 2);
  }

  // Case 3: The management request type has value 100. A body of bytes where
  // each byte has value 1 when interpreted as std::uint8_t is present. The
  // content is aligned on an 8-byte boundary.
  {
    struct InterfaceCreationArguments args {};
    args.domain          = AF_INET6;
    args.max_connections = 1000;
    args.max_requests    = 1000;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;

    std::uint8_t header[2 * fcgi_si::FCGI_HEADER_LEN] = {};
    fcgi_si::PopulateHeader(header, static_cast<fcgi_si::FCGIType>(100),
      0U, fcgi_si::FCGI_HEADER_LEN, 0U);
    std::memset(header + fcgi_si::FCGI_HEADER_LEN, 1, fcgi_si::FCGI_HEADER_LEN);
    UnknownManagementRecordTester(args, header, 2 * fcgi_si::FCGI_HEADER_LEN, 
    static_cast<fcgi_si::FCGIType>(100) , 3);
  }

  // Case 4: As in 3, but the content is not aligned on an 8-byte boundary and
  // padding is used.
  {
    struct InterfaceCreationArguments args {};
    args.domain          = AF_INET6;
    args.max_connections = 1;
    args.max_requests    = 1;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;

    std::uint8_t header[2 * fcgi_si::FCGI_HEADER_LEN] = {};
    fcgi_si::PopulateHeader(header, static_cast<fcgi_si::FCGIType>(100),
      0U, 3U, 5U);
    std::memset(header + fcgi_si::FCGI_HEADER_LEN, 1, 3);
    UnknownManagementRecordTester(args, header, 2 * fcgi_si::FCGI_HEADER_LEN, 
    static_cast<fcgi_si::FCGIType>(100) , 4);
  }

  // Case 5: As in 3, but content is not aligned on an 8-byte boundary and no
  // padding is used.
  {
    struct InterfaceCreationArguments args {};
    args.domain          = AF_INET6;
    args.max_connections = 1;
    args.max_requests    = 1;
    args.app_status      = EXIT_FAILURE;
    args.unix_path       = nullptr;

    std::uint8_t header[fcgi_si::FCGI_HEADER_LEN + 3] = {};
    fcgi_si::PopulateHeader(header, static_cast<fcgi_si::FCGIType>(100),
      0U, 3U, 0U);
    std::memset(header + fcgi_si::FCGI_HEADER_LEN, 1, 3);
    UnknownManagementRecordTester(args, header, fcgi_si::FCGI_HEADER_LEN + 3, 
    static_cast<fcgi_si::FCGIType>(100) , 5);
  }

  CheckAndReportDescriptorLeaks(&fdlc, "UnknownManagementRequests");
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
  std::tuple<std::unique_ptr<fcgi_si::FCGIServerInterface>, int, in_port_t>
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
    std::size_t socket_read {socket_functions::SocketRead(
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
      std::size_t signal_to {socket_functions::SocketWrite(
        socket_pair_array_[1], &null_byte, 1)};
      if(signal_to < 1)
        _exit(EXIT_FAILURE);
      std::size_t signal_from {socket_functions::SocketRead(
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
      socket_pair_array_[1], status_list.data(), status_list.size())};
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
    inter_tuple_ = CreateInterface(args_.inter_args);
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
  std::size_t port_write {socket_functions::SocketWrite(socket_pair_array_[0], 
    static_cast<std::uint8_t*>(static_cast<void*>(&std::get<2>(inter_tuple_))),
    sizeof(std::get<2>(inter_tuple_)))};
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
    std::size_t client_signal {socket_functions::SocketRead(
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
    std::vector<fcgi_si::FCGIRequest> result {};
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
      ADD_FAILURE() << "An FCGIRequest object was returned when none was "
        "expected in" << case_suffix_;
      return;
    } 
    if(connection_count == args_.overload_after)
      std::get<0>(inter_tuple_)->set_overload(true);
    // Signal that the interface processed the connection.
    std::size_t socket_write {socket_functions::SocketWrite(
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
  std::size_t status_report_read {socket_functions::SocketRead(
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

TEST(FCGIServerInterface, ConnectionAcceptanceAndRejection)
{
  // Testing explanation
  //    This test examines the behavior of a newly-created FCGIServerInterface
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
  //
  // Test side-effects relevant to other tests:
  // SIGPIPE will be ignored. The default disposition is restored at the end
  // of the test. Only non-fatal failures are used in the implementation
  // of TestCaseRunner to ensure that restoration takes place.

  fcgi_si_test::FileDescriptorLeakChecker fdlc {};

  SIGALRMHandlerInstaller();  

  if(setenv("FCGI_WEB_SERVER_ADDRS", "", 1) < 0)
  {
    FAIL() << "FCGI_WEB_SERVER_ADDRS could not be cleared."
      << '\n' << std::strerror(errno);
    return;
  }

  // Ignore SIGPIPE. The disposition will be inherited by the child produced
  // in the test.
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
  
  // Restore the default SIGPIPE disposition.
  sigpipe_disp.sa_handler = SIG_DFL;
  if(sigaction(SIGPIPE, &sigpipe_disp, nullptr) == -1)
  {
    FAIL() << "A call to sigaction to restore the defailt SIGPIPE  behavior "
      "failed." << '\n' << std::strerror(errno);
  }

  CheckAndReportDescriptorLeaks(&fdlc, "ConnectionAcceptanceAndRejection");
}

TEST(FCGIServerInterface, FCGIRequestGeneration)
{
  // Testing explanation
  //  
  // Test space discussion:
  // Notions related to sequences of received records:
  // 1) Partial record receipt: 
  //    a) FastCGI records must be transmitted as complete units. In the
  //       discussion of the concurrent transmission of FastCGI request data
  //       below, individual FastCGI records are the indivisible units of data
  //       whose transmission on a connection cannot be "interrupted" by the
  //       transmission of other data on that connection. 
  //    b) A record may not be received in-full before a read on the connection
  //       over which the record data was sent would block. Until the remaining
  //       data of the record is received, the record is in an incomplete or
  //       partially-received state.
  // 2) "Intermingling" of records can occur with respect to several record
  //    properties. For example, records on a single connection could be
  //    intermingled with respect to record type but not with respect to 
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
  //    sequence which is used to convey the data of the request? While the
  //    order of record receipt for a given record type is an invariant of 
  //    record transmission, differences in the order of the transmission of
  //    records of a different types and the potential for interleaving record
  //    types allows many possible variations in the record sequence which is
  //    used to transmit the data of a request.
  // 4) Stream record type data paritioning: The data associated with a stream
  //    record type can be arbitrarily partitioned between records provided
  //    that an empty record only occurs as the terminal record of the stream.
  // 5) Padding: Any record may have padding. Padding may be present regardless
  //    of whether the record is aligned on an 8-byte boundary or not.  
  // 6) The occurrence of partial record receipt when the connection of the
  //    record would block and another connection is ready. Will the request
  //    be correctly produced when the data for the request is eventually
  //    received in full?
  //
  // Test cases:
  // Single connection:
  // Note: Unless specified otherwise, the FCGI_KEEP_CONN flag is not set in
  // the FCGI_BEGIN_REQUEST record of a request. 
  // 1) Minimal requests: No data is present for any of the streams.
  //    a) Role: Responder; FCGI_PARAMS and FCGI_STDIN are terminated with
  //       empty records. No record of type FCGI_DATA is sent.
  //    b) As a, but the FCGI_KEEP_CONN flag is set.
  //    c) As a, but the role is Authorizer.
  //    d) As a, but a terminal FCGI_DATA record is sent before the request
  //       is completed given the special completion logic for the Responder
  //       role.
  //    e) As d, but the role is Authorizer.
  //    f) Role: Filter; All three data streams are terminated with empty
  //       records. The FCGI_DATA record is sent after the other records.
  //       A request object should not be generated until it is received
  //       according to the specified request completion logic for FCGI_DATA.
  //    g) Role: The role field has value 10; otherwise as e.
  // 2) Single request record type orderings: Records of different types are
  //    not interleaved. Rather, the record type order is varied across
  //    requests.
  //    a) Role: Responder. Data is present for FCGI_PARAMS and absent for
  //       FCGI_STDIN. No record with type FCGI_DATA is sent. The FCGI_PARAMS
  //       records are sent first.
  //    b) As a, but the completing, empty FCGI_STDIN record is sent first.
  //    c) Role: Responder. Data is present for both FCGI_PARAMS and FCGI_STDIN.
  //       No records of type FCGI_DATA are sent. The records for FCGI_PARAMS
  //       are sent before those for FCGI_STDIN.
  //    d) As c, but the order of FCGI_PARAMS and FCGI_STDIN is switched.
  //    e) Role: Filter. Data is present for all of the streams. Data is sent
  //       in the order: FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA.
  //    f) As e, but the order is: FCGI_DATA, FCGI_PARAMS, FCGI_STDIN.
  // 3) Single request record type interleavings:
  //    a) Role: Responder. Data is present for FCGI_PARAMS and FCGI_STDIN.
  //       No records of type FCGI_DATA are sent. The records of FCGI_PARAMS
  //       and FCGI_STDIN are interleaved before the streams are completed.
  //    b) 
  //
  // Modules which testing depends on:
  //
  // Other modules whose testing depends on this module:

  fcgi_si_test::FileDescriptorLeakChecker fdlc {};

  struct RequestData
  {
    std::uint16_t FCGI_id;
    std::uint16_t role;
    std::map<std::vector<uint8_t>, std::vector<uint8_t>> fcgi_params;
    std::vector<std::uint8_t> fcgi_stdin;
    std::vector<std::uint8_t> fcgi_data;
    bool fcgi_keep_conn;
  };

  auto RequestInspector = [](const fcgi_si::FCGIRequest& output, 
    const struct RequestData& input, const std::string& message)->void
  {
    EXPECT_EQ(output.get_request_identifier().FCGI_id(), 
      input.FCGI_id) << message;
    EXPECT_EQ(output.get_role(), input.role) << message;
    EXPECT_EQ(output.get_environment_map(), input.fcgi_params) << message;
    EXPECT_EQ(output.get_STDIN(), input.fcgi_stdin) << message;
    EXPECT_EQ(output.get_DATA(), input.fcgi_data) << message;
    EXPECT_EQ(output.get_keep_conn(), input.fcgi_keep_conn) << message;
  };

  auto PopulateRole = [](std::uint8_t* buffer_ptr, std::uint16_t role)->void
  {
    *buffer_ptr = (role >> 8);
    *(buffer_ptr + 1) = role; // Truncating assignment.
  };

  // Single connection Test Case Set 1: Minimal requests
  auto SimpleMinimalRequestTestCaseRunner = [&RequestInspector, &PopulateRole]
  (const struct RequestData& request_data, const std::string& case_message)
  {
    InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 100;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    SingleProcessInterfaceAndClients spiac {inter_args, 1};

    constexpr std::size_t request_length {4 * fcgi_si::FCGI_HEADER_LEN};
    std::uint8_t record_array[request_length] = {};
    // FCGI_BEGIN_REQUEST record
    fcgi_si::PopulateHeader(record_array,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, request_data.FCGI_id, 
      fcgi_si::FCGI_HEADER_LEN, 0U);
    PopulateRole(record_array + fcgi_si::FCGI_HEADER_LEN, request_data.role);
    if(request_data.fcgi_keep_conn)
      *(record_array + fcgi_si::FCGI_HEADER_LEN + 2) = 1U;
    // FCGI_PARAMS record
    fcgi_si::PopulateHeader(record_array + (2 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_PARAMS, request_data.FCGI_id, 0U, 0U);
    // FCGI_STDIN record
    fcgi_si::PopulateHeader(record_array + (3 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_STDIN, request_data.FCGI_id, 0U, 0U);
    
    if(socket_functions::SocketWrite(spiac.client_descriptors()[0],
      record_array, request_length) < request_length)
    {
      ADD_FAILURE() << "The request could not be written in full in "
        << case_message << '\n' << std::strerror(errno);
    }
    else
    {
      // Allow the interface to process the request.
      std::vector<fcgi_si::FCGIRequest> request_list
        {spiac.interface().AcceptRequests()};
      EXPECT_EQ(request_list.size(), 1U) << "An incorrect number of requests "
        "was generated in " << case_message;
      if(request_list.size() == 1U)
        RequestInspector(request_list[0], request_data, case_message);
    }
  };

  // a) Role: Responder; FCGI_PARAMS and FCGI_STDIN are terminated with
  //    empty records. No record of type FCGI_DATA is sent.
  {
    std::string case_message {"Test Case Set 1, test case a."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = fcgi_si::FCGI_RESPONDER;
    request_data.fcgi_keep_conn = false;

    SimpleMinimalRequestTestCaseRunner(request_data, case_message);
  }

  // b) As a, but the FCGI_KEEP_CONN flag is set.
  {
    std::string case_message {"Test Case Set 1, test case b."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = fcgi_si::FCGI_RESPONDER;
    request_data.fcgi_keep_conn = true;

    SimpleMinimalRequestTestCaseRunner(request_data, case_message);
  }

  // c) As a, but the role is Authorizer.
  {
    std::string case_message {"Test Case Set 1, test case c."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = fcgi_si::FCGI_AUTHORIZER;
    request_data.fcgi_keep_conn = false;

    SimpleMinimalRequestTestCaseRunner(request_data, case_message);
  }

  auto DataMinimalRequestTestCaseRunner = [&RequestInspector, &PopulateRole]
  (const struct RequestData& request_data, const std::string& case_message)
  {
    InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 100;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    SingleProcessInterfaceAndClients spiac {inter_args, 1};

    constexpr std::size_t request_length {5 * fcgi_si::FCGI_HEADER_LEN};
    std::uint8_t record_array[request_length] = {};
    // FCGI_BEGIN_REQUEST record
    fcgi_si::PopulateHeader(record_array,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, request_data.FCGI_id, 
      fcgi_si::FCGI_HEADER_LEN, 0U);
    PopulateRole(record_array + fcgi_si::FCGI_HEADER_LEN, request_data.role);
    if(request_data.fcgi_keep_conn)
      *(record_array + fcgi_si::FCGI_HEADER_LEN + 2) = 1U;
    // FCGI_PARAMS record
    fcgi_si::PopulateHeader(record_array + (2 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_PARAMS, request_data.FCGI_id, 0U, 0U);
    // FCGI_DATA record
    fcgi_si::PopulateHeader(record_array + (3 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_DATA, request_data.FCGI_id, 0U, 0U);
    // FCGI_STDIN record
    fcgi_si::PopulateHeader(record_array + (4 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_STDIN, request_data.FCGI_id, 0U, 0U);
    
    if(socket_functions::SocketWrite(spiac.client_descriptors()[0],
      record_array, (4 * fcgi_si::FCGI_HEADER_LEN)) < 
      (4 * fcgi_si::FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "The first part of the request could not be written in "
        "full in " << case_message << '\n' << std::strerror(errno);
    }
    else
    {
      // Allow the interface to process the first part of the request.
      std::vector<fcgi_si::FCGIRequest> request_list
        {spiac.interface().AcceptRequests()};
      EXPECT_EQ(request_list.size(), 0U) << "A request was generated when one "
        "was not expected in " << case_message;
      // Write the remaining record to complete the request.
      if(socket_functions::SocketWrite(spiac.client_descriptors()[0],
        record_array + (4 * fcgi_si::FCGI_HEADER_LEN), 
        fcgi_si::FCGI_HEADER_LEN) < fcgi_si::FCGI_HEADER_LEN)
      {
        ADD_FAILURE() << "The final part of the request was not written in "
          << "full in " << case_message << '\n' << std::strerror(errno);
      }
      else
      {
        request_list = spiac.interface().AcceptRequests();
        EXPECT_EQ(request_list.size(), 1U) << "An incorrect number of requests "
        "was generated in " << case_message;
        if(request_list.size() == 1U)
          RequestInspector(request_list[0], request_data, case_message);
      } 
    }
  };

  // d) As a, but a terminal FCGI_DATA record is sent before the request is
  //    completed given the special completion logic for the Responder role.
  {
    std::string case_message {"Test Case Set 1, test case d."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = fcgi_si::FCGI_RESPONDER;
    request_data.fcgi_keep_conn = false;

    DataMinimalRequestTestCaseRunner(request_data, case_message);
  }

  // e) As d, but the role is Authorizer.
  {
    std::string case_message {"Test Case Set 1, test case e."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = fcgi_si::FCGI_AUTHORIZER;
    request_data.fcgi_keep_conn = false;

    DataMinimalRequestTestCaseRunner(request_data, case_message);
  }

  auto FilterMinimalRequestTestCaseRunner = [&RequestInspector, &PopulateRole]
  (const struct RequestData& request_data, const std::string& case_message)
  {
    InterfaceCreationArguments inter_args {};
    inter_args.domain          = AF_INET;
    inter_args.max_connections = 1;
    inter_args.max_requests    = 100;
    inter_args.app_status      = EXIT_FAILURE;
    inter_args.unix_path       = nullptr;

    SingleProcessInterfaceAndClients spiac {inter_args, 1};

    constexpr std::size_t request_length {5 * fcgi_si::FCGI_HEADER_LEN};
    std::uint8_t record_array[request_length] = {};
    // FCGI_BEGIN_REQUEST record
    fcgi_si::PopulateHeader(record_array,
      fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST, request_data.FCGI_id, 
      fcgi_si::FCGI_HEADER_LEN, 0U);
    PopulateRole(record_array + fcgi_si::FCGI_HEADER_LEN, request_data.role);
    if(request_data.fcgi_keep_conn)
      *(record_array + fcgi_si::FCGI_HEADER_LEN + 2) = 1U;
    // FCGI_PARAMS record
    fcgi_si::PopulateHeader(record_array + (2 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_PARAMS, request_data.FCGI_id, 0U, 0U);
    // FCGI_DATA record
    fcgi_si::PopulateHeader(record_array + (3 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_STDIN, request_data.FCGI_id, 0U, 0U);
    // FCGI_STDIN record
    fcgi_si::PopulateHeader(record_array + (4 * fcgi_si::FCGI_HEADER_LEN),
      fcgi_si::FCGIType::kFCGI_DATA, request_data.FCGI_id, 0U, 0U);
    
    if(socket_functions::SocketWrite(spiac.client_descriptors()[0],
      record_array, (4 * fcgi_si::FCGI_HEADER_LEN)) < 
      (4 * fcgi_si::FCGI_HEADER_LEN))
    {
      ADD_FAILURE() << "The first part of the request could not be written in "
        "full in " << case_message << '\n' << std::strerror(errno);
    }
    else
    {
      // Allow the interface to process the first part of the request.
      std::vector<fcgi_si::FCGIRequest> request_list
        {spiac.interface().AcceptRequests()};
      EXPECT_EQ(request_list.size(), 0U) << "A request was generated when one "
        "was not expected in " << case_message;
      // Write the remaining record to complete the request.
      if(socket_functions::SocketWrite(spiac.client_descriptors()[0],
        record_array + (4 * fcgi_si::FCGI_HEADER_LEN), 
        fcgi_si::FCGI_HEADER_LEN) < fcgi_si::FCGI_HEADER_LEN)
      {
        ADD_FAILURE() << "The final part of the request was not written in "
          << "full in " << case_message << '\n' << std::strerror(errno);
      }
      else
      {
        request_list = spiac.interface().AcceptRequests();
        EXPECT_EQ(request_list.size(), 1U) << "An incorrect number of requests "
        "was generated in " << case_message;
        if(request_list.size() == 1U)
          RequestInspector(request_list[0], request_data, case_message);
      } 
    }
  };
    
  // f) Role: Filter; All three data streams are terminated with empty
  //    records. The FCGI_DATA record is sent after the other records.
  //    A request object should not be generated until it is received
  //    according to the specified request completion logic for FCGI_DATA.
  {
    std::string case_message {"Test Case Set 1, test case f."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = fcgi_si::FCGI_FILTER;
    request_data.fcgi_keep_conn = false;

    FilterMinimalRequestTestCaseRunner(request_data, case_message);
  }
  
  //  g) Role: The role field has value 10; otherwise as e.
  {
    std::string case_message {"Test Case Set 1, test case g."};

    struct RequestData request_data {};
    request_data.FCGI_id        = 1U;
    request_data.role           = 10U;
    request_data.fcgi_keep_conn = false;

    FilterMinimalRequestTestCaseRunner(request_data, case_message);
  }

  // Single Connection Test Case Set 2: Record type orderings without
  // record type record interleaving.
  // a) Role: Responder. Data is present for FCGI_PARAMS and absent for
  //    FCGI_STDIN. No record with type FCGI_DATA is sent. The FCGI_PARAMS
  //    records are sent first.
  // b) As a, but the completing, empty FCGI_STDIN record is sent first.
  // c) Role: Responder. Data is present for both FCGI_PARAMS and FCGI_STDIN.
  //    No records of type FCGI_DATA are sent. The records for FCGI_PARAMS
  //    are sent before those for FCGI_STDIN.
  // d) As c, but the order of FCGI_PARAMS and FCGI_STDIN is switched.
  // e) Role: Filter. Data is present for all of the streams. Data is sent
  //    in the order: FCGI_PARAMS, FCGI_STDIN, and FCGI_DATA.
  // f) As e, but the order is: FCGI_DATA, FCGI_PARAMS, FCGI_STDIN.

  auto TypeOrderingTestCaseRunner = [&RequestInspector, &PopulateRole]
  ()
  {
    
  };

  CheckAndReportDescriptorLeaks(&fdlc, "FCGIRequestGeneration");
}

TEST(FCGIServerInterface, RequestAcceptanceAndRejection)
{

}

TEST(FCGIServerInterface, ConnectionClosure)
{

}

TEST(FCGIServerInterface, FCGIRequestDataTransmission)
{

}

TEST(FCGIServerInterface, FCGIServerInterfaceDestruction)
{

}
