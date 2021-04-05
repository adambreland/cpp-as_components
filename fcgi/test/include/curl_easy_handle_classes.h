#ifndef AS_COMPONENTS_FCGI_TEST_INCLUDE_CURL_EASY_HANDLE_CLASSES_H_
#define AS_COMPONENTS_FCGI_TEST_INCLUDE_CURL_EASY_HANDLE_CLASSES_H_

#include <cstdint>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C" {
  #include <curl/curl.h>
}

namespace as_components {
namespace fcgi {
namespace test {

// A management class which allows automatic initialization and cleanup of the
// global CURL environment. A suitable instance should be constructed before
// using any CURL function. The lifetime of this instance should extend beyond
// any use of a CURL function.
class CurlEnvironmentManager
{
 public:
  inline CurlEnvironmentManager()
  {
    if(curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
    {
      throw std::runtime_error {"The environment for curl could not be "
        "initialized."};
    }
  }

  inline ~CurlEnvironmentManager()
  {
    curl_global_cleanup();
  }

  // No copy or move operations.
  CurlEnvironmentManager(const CurlEnvironmentManager&) = delete;
  CurlEnvironmentManager(CurlEnvironmentManager&&) = delete;
  CurlEnvironmentManager& operator=(const CurlEnvironmentManager&) = delete;
  CurlEnvironmentManager& operator=(CurlEnvironmentManager&&) = delete;
};

// A management class in the style of std::unique_ptr for CURL easy handles.
// Default construction attempts to allocate a handle with curl_easy_init.
class CurlEasyHandle
{
 public:
  inline CurlEasyHandle()
  : curl_easy_handle_ {curl_easy_init()}
  {
    if(!curl_easy_handle_)
    {
      throw std::runtime_error {"A curl easy handle could not be created."};
    }
  }

  // Move-only.
  CurlEasyHandle(const CurlEasyHandle&) = delete;
  inline CurlEasyHandle(CurlEasyHandle&& handle) noexcept
  : curl_easy_handle_ {handle.curl_easy_handle_}
  {
    handle.clear();
  }

  CurlEasyHandle& operator=(const CurlEasyHandle&) = delete;
  inline CurlEasyHandle& operator=(CurlEasyHandle&& handle) noexcept
  {
    swap(handle);
    return *this;
  }

  inline CURL* get() const noexcept
  {
    return curl_easy_handle_;
  }

  inline CURL* release() noexcept
  {
    CURL* temp_ptr {curl_easy_handle_};
    clear();
    return temp_ptr;
  }

  // An invocation of reset implies easy handle cleanup on *this. This means
  // that eh.reset(eh.get()) is invalid for CurlEasyHandle instance eh.
  inline void reset(CURL* curl_easy_handle) noexcept
  {
    cleanup();
    curl_easy_handle_ = curl_easy_handle;
  }

  inline void swap(CurlEasyHandle& handle) noexcept
  {
    CURL* temp_handle {handle.curl_easy_handle_};
    handle.curl_easy_handle_ = curl_easy_handle_;
    curl_easy_handle_ = temp_handle;
  }

  inline explicit operator bool() const noexcept
  {
    return curl_easy_handle_;
  }

  inline ~CurlEasyHandle()
  {
    cleanup();
  }

 private:
  inline void cleanup() noexcept
  {
    // It is safe to pass a null pointer to curl_easy_cleanup.
    curl_easy_cleanup(curl_easy_handle_);
  }

  inline void clear() noexcept
  {
    curl_easy_handle_ = nullptr;
  }

  CURL* curl_easy_handle_;
};

// A management class in the style of std::unique_ptr for curl_slist*
// instances returned by curl_slist_append. This class is intended to ease the
// configuration of the CURLOPT_HTTPHEADER option for CURL easy handles.
//
// 1) Each instance manages at most one curl_slist.
// 2) Default construction does not attempt to allocate a curl_slist.
//
// Example of use:
// /* The lifetime of s_list should extend beyond the point when the handle
//    will no longer be used for transfers with this setting of the option. */
// CurlSlist s_list {};
// s_list.AppendString("Vary: User-Agent");
// curl_easy_setopt(easy_handle_ptr, CURLOPT_HTTPHEADER, s_list.get());
class CurlSlist
{
 public:
  CurlSlist() = default;

  // Move-only
  CurlSlist(const CurlSlist&) = delete;
  inline CurlSlist(CurlSlist&& slist) noexcept
  : curl_slist_ptr_ {slist.curl_slist_ptr_}
  {
    slist.clear();
  }

