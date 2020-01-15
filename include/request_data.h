#ifndef FCGI_SERVER_INTERFACE_REQUEST_DATA_H_
#define FCGI_SERVER_INTERFACE_REQUEST_DATA_H_

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

  bool get_abort() const;
  void set_abort();

  bool get_close_connection() const;
  uint16_t get_role() const;

  bool IsRequestComplete() const;

  fcgi_si::RequestStatus get_status() const;

  bool ProcessFCGI_PARAMS();

  bool get_PARAMS_completion() const;
  void CompletePARAMS();
  void AppendToPARAMS(const uint8_t* buffer_ptr, size count);

  bool get_STDIN_completion() const;
  void CompleteSTDIN();
  void AppendToSTDIN(const uint8_t* buffer_ptr, size count);

  bool get_DATA_completion() const;
  void CompleteDATA();
  void AppendToDATA(const uint8_t* buffer_ptr, size count);

  // No copy or move.
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
  bool abort_;
  bool close_connection_;
  RequestStatus request_status_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_REQUEST_DATA_H_
