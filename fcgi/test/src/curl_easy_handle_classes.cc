#include "fcgi/test/include/curl_easy_handle_classes.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <vector>

extern "C" {
  #include <curl/curl.h>
}

namespace as_components {
namespace fcgi {
namespace test {

// CurlSlist

CurlSlist& CurlSlist::AppendStringHelper(const char* char_ptr)
{
  struct curl_slist* new_head {curl_slist_append(curl_slist_ptr_, char_ptr)};
  if(new_head)
  {
    curl_slist_ptr_ = new_head;
  }
  else
  {
    throw std::runtime_error {"A string could not be added to a "
      "CurlSlist instance."};
  }
  return *this;
}

// CurlHttpResponse

// Splits a response status line into version, status code, and status text
// parts.
//
// Regex description:
// (HTTP/.+?)  "HTTP/" followed by a non-greedy sequence of one or more
//             characters where the entire sequence is treated as a submatch.
//             (Version submatch.)
// \s+         A greedy sequence or one or more blank characters which is not
//             treated as a submatch.
// ([1-5]\d\d) A sequence as follows which is treated as a submatch: A digit in
//             the range 1 to 5, any digit, any digit. (Status code submatch.)
// \s+         As above.
// (.*?)       A non-greedy sequence of zero or more characters which is
//             treated as a submatch. (Status text submatch.)
// \s*         A greedy sequence of zero or more white space characters.
// (?:\n|\r\n) A terminal line separator. Either newline or the sequence
//             (carriage return, newline).
// Regex submatch count: 3
const std::regex status_line_regex
{R"((HTTP/.+?)\s+([1-5]\d\d)\s+(.*?)\s*(?:\n|\r\n))"};

// Splits a header into a name and value. White space which appears before the
// value is discarded. Trailing line separator characters are discarded.  A
// line separator may be newline only or carriage return followed by newline.
//
// Regex description:
// (.+?)       A non-greedy sequence of one or more characters which is treated
//             as a submatch. (Header name submatch.)
// :           A colon.
// \s*         A sequence of zero or more blanks characters which are not line
//             separators where the sequence is not treated as a submatch. This
//             removes leading white space from any header value.
// (.*?)       A non-greedy sequence of zero or more characters which is
//             treated as a submatch. (Header value submatch.)
// \s*         A greedy sequence of zero or more white space characters.
// (?:\n|\r\n) The newline character or the carriage return and newline
//             sequence.
// Regex submatch count: 2
const std::regex header_regex {R"((.+?):\s*(.*?)\s*(?:\n|\r\n))"};

// static data members
CURL* CurlHttpResponse::cache_easy_handle_ptr_ {nullptr};
CurlHttpResponse* CurlHttpResponse::cached_response_ptr_ {nullptr};
std::map<CURL*, CurlHttpResponse*> CurlHttpResponse::registration_map_ {};

// HeaderProcessor and BodyProcessor implementation notes:
// 1) Both of these functions assume that the response object pointed to by the
//    pointer returned by RegisteredResponse does not have its receipt_error_
//    flag set. To allow this assumption, these functions are responsible for
//    setting the receipt_error_ flag and deregistering the response when an
//    error is encountered. A deregistered response with its receipt_error_
//    flag set cannot be registered.
// 2) HeaderProcessor is responsible for detecting the reuse of a response in
//    the case that a receipt error did not occur. Such a response must be
//    deregistered to prevent propagation of this error.
// 3) Deregistering a response by a call to Deregister will perform a cache
//    update if one is needed.

constexpr const char* null_response_message {
  "The CurlHttpResponse* value received from "
  "CurlHttpResponse::RegisteredResponse was null.\n"};

// friend of CurlHttpResponse
// extern "C"
std::size_t HeaderProcessor(char* buffer, std::size_t, std::size_t nitems,
  void* userdata)
{
  // This implementation assumes nitems != 0.

  // This section must not throw. A try block with a catch-all follows.
  constexpr const char* exception_message {"An exception occurred during an "
    "invocation of HeaderProcessor.\n"};
  CurlHttpResponse* response_ptr {CurlHttpResponse::RegisteredResponse(
    userdata)};
  // Validate the response buffer relative to the context (header reception).
  // receipt_error_ need not be checked.
  if(!response_ptr)
  {
    std::cerr << __FILE__ << '\n' << __func__ << '\n' <<
      null_response_message << "Pointer value: " << userdata << '\n';
    return 0U;
  }
  if(response_ptr->terminal_header_line_received_)
  {
    // This condition detects the reuse of a response. Since calls to
    // BodyProcessor should only occur after a call to HeaderProcessor, this
    // condition detects all cases of response reuse when a receipt error did
    // not occur.
    response_ptr->Deregister();
    std::cerr << __FILE__ << '\n' << __func__ << '\n' <<
      "The header list of the CurlHttpResponse object was already complete.\n";
    return 0U;
  }
  // Header processing start.
  try
  {
    std::cmatch match {};
    const char* cbuffer {buffer};
    const char* line_end {cbuffer + nitems};

    auto UpdateOnMatchError = [response_ptr, cbuffer, line_end]()->void
    {
      response_ptr->match_error_ = true;
      // Order error line copying here so that a throw will not cause
      // deregistration to occur twice.
      response_ptr->error_line_ = std::vector<std::uint8_t> {cbuffer,
          line_end};
      response_ptr->Deregister();
      response_ptr->receipt_error_ = true;
    };

    if(!(response_ptr->status_line_received_))
    {
      if(!std::regex_match(cbuffer, line_end, match, status_line_regex))
      {
        UpdateOnMatchError();
        std::string_view status_line_view {cbuffer, nitems};
        std::cerr << __FILE__ << '\n' <<__func__ << '\n' <<
          "The status line did not match the expected pattern.\n" <<
          "Status line:\n" << status_line_view << '\n';
        return 0U;
      }
      else
      {
        StatusLine status_line {
          {match[1].first, match[1].second},
          {match[2].first, match[2].second},
          {match[3].first, match[3].second}
        };
        response_ptr->status_line_ = std::move(status_line);
        response_ptr->status_line_received_ = true;
        return nitems;
      }
    }
    else
    {
      char first_char {*cbuffer};
      if((first_char == '\n') || (first_char == '\r'))
      {
        response_ptr->terminal_header_line_received_ = true;
        return nitems;
      }
      else if(!std::regex_match(cbuffer, line_end, match, header_regex))
      {
        UpdateOnMatchError();
        std::string_view header_view {cbuffer, nitems};
        std::cerr << __FILE__ << '\n' << __func__ << '\n' <<
          "A header did not match the expected pattern.\n" <<
          "Header:\n" << header_view << '\n';
        return 0U;
      }
      else
      {
        HeaderPair header_pair {
          {match[1].first, match[1].second},
          {match[2].first, match[2].second}
        };
        response_ptr->header_list_.push_back(std::move(header_pair));
        return nitems;
      }
    }
  }
  catch(std::exception& e)
  {
    response_ptr->Deregister();
    response_ptr->receipt_error_ = true;
    try
    {
      std::cerr << __FILE__ << '\n' << exception_message << e.what() << '\n';
    }
    catch(...)
    {}
    return 0U;
  }
  catch(...)
  {
    response_ptr->Deregister();
    response_ptr->receipt_error_ = true;
    try
    {
      std::cerr << __FILE__ << '\n' << exception_message;
    }
    catch(...)
    {}
    return 0U;
  }
}

// friend of CurlHttpResponse
// extern "C"
std::size_t BodyProcessor(char* buffer, std::size_t, std::size_t nmemb,
  void* userdata)
{
  // The special case where nmemb == 0 is handled by the implementation for
  // the cases where nmemb != 0.

  constexpr const char* exception_message {"An exception occurred during an "
    "invocation of BodyProcessor.\n"};

  // This section must not throw. A try block with a catch-all follows.
  CurlHttpResponse* response_ptr {CurlHttpResponse::RegisteredResponse(
    userdata)};
  // receipt_error_ need not be checked.
  if(!response_ptr)
  {
    //    The combination !response_ptr && nmemb == 0 should not occur. If
    // !response_ptr, then a previous call to HeaderProcessor would have
    // resulted in an error. In that case, response processing by the CURL API
    // would have halted before BodyProcessor is called.
    //    The check for !response_ptr is a defensive check that should never
    // fail given the reasoning above.
    std::cerr << __FILE__ << '\n' << __func__ << '\n' <<
      null_response_message << "Pointer value: " << userdata << '\n';
    return 0U;
  }
  // Body processing start.
  char* body_portion_end {buffer + nmemb};

  auto ErrorLineInsert = [response_ptr, buffer, body_portion_end]()->void
  {
    std::vector<std::uint8_t>& error_line {response_ptr->error_line_};
    error_line.insert(error_line.end(), buffer, body_portion_end);
  };

  try
  {
    std::vector<std::uint8_t>& body {response_ptr->body_};
    body.insert(body.end(), buffer, body_portion_end);
    return nmemb;
  }
  catch(std::exception& e)
  {
    response_ptr->Deregister();
    response_ptr->receipt_error_ = true;
    try
    {
      std::cout << exception_message << e.what() << '\n';
      ErrorLineInsert();
    }
    catch(...)
    {}
    return 0;
  }
  catch(...)
  {
    response_ptr->Deregister();
    response_ptr->receipt_error_ = true;
    try
    {
      std::cout << exception_message;
      ErrorLineInsert();
    }
    catch(...)
    {}
    return 0;
  }
}

CurlHttpResponse::~CurlHttpResponse()
{
  Deregister();
}

// This implementation assumes that:
// 1) !!denormalized_easy_handle_ptr_ if and only if there exists a key of
//    registration_map_ whose value is this.
// 2) The mathematical map represented by registration_map_ is injective
//    (one-to-one).
void CurlHttpResponse::Deregister() noexcept
{
  if(denormalized_easy_handle_ptr_)
  {
    // Erasure cannot throw given the comparison operator of registration_map_.
    registration_map_.erase(denormalized_easy_handle_ptr_);
    // These invocations should always return CURLE_OK.
    curl_easy_setopt(denormalized_easy_handle_ptr_, CURLOPT_HEADERFUNCTION,
      nullptr);
    curl_easy_setopt(denormalized_easy_handle_ptr_, CURLOPT_HEADERDATA,
      nullptr);
    curl_easy_setopt(denormalized_easy_handle_ptr_, CURLOPT_WRITEFUNCTION,
      nullptr);
    curl_easy_setopt(denormalized_easy_handle_ptr_, CURLOPT_WRITEDATA, nullptr);
    // Conditionally updates the cached CurlHttpResponse*.
    if(denormalized_easy_handle_ptr_ == cache_easy_handle_ptr_)
    {
      cache_easy_handle_ptr_ = nullptr;
      cached_response_ptr_ = nullptr;
    }
    denormalized_easy_handle_ptr_ = nullptr;
  }
}

void CurlHttpResponse::Register(CURL* easy_handle_ptr)
{
  if(!easy_handle_ptr)
  {
    throw std::logic_error {"CurlHttpResponse::Register was called with a "
      "null argument."};
  }
  if(!denormalized_easy_handle_ptr_ && !status_line_received_ &&
     !receipt_error_)
  {
    std::map<CURL*, CurlHttpResponse*>::iterator map_iter
      {registration_map_.lower_bound(easy_handle_ptr)};
    if((map_iter != registration_map_.end()) &&
       (map_iter->first == easy_handle_ptr))
    {
      // Deregisters the previously registered CurlHttpResponse object.
      map_iter->second->denormalized_easy_handle_ptr_ = nullptr;
      // Registers the current CurlHttpResponse object.
      map_iter->second = this;
    }
    else
    {
      registration_map_.insert(map_iter, {easy_handle_ptr, this});
    }
    // These invocations should always return CURLE_OK.
    curl_easy_setopt(easy_handle_ptr, CURLOPT_HEADERFUNCTION, &HeaderProcessor);
    curl_easy_setopt(easy_handle_ptr, CURLOPT_HEADERDATA, easy_handle_ptr);
    curl_easy_setopt(easy_handle_ptr, CURLOPT_WRITEFUNCTION, &BodyProcessor);
    curl_easy_setopt(easy_handle_ptr, CURLOPT_WRITEDATA, easy_handle_ptr);
    denormalized_easy_handle_ptr_ = easy_handle_ptr;
    // Conditionally updates the cached CurlHttpResponse*.
    // The second condition ensures that if a previously registered response
    // was deregistered by this call and a pointer to the previous response was
    // cached, then the cache is appropriately updated.
    if(!cache_easy_handle_ptr_ || (easy_handle_ptr == cache_easy_handle_ptr_))
    {
      cache_easy_handle_ptr_ = easy_handle_ptr;
      cached_response_ptr_ = this;
    }
  }
  else
  {
    // Throws due to the invalidity of *this for registration.
    throw std::logic_error {"CurlHttpResponse::Register was called on an "
      "object which was not valid for registration."};
  }
}

// static member function
CurlHttpResponse* CurlHttpResponse::RegisteredResponse(void* userdata) noexcept
{
  if(!userdata)
  {
    return nullptr;
  }
  CURL* easy_handle_ptr {reinterpret_cast<CURL*>(userdata)};
  if(cache_easy_handle_ptr_ != easy_handle_ptr)
  {
    std::map<CURL*, CurlHttpResponse*>::iterator map_iter
      {registration_map_.find(easy_handle_ptr)};
    if(map_iter == registration_map_.end())
    {
      return nullptr;
    }
    // Updates the cached pointer to the newly found value.
    cache_easy_handle_ptr_ = easy_handle_ptr;
    return (cached_response_ptr_ = map_iter->second);
  }
  return cached_response_ptr_;
}

} // namespace test
} // namespace fcgi
} // namespace as_components
