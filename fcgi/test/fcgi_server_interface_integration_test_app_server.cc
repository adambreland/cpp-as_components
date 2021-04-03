

// In addition to providing the logic for the integration test application
// server, this program must act as an application server creation command
// which exits with either success or failure to an invoking shell depending on
// whether the application server was created successfully. The return of this
// program synchronizes application server creation before initialization of
// an NGINX instance which depends on the application server.

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

#include "fcgi/include/fcgi_request.h"
#include "fcgi/include/fcgi_request_identifier.h"
#include "fcgi/include/fcgi_server_interface.h"

int main(int, char**)
{
  auto FailWithErrnoMessage = []
  (const char* failed_call_name)->void [[noreturn]]
  {
    std::cerr << failed_call_name << ": " << std::strerror(errno) << '\n';
    exit(EXIT_FAILURE);
  };

  int pipe_array[2] = {};
  if(pipe(pipe_array) == -1)
  {
    FailWithErrnoMessage("pipe");
  }
  pid_t fork_return {fork()};
  if(fork_return == -1)
  {
    FailWithErrnoMessage("fork");
  }
  else if(fork_return != 0)
  {
    // In parent.
    close(pipe_array[1]);
    char char_buffer {};
    int read_return {};
    while((read_return = read(pipe_array[0], &char_buffer, 1U)) < 1)
    {
      if(read_return == -1)
      {
        if(errno == EINTR)
        {
          continue;
        }
        FailWithErrnoMessage("read");
      }
      else if(read_return == 0)
      {
        std::cerr << "The application server unexpectedly closed the pipe.\n";
        return EXIT_FAILURE;
      }
    }
    return EXIT_SUCCESS;
  }
  // Else, in child.
  close(pipe_array[0]);
  int fcgi_listen_sock {socket(AF_UNIX, SOCK_STREAM, 0)};
  if(fcgi_listen_sock == -1)
  {
    FailWithErrnoMessage("socket");
  }
  struct sockaddr_un unix_address {};
  unix_address.sun_family = AF_UNIX;
  std::strcpy(unix_address.sun_path,
    "/tmp/fcgi_server_interface_integration_test_listen_sock");
  if(bind(fcgi_listen_sock, static_cast<struct sockaddr*>(static_cast<void*>(
    &unix_address)), sizeof(struct sockaddr_un)) == -1)
  {
    FailWithErrnoMessage("bind");
  }
  if(listen(fcgi_listen_sock, 1024) == -1)
  {
    FailWithErrnoMessage("listen");
  }
  try
  {
    as_components::fcgi::FcgiServerInterface fcgi_inter {fcgi_listen_sock, 10,
      1};
    // Informs the waiting parent that construction was successful. Process
    // termination due to the receipt of SIGPIPE is acceptable in this case.
    char char_buffer {1};
    if(write(pipe_array[1], &char_buffer, 1U) < 1)
    {
      FailWithErrnoMessage("write");
    }
  }
  catch(std::exception& e)
  {
    std::cerr << "An exception was caught in the application server.\n" <<
      e.what() << '\n';
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}