  CurlSlist& operator=(const CurlSlist&) = delete;
  inline CurlSlist& operator=(CurlSlist&& slist) noexcept
  {
    swap(slist);
    return *this;
  }

  // AppendString
  // These overloads provide an interface to the curl_slist append operation.
  //
  // Exceptions:
  // 1) std::runtime_error is thrown if a call to curl_slist_append returned
  //    a null pointer.
  inline CurlSlist& AppendString(const char* char_ptr)
  {
    return AppendStringHelper(char_ptr);
  }

  inline CurlSlist& AppendString(const std::string& str)
  {
    return AppendStringHelper(str.data());
  }

  inline curl_slist* get() const noexcept
  {
    return curl_slist_ptr_;
  }

  inline struct curl_slist* release() noexcept
  {
    struct curl_slist* temp_ptr {curl_slist_ptr_};
    clear();
    return temp_ptr;
  }

  inline void reset(struct curl_slist* curl_slist_ptr) noexcept
  {
    cleanup();
    curl_slist_ptr_ = curl_slist_ptr;
  }

  inline void swap(CurlSlist& slist) noexcept
  {
    struct curl_slist* temp_ptr {slist.curl_slist_ptr_};
    slist.curl_slist_ptr_ = curl_slist_ptr_;
    curl_slist_ptr_ = temp_ptr;
  }

  inline explicit operator bool() const noexcept
  {
    return curl_slist_ptr_;
  }

  inline ~CurlSlist()
  {
    cleanup();
  }

 private:
  CurlSlist& AppendStringHelper(const char* char_ptr);

  inline void cleanup() noexcept
  {
    // It is safe to pass a null pointer argument to curl_slist_free_all.
    curl_slist_free_all(curl_slist_ptr_);
  }

  inline void clear() noexcept
  {
    curl_slist_ptr_ = nullptr;
  }
  
