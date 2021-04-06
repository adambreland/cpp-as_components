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

#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
  #include <curl/curl.h>
}

#include "googletest/include/gtest/gtest.h"

#include "fcgi/test/include/curl_easy_handle_classes.h"

namespace as_components {
namespace fcgi {
namespace test {

// Initializes the global CURL environment.
CurlEnvironmentManager curl_environment {};

// // Debug test
// // This test tries to connect to the application server. If it succeeds,
// // then it immediately closes the connection.
// TEST(FcgiServerInterfaceIntegrationTesting, UnixListenSockConnection)
// {
//   alarm(2U);
//   int unix_connection {socket(AF_UNIX, SOCK_STREAM, 0)};
//   ASSERT_NE(unix_connection, -1) << "socket " << std::strerror(errno) <<
//     '\n';
//   struct sockaddr_un unix_address {};
//   unix_address.sun_family = AF_UNIX;
//   std::strcpy(unix_address.sun_path,
//     "/tmp/fcgi_server_interface_integration_test_listen_sock");
//   ASSERT_NE(connect(unix_connection, static_cast<struct sockaddr*>(
//     static_cast<void*>(&unix_address)), sizeof(struct sockaddr_un)), -1) <<
//       "connect " << std::strerror(errno) << '\n';
//   close(unix_connection);
// }

// // Debug test
// // A test which makes an HTTP request and prints the response to the test
// // log.
// TEST(FcgiServerInterfaceIntegrationTesting, PrintResponse)
// {
//   try
//   {
//     as_components::fcgi::test::CurlEasyHandle easy_handle {};
//     ASSERT_EQ(curl_easy_setopt(easy_handle.get(), CURLOPT_URL,
//       "http://127.0.0.1/fastcgi"), CURLE_OK) <<
//         "CURLOPT_URL could not be set.";
//     as_components::fcgi::test::CurlSlist s_list {};
//     s_list.AppendString("Test-Header-1: first_test_header");
//     s_list.AppendString("Test-Header-2: second_test_header");
//     // It is assumed that this call cannot fail since easy_handle was set
//     // with a URL with http as the scheme.
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
//       {response.header_list().begin()}; h_iter !=
//         response.header_list().end();
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

struct AppServerTerminator
{
  inline ~AppServerTerminator()
  {
    EXPECT_EQ(kill(app_server_id, SIGTERM), 0) << std::strerror(errno);
  }

