#include <cstdlib>
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
namespace test {

// Ensure that the CURL environment is initialized before the program starts.
CurlEnvironmentManager curl_environment {};

TEST(CurlEasyHandleClasses, SimpleTest)
{
  try
  {
    as_components::fcgi::test::CurlEasyHandle easy_handle {};
    ASSERT_EQ(curl_easy_setopt(easy_handle.get(), CURLOPT_URL,
      "http://localhost"), CURLE_OK) << "CURLOPT_URL could not be set.";
    as_components::fcgi::test::CurlSlist s_list {};
    s_list.AppendString("Vary: User-Agent");
    // It is assumed that this call cannot fail since easy_handle was set with
    // a URL with http as the scheme.
    curl_easy_setopt(easy_handle.get(), CURLOPT_HTTPHEADER, s_list.get());
    as_components::fcgi::test::CurlHttpResponse response {};
    // Implicitly sets CURLOPT_HEADERFUNCTION, CURLOPT_HEADERDATA,
    // CURLOPT_WRITEFUNCTION, and CURLOPT_WRITEDATA.
    response.Register(easy_handle.get());
    CURLcode perform_result {};
    ASSERT_EQ((perform_result = curl_easy_perform(easy_handle.get())),
      CURLE_OK) << "curl_easy_perform failed. Curl error number: " <<
        perform_result << '\n' << curl_easy_strerror(perform_result);
    std::cout << "Status line: " <<
      std::string_view
      {
        reinterpret_cast<char*>(response.status_line().version.data()),
        response.status_line().version.size()
      } << ' ' <<
      std::string_view
      {
        reinterpret_cast<char*>(response.status_line().status_code.data()),
        response.status_line().status_code.size()
      } << ' ' <<
      std::string_view
      {
        reinterpret_cast<char*>(response.status_line().status_text.data()),
        response.status_line().status_text.size()
      } << '\n';
    std::cout << "\nHeaders:\n";
    for(as_components::fcgi::test::HeaderList::iterator h_iter
      {response.header_list().begin()}; h_iter != response.header_list().end();
      ++h_iter)
    {
      std::cout <<
        std::string_view {
          reinterpret_cast<char*>(h_iter->first.data()),
          h_iter->first.size()
        } << ": " <<
        std::string_view {
          reinterpret_cast<char*>(h_iter->second.data()),
          h_iter->second.size()
        } << '\n';
    }
    std::string_view body_view
      {reinterpret_cast<const char*>(response.body().data()),
        response.body().size()};
    std::cout << "\nBody:\n" << body_view << '\n';
  }
  catch(std::exception& e)
  {
    FAIL() << "An exception derived from std::exception was caught. " <<
      e.what();
  }
  catch(...)
  {
    FAIL() << "An unknown exception type was caught.\n";
  }
}

} // namespace test
} // namespace test
} // namespace fcgi
} // namespace as_components
