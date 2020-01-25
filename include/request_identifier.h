#ifndef FCGI_SERVER_INTERFACE_REQUEST_IDENTIFIER_H_
#define FCGI_SERVER_INTERFACE_REQUEST_IDENTIFIER_H_

#include <cstdint>

#include <utility>

namespace fcgi_si {

class RequestIdentifier {
public:
  int descriptor() const;
  uint16_t FCGI_id() const;

  RequestIdentifier();
  RequestIdentifier(int descriptor, uint16_t FCGI_id);
  RequestIdentifier(const RequestIdentifier& request_id) = default;
  RequestIdentifier(RequestIdentifier&& request_id) = default;

  RequestIdentifier& operator=(const RequestIdentifier& request_id) = default;
  RequestIdentifier& operator=(RequestIdentifier&& request_id) = default;

  bool operator==(const RequestIdentifier& rhs) const;
  bool operator!=(const RequestIdentifier& rhs) const;

  bool operator<(const RequestIdentifier& rhs) const;
  bool operator<=(const RequestIdentifier& rhs) const;
  bool operator>(const RequestIdentifier& rhs) const;
  bool operator>=(const RequestIdentifier& rhs) const;

  explicit operator bool() const;

  ~RequestIdentifier() = default;

private:
  std::pair<int, uint16_t> pair_;
};

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_REQUEST_IDENTIFIER_H_
