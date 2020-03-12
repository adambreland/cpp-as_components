#ifndef FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_IDENTIFIER_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_IDENTIFIER_H_

#include <cstdint>

#include <utility>

namespace fcgi_si {

class RequestIdentifier {
public:
  int descriptor() const noexcept;
  uint16_t FCGI_id() const noexcept;

  RequestIdentifier() noexcept;
  RequestIdentifier(int descriptor, uint16_t FCGI_id) noexcept;
  RequestIdentifier(const RequestIdentifier& request_id) noexcept;
  RequestIdentifier(RequestIdentifier&& request_id) noexcept;

  RequestIdentifier& operator=(const RequestIdentifier& request_id) noexcept;
  RequestIdentifier& operator=(RequestIdentifier&& request_id) noexcept;

  bool operator== (const RequestIdentifier& rhs) const noexcept;
  bool operator!= (const RequestIdentifier& rhs) const noexcept;

  bool operator<  (const RequestIdentifier& rhs) const noexcept;
  bool operator<= (const RequestIdentifier& rhs) const noexcept;
  bool operator>  (const RequestIdentifier& rhs) const noexcept;
  bool operator>= (const RequestIdentifier& rhs) const noexcept;

  explicit operator bool() const noexcept;

  ~RequestIdentifier() = default;

private:
  std::pair<int, uint16_t> pair_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_IDENTIFIER_H_