  struct curl_slist* curl_slist_ptr_ {nullptr};
};

// Functions for the CURLOPT_HEADERFUNCTION and CURLOPT_WRITEFUNCTION options
// of a CURL easy handle. Note that these functions are not designed to handle
// multipart MIME types (i.e. multipart or message types) or chunked transfer
// encoding.

// friend of CurlHttpResponse
//
// A function to be used as the callback for the CURLOPT_HEADERFUNCTION option
// of a CURL easy handle. This callback can be simultaneously registered with
// distinct easy handles. It checks that a valid CurlHttpResponse instance is
// associated with an easy handle, and, if one is present, it processes and
// stores a header in the instance.
//
// Parameters: Defined by the CURL easy API. userdata should be null or the
//             value of a pointer to a CURL easy handle (CURL*).
//
// Preconditions:
// 1) It is assumed that nitems != 0.
//
// Exceptions and errors:
// 1) This is a C++ function with C naming and calling conventions (language
//    linkage). All errors, including exceptions, cause the function to return
//    0. No exception is propagated beyond a call.
// 2) Exceptions can only occur after a non-null pointer to a
//    CurlHttpResponse instance was provided by
//    CurlHttpResponse::RegisteredResponse. Let p be this pointer. If an
//    exception occurred, then p->receipt_error() returns true and the response
//    instance was deregistered.
// 3) If an error occurred because the status line or a header line did not
//    match the expected pattern, then, in addition returning 0:
//    a) The receipt error flag was set.
//    b) The match error flag was set.
//    c) The instance was deregistered.
//    d) An attempt was made to copy the line to the error line field of the
//       response instance.
// 4) The function may attempt to write to std::cerr to report errors.
//
// Effects:
// 1) If userdata, when cast to CURL*, was not associated with a
//    CurlHttpResponse instance as determined by the value returned from a call
//    to CurlHttpResponse::RegisteredResponse, then 0 was returned.
// 2) If userdata, when cast to CURL*, was associated with a CurlHttpResponse
//    instance which had previously been used to receive a complete set of
//    headers (terminal_header_line_received() == true), then the instance
//    was deregistered and 0 was returned.
// 3) If userdata, when cast to CURL*, was associated with a valid
//    CurlHttpResponse instance, then the header line given by
//    [buffer, buffer + nitems) was processed. If no error occurred during
//    processing, then the CurlHttpResponse instance was updated appropriately.
//    a) The status_line_received() returns true if a status line was received.
//       status_line() holds the parts of the status line.
//    b) header_list() was updated with the header name-value pair if a header
//       line was received.
//    c) header_list_complete() returns true if the empty line which indicates
//       the end of headers was received.
extern "C" std::size_t HeaderProcessor(char* buffer, std::size_t size,
  std::size_t nitems, void* userdata);

// friend of CurlHttpResponse
//
// A function to be used as the callback for the CURLOPT_WRITEFUNCTION option
// of a CURL easy handle. This callback can be simultaneously registered with
// distinct easy handles. It checks that a valid CurlHttpResponse instance is
// associated with an easy handle, and, if one is present, it stores the
// received portion of the HTTP response body in the instance.
//
// Parameters: Defined by the CURL easy API. userdata should be null or the
//             value of a pointer to a CURL easy handle (CURL*).
//
// Preconditions: none
//
// Exceptions and errors:
// 1) This is a C++ function with C naming and calling conventions. All errors,
//    including exceptions, cause the function to return 0. No exception is
//    propagated beyond a call.
// 2) Exceptions can only occur after a non-null pointer to a
//    CurlHttpResponse instance was provided by
//    CurlHttpResponse::RegisteredResponse. Let p be this pointer. If an
//    exception occurred, then p->receipt_error() returns true and the response
//    instance was deregistered.
// 3) In addition, if an exception occurred:
//    1) An attempt was made to write the received body portion to the
//       error_line field of the response instance.
//    2) An attempt may have been made to write an error message to std::cerr.
//
// Effects:
// 1)    If userdata, when cast to CURL*, was not associated with a
//    CurlHttpResponse instance as determined by the value returned from a call
//    to CurlHttpResponse::RegisteredResponse, then 0 was returned.
//       This should not occur given the implied ordering of HeaderProcessor
//    and BodyProcessor.
// 2) If userdata, when cast to CURL*, was associated with a valid
//    CurlHttpResponse instance, then an attempt was made to insert
//    [buffer, buffer + nmemb) at the end of the body vector of the response
//    instance.
// 3) Note that this function does not deregister CurlHttpResponse instances
//    from non-exceptional execution.
extern "C" std::size_t BodyProcessor(char* buffer, std::size_t size,
  std::size_t nmemb, void* userdata);

using HeaderPair = std::pair<std::vector<std::uint8_t>,
  std::vector<std::uint8_t>>;
using HeaderList = std::vector<HeaderPair>;

struct StatusLine
{
  std::vector<std::uint8_t> version;
  std::vector<std::uint8_t> status_code;
  std::vector<std::uint8_t> status_text;
};

//    A member of this class serves as an abstraction of a collection of
// buffers which store the HTTP headers and the body of an HTTP response. This
// class offers usage error checking which would not occur if buffers were
// manually set with calls to curl_easy_setopt with CURLOPT_HEADERDATA and
// CURLOPT_WRITEDATA.
//    This class only affects the CURLOPT_HEADERFUNCTION, CURLOPT_HEADERDATA,
// CURLOPT_WRITEFUNCTION, and CURLOPT_WRITEDATA options of the CURL easy
// handles which are passed to it with pointer arguments. The other options of
// easy handles are managed by the user of the class.
//
// Preconditions:
// 1) Use of this class requires that the lifetime of any CURL easy handle
//    which is associated with instances of the class extends beyond the
//    lifetime of the associated instances.
class CurlHttpResponse
{
 public:
  CurlHttpResponse() = default;

  // No copy or move operations.
  CurlHttpResponse(const CurlHttpResponse&) = delete;
  CurlHttpResponse(CurlHttpResponse&&) = delete;

  CurlHttpResponse& operator=(const CurlHttpResponse&) = delete;
  CurlHttpResponse& operator=(CurlHttpResponse&&) = delete;

  // This function is used to register a CurlHttpResponse instance with a CURL
  // easy handle before a call to curl_easy_perform which takes the easy handle
  // as an argument.
  //
  // Parameters:
  // easy_handle_ptr: A pointer to a CURL easy handle. Such a pointer is
  //                  initially obtained from a call to curl_easy_init.
  //
  // Preconditions:
  // 1) If easy_handle_ptr is non-null, then it refers to a valid CURL easy
  //    handle.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) Strong exception guarantee.
  // 3) A std::logic_error exception is thrown in the following cases:
  //    a) easy_handle_ptr == nullptr
  //    b) The CurlHttpResponse instance is already associated with a CURL
  //       easy handle, has been used to receive response information, or
  //       is in an error state (processing_error() returns true).
  //
  // Effects:
  // 1) *this and the state of CurlHttpResponse were updated to reflect the
  //    association of *this with easy_handle_ptr.
  //    a) If easy_handle_ptr was already associated with a CurlHttpResponse
  //       instance, then that association was dissolved.
  // 2) HeaderProcessor and BodyProcessor were set on *easy_handle_ptr with
  //    CURLOPT_HEADERFUNCTION and CURLOPT_WRITEFUNCTION, respectively.
  // 3) The CURLOPT_HEADERDATA and CURLOPT_WRITEDATA options were set on
  //    *easy_handle_ptr so that a call to curl_easy_perform may be made with
  //    easy_handle_ptr as an argument.
  // 4) Once any CurlHttpResponse instance is registered with an easy handle,
  //    the functions which were set on the easy handle for the options
  //    CURLOPT_HEADERFUNCTION and CURLOPT_WRITEFUNCTION will remain set on the
  //    easy handle. The default settings can be restored with
  //    curl_easy_restore. Alternatively, another function can be explicitly
  //    set on the easy handle by a call to curl_easy_setopt with the
  //    approprpiate option argument.
  void Register(CURL* easy_handle_ptr);

