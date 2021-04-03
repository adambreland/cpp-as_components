// In addition to providing the logic for the integration test application
// server, this program must act as an application server creation command
// which exits with either success or failure to an invoking shell depending on
// whether the application server was created successfully. The return of this
// program synchronizes application server creation before initialization of
// an NGINX instance which depends on the application server.

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "fcgi/include/fcgi_protocol_constants.h"
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
  // Fail-safe process termination.
  alarm(2U);
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
    // Writes the process ID of the application server to a file so that the
    // HTTP client test process can terminate it upon test exit.
    std::ios_base::openmode open_mode {std::ios_base::out |
      std::ios_base::trunc};
    std::fstream pid_out {"/tmp/fcgi_server_interface.pid", open_mode};
    const char* pid_write_failure_message {"The interface process ID could "
      "not be written.\n"};
    if(!(pid_out.good()))
    {
      std::cerr << pid_write_failure_message;
      exit(EXIT_FAILURE);
    }
    pid_out << getpid() << '\n';
    if(!(pid_out.good()))
    {
      std::cerr << pid_write_failure_message;
      exit(EXIT_FAILURE);
    }
    pid_out.close();
    // Informs the waiting parent that construction was successful. Process
    // termination due to the receipt of SIGPIPE is acceptable in this case.
    char char_buffer {1};
    if(write(pipe_array[1], &char_buffer, 1U) < 1)
    {
      FailWithErrnoMessage("write");
    }
    close(pipe_array[1]);

    // Beginning of the application server logic.
    const char* header_terminator {"\r\n"};
    const char* response_prefix {"Status: 200 Success\r\n"};
    const char* test_header_name_prefix {"TEST_HEADER_"};
    const std::vector<std::uint8_t> test_header_search_key {
      static_cast<const std::uint8_t*>(static_cast<const void*>(
          test_header_name_prefix)),
      static_cast<const std::uint8_t*>(static_cast<const void*>(
          test_header_name_prefix + 12)) // Yes, this was manually counted.
    };
    std::vector<std::uint8_t>::const_iterator test_header_search_key_end
      {test_header_search_key.end()};
    const std::string response_body {"FcgiServerInterface!\n"};
    const std::string response_body_length_text
      {std::to_string(response_body.size())};
    using FcgiRequest = as_components::fcgi::FcgiRequest;
    std::vector<FcgiRequest> requests {};
    while(true)
    {
      requests = fcgi_inter.AcceptRequests();
      for(std::vector<FcgiRequest>::iterator req_iter {requests.begin()};
        req_iter != requests.end(); ++req_iter)
      {
        std::string response {response_prefix};
        // Validate the metadata of the request against the expected values.
        int metadata_correct {1};
        if((req_iter->get_environment_map().size() == 0U) ||
           (req_iter->get_DATA().size() != 0U) ||
           (req_iter->get_keep_conn() != true) ||
           (req_iter->get_role() != as_components::fcgi::FCGI_RESPONDER) ||
           (req_iter->AbortStatus() != false))
        {
          metadata_correct = 0;
        }
        response.append("Metadata-Correct: ").append(
          std::to_string(metadata_correct)).append(header_terminator);
        const std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>&
        req_env {req_iter->get_environment_map()};
        std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>::const_iterator
        env_iter {req_env.lower_bound(test_header_search_key)};
        // Iterates over test headers as determined by test header prefix
        // inclusion.
        for(/*no-op*/;
            (env_iter != req_env.end()) &&
            (std::mismatch(test_header_search_key.begin(),
                           test_header_search_key_end,
                           env_iter->first.begin()).first ==
            test_header_search_key_end);
            ++env_iter)
        {
          response.append(env_iter->first.begin(), env_iter->first.end());
          response.append(": ");
          response.append(env_iter->second.begin(), env_iter->second.end());
          response.append(header_terminator);
        }
        response.append("Content-Type: text/plain").append(header_terminator);
        response.append("Content-Length: ").append(response_body_length_text).
          append(header_terminator);
        // Empty line which indicates the start of the body.
        response.append(header_terminator);
        response.append(response_body.begin(), response_body.end());
        req_iter->Write(response.begin(), response.end());
        req_iter->Complete(EXIT_SUCCESS);
      }
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
