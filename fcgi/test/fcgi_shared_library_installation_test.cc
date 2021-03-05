#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "fcgi/include/fcgi_protocol_constants.h"
#include "fcgi/include/fcgi_request_identifier.h"
#include "fcgi/include/fcgi_server_interface.h"
#include "fcgi/include/fcgi_request.h"
#include "fcgi/test/include/test_fcgi_client_interface.h"
#include "socket_functions/include/socket_functions.h"

const std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
kCommonParams
{
  {
    {'F', 'C', 'G', 'I'}, {'1'}
  }
};

namespace {

struct Terminator
{
  bool Terminate();

  ~Terminator();

  pid_t child_id;
};

bool Terminator::Terminate()
{
  if(!child_id)
  {
    return false;
  }
  pid_t local_child_id {child_id};
  child_id = 0;
  int status {0};
  // Poll for termination of the process. It is assumed that previous
  // termination of the child was due to an error.
  int waitpid_return {waitpid(local_child_id, &status, WNOHANG)};
  if(waitpid_return != -1)
  {
    if(waitpid_return != 0)
    {
      std::cout << "The child changed state unexpectedly.\n";
    }
    // If the child hasn't changed state or has changed state but hasn't
    // terminated, then kill the child.
    if((waitpid_return == 0) || (!WIFEXITED(status) && !WIFSIGNALED(status)))
    {
      if(kill(local_child_id, SIGKILL) == -1)
      {
        std::cout << "An error occurred when kill was called to kill the "
          "child.\n" << std::strerror(errno) << '\n';
        return false;
      }
      // Reap the child once it terminates.
      while(((waitpid_return = waitpid(local_child_id, nullptr, 0)) == -1) &&
            (errno == EINTR))
      {
        continue;
      }
      if(waitpid_return == -1)
      {
        std::cout << "An error occurred when waitpid was used to reap the "
          "terminated child process.\n" << std::strerror(errno) << '\n';
        return false;
      }
    }
  }
  else
  {
    std::cout << "An error occurred when the parent tried to kill the child.\n";
    std::cout << strerror(errno) << '\n';
    return false;
  }
  return true;
}

Terminator::~Terminator()
{
  Terminate();
}

} // namespace

