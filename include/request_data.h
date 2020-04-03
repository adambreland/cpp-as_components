#ifndef FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_DATA_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_DATA_H_

#include <cstdint>

#include <map>
#include <vector>

namespace fcgi_si {

enum class RequestStatus {kRequestPending, kRequestAssigned};

class RequestData {
public:
  // TODO Add a member function which is essentially a helper function
  // for FCGIRequest.

  using size = std::allocator_traits<std::allocator<uint8_t>>::size_type;

  inline void set_connection_closed_by_interface()
  {
    connection_closed_by_interface_ = true;
  }

  inline fcgi_si::RequestStatus get_status() const
  {
    return request_status_;
  }

  inline bool get_abort() const
  {
    return client_set_abort_;
  }

  inline void set_abort()
  {
    client_set_abort_ = true;
  }

  inline bool get_close_connection() const
  {
    return close_connection_;
  }

  inline uint16_t get_role() const
  {
    return role_;
  }

  inline bool IsRequestComplete() const
  {
    return FCGI_PARAMS_complete_ && FCGI_STDIN_complete_ && FCGI_DATA_complete_;
  }

  bool ProcessFCGI_PARAMS();

  inline bool get_PARAMS_completion() const
  {
    return FCGI_PARAMS_complete_;
  }

  inline void CompletePARAMS()
  {
    FCGI_PARAMS_complete_ = true;
  }

  inline void AppendToPARAMS(const uint8_t* buffer_ptr, size count)
  {
    FCGI_PARAMS_.insert(FCGI_PARAMS_.end(), buffer_ptr, buffer_ptr + count);
  }

  inline bool get_STDIN_completion() const
  {
    return FCGI_STDIN_complete_;
  }

  inline void CompleteSTDIN()
  {
    FCGI_STDIN_complete_ = true;
  }

  inline void AppendToSTDIN(const uint8_t* buffer_ptr, size count)
  {
    FCGI_STDIN_.insert(FCGI_STDIN_.end(), buffer_ptr, buffer_ptr + count);
  }

  inline bool get_DATA_completion() const
  {
    return FCGI_DATA_complete_;
  }

  inline void CompleteDATA()
  {
    FCGI_DATA_complete_ = true;
  }

  inline void AppendToDATA(const uint8_t* buffer_ptr, size count)
  {
    FCGI_DATA_.insert(FCGI_DATA_.end(), buffer_ptr, buffer_ptr + count);
  }

  // Move only.
  RequestData() = default;
  RequestData(uint16_t role, bool close_connection);
  RequestData(const RequestData&) = delete;
  RequestData(RequestData&&) = default;

  RequestData& operator=(const RequestData&) = delete;
  RequestData& operator=(RequestData&&) = default;

  ~RequestData() = default;

private:
  // Make FCGIRequest a friend class as its constructor will move data
  // from the representation of RequestData objects.
  friend class FCGIRequest;

  // Request data and completion status
  bool FCGI_PARAMS_complete_;
  bool FCGI_STDIN_complete_;
  bool FCGI_DATA_complete_;
  std::vector<uint8_t> FCGI_PARAMS_;
  std::vector<uint8_t> FCGI_STDIN_;
  std::vector<uint8_t> FCGI_DATA_;

  // Map to hold processed FCGI_PARAMS_ data.
  std::map<std::vector<uint8_t>, std::vector<uint8_t>>
    environment_map_;

  // Request metadata
  uint16_t role_;
  bool client_set_abort_;
  bool close_connection_;
  fcgi_si::RequestStatus request_status_;
  bool connection_closed_by_interface_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_DATA_H_
