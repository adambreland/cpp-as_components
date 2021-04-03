#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <exception>
#include <iostream>
#include <string_view>

extern "C" {
  #include <curl/curl.h>
}

#include "googletest/include/gtest/gtest.h"

#include "fcgi/test/include/curl_easy_handle_classes.h"

namespace as_components {
namespace fcgi {
namespace test {

// // Debug test
// // This test tries to connect to the application server. If it succeeds, then
// // it immediately closes the connection.
// TEST(FcgiServerInterfaceIntegrationTesting, UnixListenSockTest)
// {
//   alarm(2U);
//   int unix_connection {socket(AF_UNIX, SOCK_STREAM, 0)};
//   ASSERT_NE(unix_connection, -1) << "socket " << std::strerror(errno) << '\n';
//   struct sockaddr_un unix_address {};
//   unix_address.sun_family = AF_UNIX;
//   std::strcpy(unix_address.sun_path, "/tmp/fcgi_server_interface_integration_test_listen_sock");
//   ASSERT_NE(connect(unix_connection, static_cast<struct sockaddr*>(
//     static_cast<void*>(&unix_address)), sizeof(struct sockaddr_un)), -1) <<
//       "connect " << std::strerror(errno) << '\n';
//   close(unix_connection);
// }

// // Debug test
// // A test which makes an HTTP request and prints the response to the test log.
// TEST(FcgiServerInterfaceIntegrationTesting, DebugTest)
// {
//   try
//   {
//     as_components::fcgi::test::CurlEasyHandle easy_handle {};
//     ASSERT_EQ(curl_easy_setopt(easy_handle.get(), CURLOPT_URL,
//       "http://127.0.0.1/fastcgi"), CURLE_OK) << "CURLOPT_URL could not be set.";
//     as_components::fcgi::test::CurlSlist s_list {};
//     s_list.AppendString("Test-Header-1: first_test_header");
//     s_list.AppendString("Test-Header-2: second_test_header");
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

// TODO Terminate the application server directly.

} // namespace test
} // namespace fcgi
} // namespace as_components