int main(int, char**)
{
  std::cout << "Executing test:\n" << __FILE__ << '\n';
  // Ignore SIGPIPE in the parent and child.
  struct sigaction ignore_signal_action
  {
    /* sa_handler */  SIG_IGN,
    /* sa_mask */     {},
    /* sa_flags */    {},
    /* sa_restorer */ {}
  };
  // Properly initialize the signal set sa_mask to an empty state.
  if(sigemptyset(&(ignore_signal_action.sa_mask)) == -1)
  {
    std::cout << std::strerror(errno) << '\n';
    return EXIT_FAILURE;
  }
  if(sigaction(SIGPIPE, &ignore_signal_action, nullptr) == -1)
  {
    std::cout << std::strerror(errno) << '\n';
    return EXIT_FAILURE;;
  }

  int pipe_return_array[2] = {};
  if(pipe(pipe_return_array) == -1)
  {
    std::cout << std::strerror(errno) << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "Forking a child process to use as a server.\n";
  pid_t fork_return {fork()};
  if(fork_return == -1)
  {
    std::cout << std::strerror(errno) << '\n';
    return EXIT_FAILURE;
  }
  else if(fork_return == 0)
  {
    // In child.
    // Set a self-kill timer.
    alarm(1);
    // Create the listening socket which will be used for the fcgi server.
    int listening_socket {socket(AF_INET, SOCK_STREAM, 0)};
    if(listening_socket == -1)
    {
      std::cout << std::strerror(errno) << '\n';
      return EXIT_FAILURE;
    }
    if(listen(listening_socket, 5) == -1)
    {
      std::cout << std::strerror(errno) << '\n';
      return EXIT_FAILURE;
    }
    socklen_t address_length_buffer {sizeof(struct sockaddr_in)};
    struct sockaddr_in in_address {};
    if(getsockname(listening_socket, reinterpret_cast<struct sockaddr*>(
      &in_address), &address_length_buffer) == -1)
    {
      std::cout << std::strerror(errno) << '\n';
      return EXIT_FAILURE;
    }
    as_components::fcgi::FcgiServerInterface server_inter
      {listening_socket, 1, 1};
    // Inform the parent that interface construction succeeded and of the
    // ephemeral port of the server.
    close(pipe_return_array[0]);
    if(as_components::socket_functions::SocketWrite(pipe_return_array[1],
      reinterpret_cast<std::uint8_t*>(&(in_address.sin_port)),
      sizeof(std::uint16_t)) < sizeof(std::uint16_t))
    {
      std::cout << std::strerror(errno) << '\n';
      return EXIT_FAILURE;
    }
    // Echo requests back to the parent.
    std::vector<as_components::fcgi::FcgiRequest> accept_buffer {};
    while(true)
    {
      accept_buffer = server_inter.AcceptRequests();
      for(std::vector<as_components::fcgi::FcgiRequest>::iterator
        req_iter {accept_buffer.begin()}; req_iter != accept_buffer.end();
        ++req_iter)
      {
        if(req_iter->get_environment_map() != kCommonParams)
        {
          std::cout << "A mismatch was found in the received FCGI_PARAMS "
            "map.\n";
          exit(EXIT_FAILURE);
        }
        if(req_iter->get_keep_conn())
        {
          std::cout << "keep_conn was set when it was not expected to be.\n";
          exit(EXIT_FAILURE);
        }
        if(req_iter->get_role() != as_components::fcgi::FCGI_RESPONDER)
        {
          std::cout << "A role other than FCGI_RESPONDER was received.\n";
          exit(EXIT_FAILURE);
        }
        const std::vector<std::uint8_t>& in_ref {req_iter->get_STDIN()};
        const std::vector<std::uint8_t>& data_ref {req_iter->get_DATA()};
        req_iter->Write(in_ref.begin(), in_ref.end());
        req_iter->WriteError(data_ref.begin(), data_ref.end());
        req_iter->Complete(EXIT_SUCCESS);
      }
    }
  }
  // In parent.
  // Ensure that the child is terminated when the process exits.
  std::unique_ptr<struct Terminator> child_terminator_uptr
    {new Terminator {fork_return}};
  close(pipe_return_array[1]);
  // Wait for the child to signal that the server has been constructed or that
  // an error occurred. The child signals by sending the ephemeral port of the
  // server in network byte order.
  std::cout << "Waiting for the server process to indicate that it is ready.\n";
  std::uint16_t port {};
  if(as_components::socket_functions::SocketRead(pipe_return_array[0],
    reinterpret_cast<std::uint8_t*>(&port), sizeof(std::uint16_t)) <
    sizeof(std::uint16_t))
  {
    std::cout << std::strerror(errno) << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "Indication received. Creating the client interface and "
    "connecting to the server.\n";
  as_components::fcgi::test::TestFcgiClientInterface client_inter {};
  int server_connection {client_inter.Connect("127.0.0.1", port)};
  if(server_connection == -1)
  {
    std::cout << "Port: " << static_cast<int>(port) << '\n';
    std::cout << std::strerror(errno) << '\n';
    return EXIT_FAILURE;
  }
  std::vector<std::uint8_t> FCGI_STDIN_data {0, 1, 2, 3};
  std::vector<std::uint8_t> FCGI_DATA_data {'A', 'B', 'C'};
  struct as_components::fcgi::test::FcgiRequestDataReference request_ref
  {
    /* role */           as_components::fcgi::FCGI_RESPONDER,
    /* keep_conn */      false,
    /* params_map_ptr */ &kCommonParams,
    /* stdin_begin */    FCGI_STDIN_data.data(),
    /* stdin_end */      FCGI_STDIN_data.data() + FCGI_STDIN_data.size(),
    /* data_begin */     FCGI_DATA_data.data(),
    /* data_end */       FCGI_DATA_data.data() + FCGI_DATA_data.size()
  };
  std::cout << "Sending a request.\n";
  as_components::fcgi::FcgiRequestIdentifier request_id
    {client_inter.SendRequest(server_connection, request_ref)};
  if(request_id == as_components::fcgi::FcgiRequestIdentifier {})
  {
    std::cout << "A request could not be sent to the server.\n";
    return EXIT_FAILURE;
  }
  std::cout << "Retrieving the response.\n";
  std::unique_ptr<as_components::fcgi::test::ServerEvent> event_uptr
    {client_inter.RetrieveServerEvent()};
  as_components::fcgi::test::FcgiResponse* response_ptr
    {dynamic_cast<as_components::fcgi::test::FcgiResponse*>(event_uptr.get())};
  if(!response_ptr)
  {
    std::cout << "A response ServerEvent pointer could not be converted to a "
      "non-null pointer to FcgiResponse.\n";
    return EXIT_FAILURE;
  }
  std::cout << "Comparing the received values to the expected values.\n";
  if((response_ptr->RequestId()      != request_id)                                 ||
     (response_ptr->Request()        != request_ref)                                ||
     (response_ptr->FcgiStdout()     != FCGI_STDIN_data)                            ||
     (response_ptr->FcgiStderr()     != FCGI_DATA_data)                             ||
     (response_ptr->ProtocolStatus() != as_components::fcgi::FCGI_REQUEST_COMPLETE) ||
     (response_ptr->AppStatus()      != EXIT_SUCCESS))
  {
    std::cout << "The response has unexpected values.\n";
    return EXIT_FAILURE;
  }
  std::cout << "Test passed. Terminating the server.\n";
  return (child_terminator_uptr->Terminate()) ?
    EXIT_SUCCESS : EXIT_FAILURE;
}
