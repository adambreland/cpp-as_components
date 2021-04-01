#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

extern "C" {
  #include <curl/curl.h>
}

#include "googletest/include/gtest/gtest.h"
#include "socket_functions/include/socket_functions.h"

#include "fcgi/test/include/curl_easy_handle_classes.h"
#include "fcgi/test/test/include/client_interface_testing_utilities.h"

namespace as_components {
namespace fcgi {
namespace test {
namespace test {

// Test notes:
// Most of the methods of the classes of curl_easy_handle_classes.h do not
// require explicit testing. Review is sufficient for these methods. The
// methods and modules which require testing are:
// 1) CurlSlist::AppendString
// 2) HeaderProcessor and BodyProcessor
// 2) CurlHttpResponse::Register
// 3) CurlHttpResponse::Deregister

// Ensures that the CURL environment is initialized before the program starts.
CurlEnvironmentManager curl_environment {};

// Debug test
// A test which makes an HTTP request and prints the response to the test log.
// TEST(CurlEasyHandleClasses, DebugTest)
// {
//   try
//   {
//     as_components::fcgi::test::CurlEasyHandle easy_handle {};
//     ASSERT_EQ(curl_easy_setopt(easy_handle.get(), CURLOPT_URL,
//       "http://localhost/echo/response.txt"), CURLE_OK) <<
//         "CURLOPT_URL could not be set.";
//     as_components::fcgi::test::CurlSlist s_list {};
//     s_list.AppendString("Echo-1: first_echo");
//     s_list.AppendString("Echo-2: second_echo");
//     // It is assumed that this call cannot fail since easy_handle was set with
//     // a URL with http as the scheme.
//     curl_easy_setopt(easy_handle.get(), CURLOPT_HTTPHEADER, s_list.get());
//     as_components::fcgi::test::CurlHttpResponse response {};
//     // Implicitly sets CURLOPT_HEADERFUNCTION, CURLOPT_HEADERDATA,
//     // CURLOPT_WRITEFUNCTION, and CURLOPT_WRITEDATA.
//     response.Register(easy_handle.get());
//     CURLcode perform_result {};
//     ASSERT_EQ((perform_result = curl_easy_perform(easy_handle.get())),
//       CURLE_OK) << "curl_easy_perform failed. Curl error number: " <<
//         perform_result << '\n' << curl_easy_strerror(perform_result);
//     std::cout << "Status line: " <<
//       std::string_view
//       {
//         reinterpret_cast<char*>(response.status_line().version.data()),
//         response.status_line().version.size()
//       } << ' ' <<
//       std::string_view
//       {
//         reinterpret_cast<char*>(response.status_line().status_code.data()),
//         response.status_line().status_code.size()
//       } << ' ' <<
//       std::string_view
//       {
//         reinterpret_cast<char*>(response.status_line().status_text.data()),
//         response.status_line().status_text.size()
//       } << '\n';
//     std::cout << "\nHeaders:\n";
//     for(as_components::fcgi::test::HeaderList::iterator h_iter
//       {response.header_list().begin()}; h_iter != response.header_list().end();
//       ++h_iter)
//     {
//       std::cout <<
//         std::string_view {
//           reinterpret_cast<char*>(h_iter->first.data()),
//           h_iter->first.size()
//         } << ": " <<
//         std::string_view {
//           reinterpret_cast<char*>(h_iter->second.data()),
//           h_iter->second.size()
//         } << '\n';
//     }
//     std::string_view body_view
//       {reinterpret_cast<const char*>(response.body().data()),
//         response.body().size()};
//     std::cout << "\nBody:\n" << body_view << '\n';
//   }
//   catch(std::exception& e)
//   {
//     FAIL() << "An exception derived from std::exception was caught. " <<
//       e.what();
//   }
//   catch(...)
//   {
//     FAIL() << "An unknown exception type was caught.\n";
//   }
// }

// CurlSlist
// Testcases:
// 1) Both of the overloads of AppendString are used.

// CurlHttpResponse
// Test cases:
// 1) A std::logic_error exception is thrown when Register is called.
//    a) easy_handle_ptr == nullptr
//    b) The instance is already associated with an easy handle.
//    c) The instance has been used to receive the information for a request.
//    d) The instance was used and became associated with an error.
// 2) Deregister is called on an instance which was never registered.
// 3) A new instance is registered, deregistered, and then registered again.
//    The instance is then successfully used to receive response information.
// 4) An instance is registered. Another instance takes the easy handle of the
//    first instance through registration. The first instance is then
//    registered with another easy handle (which should be possible as the
//    first instance should have been deregistered).

// HeaderProcessor and BodyProcessor
// Examines properties:
// 1) Failure cases:
//    a) No instance is registered for an easy handle.
//       1) An instance was registered, and then deregistered.
//       2) An instance was registered, used to completion, and then an attempt
//          was made to use the instance again. This was detected, and the
//          instance was deregistered. A request is then made.
//    b) An instance is used after it has already been used. This case can be
//       combined with a.3.
//    c) For each of a header and status line: mismatch against the expected
//       pattern.
// 2) Non-failure cases:
//    Note: In all cases, the presence and correct content of two test headers
//    should be checked.
//    a) A response is registered and used.
//    b) A second response is registered on an easy handle which had
//       successfully been used. A request-response cycle is performed.
//    c) A second easy handle is used with a new response after at least one
//       response had been received over another easy handle.
//    d) A response is received over an easy handle which had previously been
//       used to receive a response and for which another easy handle was used
//       to receive a response between these responses.

const std::vector<std::uint8_t> kResponseBody
  {'a', 'b', 'c', '\n', '1', '2', '3'};

TEST(CurlEasyHandleClasses, CombinedSet1)
{
  try
  {
    auto GTestNonFatalCheckSuccessfulResponseObservers = []
    (CurlHttpResponse* response_ptr, int invocation_line)
    {
      ::testing::ScopedTrace tracer {__FILE__, invocation_line,
        "lambda CheckSuccessfulResponseObservers"};
      EXPECT_TRUE(response_ptr->status_line_received());
      EXPECT_TRUE(response_ptr->header_list_complete());
      EXPECT_EQ(response_ptr->body(), kResponseBody);
      EXPECT_FALSE(response_ptr->processing_error());
      EXPECT_FALSE(response_ptr->match_error());
      EXPECT_EQ(response_ptr->error_line().size(), 0U);
    };

    auto HeaderNameLessThan = []
    (const HeaderPair& lhs, const HeaderPair& rhs)->bool
    {
      return (lhs.first < rhs.first);
    };

    auto HeaderEquality = []
    (const HeaderPair& lhs, const HeaderPair& rhs)->bool
    {
      return (lhs.first == rhs.first);
    };

    auto GTestNonFatalCheckHeaders = [&HeaderNameLessThan, &HeaderEquality]
    (CurlHttpResponse* response_ptr, int invocation_line)->void
    {
      ::testing::ScopedTrace tracer {__FILE__, invocation_line,
        "lambda CheckHeaders"};
      // Use move iterators for efficient sorting below.
      HeaderList::iterator h_begin {response_ptr->header_list().begin()};
      HeaderList::iterator h_end {response_ptr->header_list().end()};
      EXPECT_GT(response_ptr->header_list().size(), 0U);
      std::sort(h_begin, h_end, HeaderNameLessThan);
      HeaderList::iterator duplicate_iter {std::adjacent_find(h_begin, h_end,
        HeaderEquality)};
      EXPECT_EQ(duplicate_iter, h_end);
      HeaderPair pairs[2] =
      {
        {{'E', 'c', 'h', 'o', '-', '1'}, {'1'}},
        {{'E', 'c', 'h', 'o', '-', '2'}, {'2'}}
      };
      for(int i {0}; i != 2; ++i)
      {
        HeaderPair& current_pair {pairs[i]};
        HeaderList::iterator echo_iter {std::lower_bound(h_begin, h_end,
          current_pair, HeaderNameLessThan)};
        EXPECT_NE(echo_iter, h_end);
        if(echo_iter == h_end)
        {
          return;
        }
        EXPECT_EQ(echo_iter->first, current_pair.first);
        EXPECT_EQ(echo_iter->second, current_pair.second);
      }
    };

    // TEST CASE 1 for CurlSlist.
    CurlSlist header_list {};
    header_list.AppendString("Echo-1: 1");
    std::string echo_2 {"Echo-2: 2"};
    header_list.AppendString(echo_2);
    CurlEasyHandle easy_handle_1 {};
    CurlHttpResponse curl_response_1 {};
    ASSERT_EQ(curl_easy_setopt(easy_handle_1.get(), CURLOPT_URL,
      "http://localhost/echo/response.txt"), CURLE_OK);
    ASSERT_EQ(curl_easy_setopt(easy_handle_1.get(), CURLOPT_HTTPHEADER,
      header_list.get()), CURLE_OK);
    // TEST CASE 1.a.1 for HeaderProcessor and BodyProcessor.
    ASSERT_NO_THROW(curl_response_1.Register(easy_handle_1.get()));
    curl_response_1.Deregister();
    ASSERT_NE(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    // TEST CASE 2.a for HeaderProcessor and BodyProcessor.
    ASSERT_NO_THROW(curl_response_1.Register(easy_handle_1.get()));
    ASSERT_EQ(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    GTestNonFatalCheckSuccessfulResponseObservers(&curl_response_1, __LINE__);
    GTestNonFatalCheckHeaders(&curl_response_1, __LINE__);
    // TEST CASE 2.b for HeaderProcessor and BodyProcessor.
    CurlHttpResponse curl_response_2 {};
    ASSERT_NO_THROW(curl_response_2.Register(easy_handle_1.get()));
    ASSERT_EQ(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    GTestNonFatalCheckSuccessfulResponseObservers(&curl_response_2, __LINE__);
    GTestNonFatalCheckHeaders(&curl_response_2, __LINE__);
    // TEST CASE 2.c for HeaderProcessor and BodyProcessor.
    CurlEasyHandle easy_handle_2 {};
    ASSERT_EQ(curl_easy_setopt(easy_handle_2.get(), CURLOPT_URL,
      "http://localhost/echo/response.txt"), CURLE_OK);
    ASSERT_EQ(curl_easy_setopt(easy_handle_2.get(), CURLOPT_HTTPHEADER,
      header_list.get()), CURLE_OK);
    CurlHttpResponse curl_response_3 {};
    ASSERT_NO_THROW(curl_response_3.Register(easy_handle_2.get()));
    ASSERT_EQ(curl_easy_perform(easy_handle_2.get()), CURLE_OK);
    GTestNonFatalCheckSuccessfulResponseObservers(&curl_response_3, __LINE__);
    GTestNonFatalCheckHeaders(&curl_response_3, __LINE__);
    // TEST CASE 2.d for HeaderProcessor and BodyProcessor.
    CurlHttpResponse curl_response_4 {};
    ASSERT_NO_THROW(curl_response_4.Register(easy_handle_1.get()));
    ASSERT_EQ(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    GTestNonFatalCheckSuccessfulResponseObservers(&curl_response_4, __LINE__);
    GTestNonFatalCheckHeaders(&curl_response_4, __LINE__);
  }
  catch(...)
  {
    FAIL();
  }
}

// CurlHttpHeader test cases 1.a, 1.b, 2, and 4.
TEST(CurlEasyHandleClasses, CurlHttpResponseOnlySet)
{
  // TEST CASE 1.a
  CurlHttpResponse curl_response_1 {};
  EXPECT_THROW(curl_response_1.Register(nullptr), std::logic_error);

  EXPECT_NO_THROW(
    CurlEasyHandle easy_handle_1 {};
    CurlEasyHandle easy_handle_2 {};
    CurlHttpResponse curl_response_2 {};
    CurlHttpResponse curl_response_3 {};
    // TEST CASE 2
    curl_response_2.Deregister();
    curl_response_3.Deregister();
    curl_response_2.Register(easy_handle_1.get());
    // curl_response_2 is already registered; these should throw.
    // TEST CASE 1.b
    ASSERT_THROW(curl_response_2.Register(easy_handle_1.get()),
      std::logic_error);
    ASSERT_THROW(curl_response_2.Register(easy_handle_2.get()),
      std::logic_error);
    // TEST CASE 4.
    // curl_response_3 is not registered. It can take easy_handle_1.
    ASSERT_NO_THROW(curl_response_3.Register(easy_handle_1.get()));
    // When easy_handle_1 was taken from curl_response_2, it should have become
    // deregistered. Re-registration should then be possible.
    EXPECT_NO_THROW(curl_response_2.Register(easy_handle_2.get()));
  );
}

// CurlHttpHeader test cases 1.c, 1.d, and 3; HeaderProcessor and BodyProcessor
// test cases 1.a.2, 1.b, and 1.c.
TEST(CurlEasyHandleClasses, CombinedSet2)
{
  try
  {
    std::cout.flush();
    int pipe_array[2] = {};
    ASSERT_NE(pipe(pipe_array), -1) << std::strerror(errno);
    // Forks a process which will act as a mock HTTP server.
    pid_t fork_return {fork()};
    ASSERT_NE(fork_return, -1) << std::strerror(errno);
    if(fork_return == 0)
    {
      // In child.
      alarm(2U); // Ensure that the child process terminates.
      close(pipe_array[0]);

      const std::string malformed_status_line_response
      {
        "HTTP/1.1 200Success\r\n" // Missing space between status code and text.
        "Content-Type: text/plain\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "0123456789\n"
      };
      const std::string malformed_header_response
      {
        "HTTP/1.1 200 Success\r\n"
        "Content-Type text/plain\r\n" // Missing colon separator.
        "Content-Length: 11\r\n"
        "\r\n"
        "0123456789\n"
      };

      auto WriteErrnoMessageAndQuickExit = []()->void [[noreturn]]
      {
        std::cerr << std::strerror(errno) << '\n';
        _exit(EXIT_FAILURE);
      };

      int listen_socket {socket(AF_INET, SOCK_STREAM, 0)};
      if(listen_socket == -1)
      {
        std::cerr << "The mock HTTP server could not create a socket.\n";
        WriteErrnoMessageAndQuickExit();
      }
      struct sockaddr_in server_address {};
      server_address.sin_family = AF_INET;
      server_address.sin_port = htons(80);
      int address_conversion {};
      if((address_conversion = inet_pton(AF_INET, "127.0.0.2",
        &server_address.sin_addr)) < 1)
      {
          FAIL() << ((address_conversion == 0) ?
            "inet_pton considered the address string to be invalid." :
            std::strerror(errno));
      }
      if((bind(listen_socket, static_cast<struct sockaddr*>(static_cast<void*>(
        &server_address)), sizeof(struct sockaddr_in)) == -1) ||
        (listen(listen_socket, 5) == -1))
      {
        WriteErrnoMessageAndQuickExit();
      }
      // Signals readiness to the parent with a write.
      char char_buffer {static_cast<char>(1)};
      if(write(pipe_array[1], &char_buffer, 1U) == -1)
      {
        WriteErrnoMessageAndQuickExit();
      }

      // Reads an incoming request, discards the read data, and pauses for a
      // short time before trying to read again. If no data was sent during the
      // pause, then it is assumed that the request is complete.
      auto ReadAndDiscard = [&WriteErrnoMessageAndQuickExit]
      (int client_connection)->void
      {
        constexpr const int buffer_size {128};
        std::uint8_t read_buffer[buffer_size];
        int max_for_select {client_connection + 1};
        fd_set read_set;
        while(true)
        {
          FD_ZERO(&read_set);
          FD_SET(client_connection, &read_set);
          if(select(max_for_select, &read_set, nullptr, nullptr, nullptr) == -1)
          {
            if(errno == EINTR)
            {
              continue;
            }
            else
            {
              WriteErrnoMessageAndQuickExit();
            }
          }
          break;
        }
        ssize_t read_return {};
        while(true)
        {
          while((read_return = read(client_connection, &read_buffer,
            buffer_size)) > 0)
          {
            continue;
          }
          if(read_return == 0)
          {
            WriteErrnoMessageAndQuickExit();
          }
          else if(errno == EINTR)
          {
            continue;
          }
          else if((errno == EAGAIN) || (errno == EWOULDBLOCK))
          {
            const struct timeval kWaitTime
            {
              /* tv_sec */ 0,
              /* tv_usec */ 100000 // 0.1 s == 100000 us
            };
            int select_return {};
            while(true)
            {
              struct timeval wait_time {kWaitTime};
              FD_ZERO(&read_set);
              FD_SET(client_connection, &read_set);
              select_return = select(max_for_select, &read_set, nullptr,
                nullptr, &wait_time);
              if(select_return == -1)
              {
                if(errno == EINTR)
                {
                  continue;
                }
                else
                {
                  WriteErrnoMessageAndQuickExit();
                }
              }
              else if(select_return == 0) // time-out
              {
                return;
              }
              break; // Loop to read more.
            }
          }
          else
          {
            WriteErrnoMessageAndQuickExit();
          }
        }
      };

      auto WriteString = [&WriteErrnoMessageAndQuickExit]
      (int client_connection, const std::string& response)->void
      {
        if(socket_functions::SocketWrite(client_connection,
          static_cast<const std::uint8_t*>(static_cast<const void*>(
            response.data())),
          response.size()) < response.size())
        {
          WriteErrnoMessageAndQuickExit();
        }
      };

      auto MakeNonblocking = [&WriteErrnoMessageAndQuickExit]
      (int client_connection)->void
      {
        // Makes the socket non-blocking.
        int fcntl_return  {fcntl(client_connection, F_GETFL)};
        if(fcntl_return == -1)
        {
          WriteErrnoMessageAndQuickExit();
        }
        fcntl_return |= O_NONBLOCK;
        if(fcntl(client_connection, F_SETFL, fcntl_return) == -1)
        {
          WriteErrnoMessageAndQuickExit();
        }
      };

      int client_connection {accept(listen_socket, nullptr, nullptr)};
      if(client_connection == -1)
      {
        WriteErrnoMessageAndQuickExit();
      }
      MakeNonblocking(client_connection);
      ReadAndDiscard(client_connection);
      WriteString(client_connection, malformed_status_line_response);
      close(client_connection);

      client_connection = accept(listen_socket, nullptr, nullptr);
      if(client_connection == -1)
      {
        WriteErrnoMessageAndQuickExit();
      }
      MakeNonblocking(client_connection);
      ReadAndDiscard(client_connection);
      WriteString(client_connection, malformed_header_response);
      close(client_connection);

      // Performs a blocking accept call to wait for termination.
      client_connection = accept(listen_socket, nullptr, nullptr);
    }
    // else, in parent.
    close(pipe_array[1]);
    // The parent waits for a ready write from the child.
    char char_buffer {};
    int read_return {};
    if((read_return = read(pipe_array[0], &char_buffer, 1U)) < 1)
    {
      ASSERT_NE(read_return, 0) <<
        "The child unexpectedly closed the connection.";
      ASSERT_GT(read_return, -1) << std::strerror(errno);
    }

    CurlEasyHandle easy_handle_1 {};
    // Sets a URL which is associated with a dummy HTTP server which will
    // serve a malformed response.
    if(curl_easy_setopt(easy_handle_1.get(), CURLOPT_URL,
      "http://127.0.0.2/") != CURLE_OK)
    {
      GTestFatalTerminateChild(fork_return, __LINE__);
      FAIL();
    }
    CurlHttpResponse curl_response_1 {};
    try
    {
      curl_response_1.Register(easy_handle_1.get());
    }
    catch(...)
    {
      GTestFatalTerminateChild(fork_return, __LINE__);
      FAIL();
    }
    // TEST CASE 1.d for CurlHttpResponse
    // TEST CASE 1.c for HeaderProcessor and BodyProcessor
    EXPECT_NE(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    EXPECT_TRUE(curl_response_1.processing_error());
    EXPECT_TRUE(curl_response_1.match_error());
    curl_response_1.Deregister();
    EXPECT_THROW(curl_response_1.Register(easy_handle_1.get()),
      std::logic_error);
    CurlHttpResponse curl_response_2 {};
    try
    {
      curl_response_2.Register(easy_handle_1.get());
    }
    catch(...)
    {
      GTestFatalTerminateChild(fork_return, __LINE__);
      FAIL();
    }
    // Makes a second request from the mock server.
    EXPECT_NE(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    EXPECT_TRUE(curl_response_2.processing_error());
    EXPECT_TRUE(curl_response_2.match_error());
    curl_response_2.Deregister();
    EXPECT_THROW(curl_response_2.Register(easy_handle_1.get()),
      std::logic_error);

    GTestFatalTerminateChild(fork_return, __LINE__);

    // TEST CASE 3 for CurlHttpResponse
    CurlEasyHandle easy_handle_2 {};
    CurlHttpResponse curl_response_3 {};
    ASSERT_NO_THROW(curl_response_3.Register(easy_handle_2.get()));
    curl_response_3.Deregister();
    ASSERT_NO_THROW(curl_response_3.Register(easy_handle_1.get()));
    ASSERT_EQ(curl_easy_setopt(easy_handle_1.get(), CURLOPT_URL,
      "http://127.0.0.1/data/response.txt"), CURLE_OK);
    ASSERT_EQ(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    EXPECT_TRUE(curl_response_3.status_line_received());
    EXPECT_TRUE(curl_response_3.header_list_complete());
    EXPECT_FALSE(curl_response_3.processing_error());
    EXPECT_FALSE(curl_response_3.match_error());
    EXPECT_EQ(curl_response_3.body(), kResponseBody);
    EXPECT_EQ(curl_response_3.error_line().size(), 0U);
    // TEST CASE 1.b for HeaderProcessor and BodyProcessor.
    ASSERT_NE(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
    // The above call to curl_easy_perform should have caused HeaderProcessor
    // to detect that the CurlHttpResponse instance had already been used and
    // was therefore invalid. It should have been deregistered.
    // TEST CASE 1.c for CurlHttpResponse.
    ASSERT_THROW(curl_response_3.Register(easy_handle_1.get()),
      std::logic_error);
    // TEST CASE 1.a.3 for HeaderProcessor and BodyProcessor.
    ASSERT_NE(curl_easy_perform(easy_handle_1.get()), CURLE_OK);
  }
  catch(...)
  {
    FAIL();
  }
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components
