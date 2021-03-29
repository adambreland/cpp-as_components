#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

extern "C" {
  #include <curl/curl.h>
}

#include "googletest/include/gtest/gtest.h"

#include "fcgi/test/include/curl_easy_handle_classes.h"

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

// Ensure that the CURL environment is initialized before the program starts.
CurlEnvironmentManager curl_environment {};

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

// CurlHttpResponse
// Test cases:
// 1) A std::logic_error exception is thrown when Register is called.
//    a) easy_handle_ptr == nullptr
//    b) The instance is already associated with an easy handle.
//    c) The instance has been used to receive the information for a request.
//    d) The instance was used and became associated with an error.
// 2) Deregister is called on an instance which was never registered.
// 3) An new instance is registered, deregistered, and then registered again.
//    The instance is then successfully used to receive response information.
TEST(CurlHttpResponse, Exceptions)
{
  CurlHttpResponse curl_response_1 {};
  EXPECT_THROW(curl_response_1.Register(nullptr), std::logic_error);

  EXPECT_NO_THROW(
  {
    CurlEasyHandle easy_handle_1 {};
    CurlEasyHandle easy_handle_2 {};
    CurlHttpResponse curl_response_2 {};
    CurlHttpResponse curl_response_3 {};
    curl_response_2.Register(easy_handle_1.get());
    // curl_response_2 is already registered; these should throw.
    ASSERT_THROW(curl_response_2.Register(easy_handle_1.get()),
      std::logic_error);
    ASSERT_THROW(curl_response_2.Register(easy_handle_2.get()),
      std::logic_error);
    // curl_response_3 is not registered. It can take easy_handle_1.
    ASSERT_NO_THROW(curl_response_3.Register(easy_handle_1.get()));
    // When easy_handle_1 was taken from curl_response_2, it should have become
    // deregistered. Re-registration should then be possible.
    EXPECT_NO_THROW(curl_response_2.Register(easy_handle_2.get()));
  });

  // TODO ********************************************
}



// HeaderProcessor and BodyProcessor
// Examines properties:
// 1) Failure cases:
//    a) No instance is registered for an easy handle.
//       1) An instance was never registered.
//       2) An instance was registered, and then deregistered.
//       3) An instance was registered, used to completion, and then an attempt
//          was made to use the instance again. This was detected, and the
//          instance was deregistered.
//    b) An instance is used after it has already been used. This case can be
//       combined with a.3.
//    c) For each of a header and status line: mismatch against the expected
//       pattern.
// 2) Non-failure cases:
//    a)

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components
