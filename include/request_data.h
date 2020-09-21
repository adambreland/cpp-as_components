#ifndef FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_DATA_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_DATA_H_

#include <cstdint>

#include <map>
#include <vector>

namespace fcgi_si {

enum class RequestStatus {kRequestPending, kRequestAssigned};

class RequestData {
public:

  using size = std::allocator_traits<std::allocator<std::uint8_t>>::size_type;

  inline void set_connection_closed_by_interface() noexcept
  {
    connection_closed_by_interface_ = true;
  }

  inline RequestStatus get_status() const noexcept
  {
    return request_status_;
  }

  inline bool get_abort() const noexcept
  {
    return client_set_abort_;
  }

  inline void set_abort() noexcept
  {
    client_set_abort_ = true;
  }

  inline bool get_close_connection() const noexcept
  {
    return close_connection_;
  }

  inline std::uint16_t get_role() const noexcept
  {
    return role_;
  }

  bool CheckRequestCompletionWithConditionalUpdate() noexcept;

  // Attempts to convert the FCGI_PARAMS byte sequence which is encoded in the
  // FastCGI name-value pair format into a std::map object with names as 
  // map keys and values as map values. The map is held within the
  // RequestData object and later used in the construction of an FcgiRequest
  // object.
  //
  // Parameters: none
  // 
  // Preconditions:
  // 1) This method may only be called once the FCGI_PARAMS stream is complete
  //    as determined by get_PARAMS_completion.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) In the event of a throw, the call had no effect on the RequestData
  //    object (strong exception guarantee).
  //
  // Effects:
  // 1) If true was returned, the FCGI_PARAMS byte sequence was used to
  //    construct an internal environment variable map.
  // 2) If false was returned, the FCGI_PARAMS byte sequence had a FastCGI
  //    name-value pair binary formatting error or the list of environment
  //    variable definitions had distinct definitions for the same variable.
  bool ProcessFCGI_PARAMS();

  inline bool get_PARAMS_completion() const noexcept
  {
    return FCGI_PARAMS_complete_;
  }

  inline void CompletePARAMS() noexcept
  {
    FCGI_PARAMS_complete_ = true;
  }

  inline void AppendToPARAMS(const std::uint8_t* buffer_ptr, size count)
  {
    FCGI_PARAMS_.insert(FCGI_PARAMS_.end(), buffer_ptr, buffer_ptr + count);
  }

  inline bool get_STDIN_completion() const noexcept
  {
    return FCGI_STDIN_complete_;
  }

  inline void CompleteSTDIN() noexcept
  {
    FCGI_STDIN_complete_ = true;
  }

  inline void AppendToSTDIN(const std::uint8_t* buffer_ptr, size count)
  {
    FCGI_STDIN_.insert(FCGI_STDIN_.end(), buffer_ptr, buffer_ptr + count);
  }

  inline bool get_DATA_completion() const noexcept
  {
    return FCGI_DATA_complete_;
  }

  inline void CompleteDATA() noexcept
  {
    FCGI_DATA_complete_ = true;
  }

  inline void AppendToDATA(const std::uint8_t* buffer_ptr, size count)
  {
    FCGI_DATA_.insert(FCGI_DATA_.end(), buffer_ptr, buffer_ptr + count);
  }

  RequestData() = default;
  RequestData(std::uint16_t role, bool close_connection);
  
  // Move only.
  RequestData(RequestData&&) = default;
  RequestData& operator=(RequestData&&) = default;

  // No copy.
  RequestData(const RequestData&) = delete;
  RequestData& operator=(const RequestData&) = delete;
 
  ~RequestData() = default;

private:
  // Make FcgiRequest a friend class as its constructor will move data
  // from the representation of RequestData objects.
  friend class FcgiRequest;

  // Request data and completion status
  bool                      FCGI_PARAMS_complete_ {false};
  bool                      FCGI_STDIN_complete_  {false};
  bool                      FCGI_DATA_complete_   {false};
  std::vector<std::uint8_t> FCGI_PARAMS_          {};
  std::vector<std::uint8_t> FCGI_STDIN_           {};
  std::vector<std::uint8_t> FCGI_DATA_            {};

  // Map to hold processed FCGI_PARAMS_ data.
  std::map<std::vector<std::uint8_t>, std::vector<std::uint8_t>>
                environment_map_ {};

  // Request metadata
  std::uint16_t role_;
  bool          client_set_abort_               {false};
  bool          close_connection_;
  RequestStatus request_status_                 {RequestStatus::kRequestPending};
  bool          connection_closed_by_interface_ {false};
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_DATA_H_