  pid_t app_server_id;
};

TEST(FcgiServerInterfaceIntegrationTesting, BodyAndNoBodyTest)
{
  try
  {
    // Gets the application server process ID so that it can be terminated when
    // the test exits.
    const char* str_ptr {std::getenv("TEST_TMPDIR")};
    ASSERT_NE(str_ptr, nullptr);
    std::string id_path {str_ptr};
    id_path.append("/fcgi_server_interface.pid");
    std::fstream app_server_id_stream {id_path, std::ios::in};
    ASSERT_TRUE(app_server_id_stream.good());
    pid_t app_server_id {};
    app_server_id_stream >> app_server_id;
    ASSERT_TRUE(app_server_id_stream.good());
    app_server_id_stream.close();
    ASSERT_GT(app_server_id, 0);
    AppServerTerminator app_server_terminator {app_server_id};

    // Note on response validation: The following properties are validated.
    // 1) Was the request produced by FcgiServerInterface and observed by the
    //    application as expected? This is determined by examining the value of
    //    the Request-Correct header.
    // 2) Were the expected custom response headers present and correct?
    // 3) Was the expected response body present and correct?
    const std::vector<std::uint8_t> kSuccessCode {'2','0','0'};
    const HeaderPair kRequestCorrectHeaderPair
    {
      {'R','e','q','u','e','s','t','-','C','o','r','r','e','c','t'},
      {'1'}
    };
    const HeaderPair kTestHeader1Pair
    {
      {'T','e','s','t','-','H','e','a','d','e','r','-','1'},
      {'f','i','r','s','t','_','t','e','s','t','_','h','e','a','d','e','r'}
    };
    const HeaderPair kTestHeader2Pair
    {
      {'T','e','s','t','-','H','e','a','d','e','r','-','2'},
      {'s','e','c','o','n','d','_','t','e','s','t','_','h','e','a','d','e','r'}
    };
    const std::vector<std::uint8_t> kResponseBody
      {'F','c','g','i','S','e','r','v','e','r','I','n','t','e','r','f','a','c',
       'e','!'};

    // Preconditions:
    // 1) The header list of *response_ptr has been sorted and does not contain
    //    duplicates.
    auto GTestNonFatalCustomHeaderCheck =
    [&kRequestCorrectHeaderPair, &kTestHeader1Pair, &kTestHeader2Pair]
    (CurlHttpResponse* response_ptr, int invocation_line)->void
    {
      ::testing::ScopedTrace tracer {__FILE__, invocation_line,
        "lambda GTestNonFatalHeaderCheck"};
      HeaderList::const_iterator header_begin
        {response_ptr->header_list().cbegin()};
      HeaderList::const_iterator header_end
        {response_ptr->header_list().cend()};
      HeaderList::const_iterator header_iter  {std::lower_bound(header_begin,
        header_end, kRequestCorrectHeaderPair, HeaderNameLessThan)};
      if((header_iter == header_end) || (header_iter->first !=
        kRequestCorrectHeaderPair.first))
      {
        ADD_FAILURE() << "The Request-Correct header was missing.";
      }
      else
      {
        EXPECT_EQ(header_iter->second, kRequestCorrectHeaderPair.second);
      }
      header_iter = std::lower_bound(header_begin, header_end,
        kTestHeader1Pair, HeaderNameLessThan);
      if((header_iter == header_end) || (header_iter->first !=
        kTestHeader1Pair.first))
      {
        ADD_FAILURE() << "The first custom test header was missing.";
      }
      else
      {
        EXPECT_EQ(header_iter->second, kTestHeader1Pair.second);
      }
      header_iter = std::lower_bound(header_begin, header_end,
        kTestHeader2Pair, HeaderNameLessThan);
      if((header_iter == header_end) || (header_iter->first !=
        kTestHeader2Pair.first))
      {
        ADD_FAILURE() << "The second custom test header was missing";
      }
      else
      {
        EXPECT_EQ(header_iter->second, kTestHeader2Pair.second);
      }
    };

    as_components::fcgi::test::CurlEasyHandle easy_handle {};
    ASSERT_EQ(curl_easy_setopt(easy_handle.get(), CURLOPT_URL,
      "http://127.0.0.1/fastcgi"), CURLE_OK) <<
        "CURLOPT_URL could not be set.";
    as_components::fcgi::test::CurlSlist s_list {};
    s_list.AppendString("Test-Header-1: first_test_header");
    s_list.AppendString("Test-Header-2: second_test_header");
    // It is assumed that this call cannot fail since easy_handle was set
    // with a URL with http as the scheme.
    curl_easy_setopt(easy_handle.get(), CURLOPT_HTTPHEADER, s_list.get());
    // Sends a request with the custom headers and no body.
    as_components::fcgi::test::CurlHttpResponse first_response {};
    first_response.Register(easy_handle.get());
    ASSERT_EQ(curl_easy_perform(easy_handle.get()), CURLE_OK);
    EXPECT_EQ(first_response.status_line().status_code, kSuccessCode);
    ASSERT_GT(first_response.header_list().size(), 0U);
    ASSERT_TRUE(SortHeadersAndCheckForDuplicates(&first_response));
    GTestNonFatalCustomHeaderCheck(&first_response, __LINE__);
    EXPECT_EQ(first_response.body(), kResponseBody);

    // Sends a POST request with the custom headers and a simple body.
    s_list.AppendString("Content-Type: text/plain");
    curl_easy_setopt(easy_handle.get(), CURLOPT_HTTPHEADER, s_list.get());
    curl_easy_setopt(easy_handle.get(), CURLOPT_POST, 1);
    curl_easy_setopt(easy_handle.get(), CURLOPT_POSTFIELDS, "POST");
    as_components::fcgi::test::CurlHttpResponse second_response {};
    second_response.Register(easy_handle.get());
    ASSERT_EQ(curl_easy_perform(easy_handle.get()), CURLE_OK);
    EXPECT_EQ(second_response.status_line().status_code, kSuccessCode);
    ASSERT_GT(second_response.header_list().size(), 0U);
    ASSERT_TRUE(SortHeadersAndCheckForDuplicates(&second_response));
    GTestNonFatalCustomHeaderCheck(&second_response, __LINE__);
    EXPECT_EQ(second_response.body(), kResponseBody);
  }
  catch(...)
  {
    FAIL() << "An exception was caught.";
  }
}

} // namespace test
} // namespace fcgi
} // namespace as_components