  // This function is used to deregister a CurlHttpResponse instance from a
  // CURL easy handle.
  // 1) Use of this function is optional.
  // 2) Invoking the function on an instance which was not registered with a
  //    CURL easy handle has no effect.
  // 3) If an instance was registered, never used to receive data, and then
  //    deregistered, it may be registered again.
  void Deregister() noexcept;

  // non-constant reference.
  inline StatusLine& status_line() noexcept
  {
    return status_line_;
  }

  inline const StatusLine& status_line() const noexcept
  {
    return status_line_;
  }

  inline bool status_line_received() const noexcept
  {
    return status_line_received_;
  }

  // non-constant reference.
  inline HeaderList& header_list() noexcept
  {
    return header_list_;
  }

  inline const HeaderList& header_list() const noexcept
  {
    return header_list_;
  }

  inline bool header_list_complete() const noexcept
  {
    return terminal_header_line_received_;
  }

  // non-constant reference.
  inline std::vector<std::uint8_t>& body() noexcept
  {
    return body_;
  }

  inline const std::vector<std::uint8_t>& body() const noexcept
  {
    return body_;
  }

  inline bool processing_error() const noexcept
  {
    return receipt_error_;
  }

  inline bool match_error() const noexcept
  {
    return match_error_;
  }

  // non-constant reference.
  inline std::vector<std::uint8_t>& error_line() noexcept
  {
    return error_line_;
  }

  ~CurlHttpResponse();

 private:
  friend std::size_t HeaderProcessor(char* buffer, std::size_t size,
    std::size_t nitems, void* userdata);
  friend std::size_t BodyProcessor(char* buffer, std::size_t size,
    std::size_t nmemb, void* userdata);

  // A function for use by HeaderProcessor and BodyProcessor which provides
  // an interface to the registration information of CurlHttpResponse. It
  // partly manages the cache for a CurlHttpResponse* value.
  //
  // Parameters:
  // userdata: The pointer provided to the header or body callback by the CURL
  //           easy API.
  //
  // Preconditions: none
  //
  // Effects:
  // 1) If userdata was null or (CURL*)userdata was not a key of
  //    registration_map_, then nullptr was returned.
  // 2) If userdata was not null and (CURL*)userdata was a key of
  //    registration_map_, then registration_map_[(CURL*)userdata] was
  //    returned.
  static CurlHttpResponse* RegisteredResponse(void* userdata) noexcept;

  // State for a single-item CurlHttpResponse* cache.
  static CURL* cache_easy_handle_ptr_;
  static CurlHttpResponse* cached_response_ptr_;

  static std::map<CURL*, CurlHttpResponse*> registration_map_;

  CURL* denormalized_easy_handle_ptr_ {nullptr};
  bool receipt_error_ {false};
  bool match_error_ {false};
  std::vector<std::uint8_t> error_line_ {};
  struct StatusLine status_line_ {};
  bool status_line_received_ {false};
  HeaderList header_list_ {};
  bool terminal_header_line_received_ {false};
  std::vector<std::uint8_t> body_ {};
};

// Helper functions for CurlHttpResponse.
inline bool HeaderNameLessThan(const HeaderPair& lhs,
  const HeaderPair& rhs)
{
  return (lhs.first < rhs.first);
};

inline bool HeaderNameEquality(const HeaderPair& lhs,
  const HeaderPair& rhs)
{
  return (lhs.first == rhs.first);
};

// Sorts the headers by header name and returns true if no duplicates were
// found and false otherwise.
bool SortHeadersAndCheckForDuplicates(CurlHttpResponse* response_ptr);

} // namespace test
} // namespace fcgi
} // namespace as_components

#endif // AS_COMPONENTS_FCGI_TEST_INCLUDE_CURL_EASY_HANDLE_CLASSES_H_
