#ifndef FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_IDENTIFIER_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_IDENTIFIER_H_

#include <cstdint>

#include <utility>

namespace a_component {
namespace fcgi {

// A value class for identifying a FastCGI request by the connected socket
// descriptor over which it was sent and its FastCGI request identifier.
// * The class is ordered.
// * An explicit conversion to boolean type converts the value (0, 0) to false
//   and all other values to true.
// * Mutation is only allowed through assignment.
class RequestIdentifier {
public:
  inline int descriptor() const noexcept
  {
    return pair_.first;
  }

  inline std::uint16_t Fcgi_id() const noexcept
  {
    return pair_.second;
  }

  // Default-constructed RequestIdentifier objects are interpreted as null
  // objects and are false when converted to boolean type. Any object which
  // does not compare equal to a default-constructed object converts to true.
  inline RequestIdentifier() noexcept
  : pair_ {0, 0U}
  {}

  inline RequestIdentifier(int descriptor, uint16_t Fcgi_id) noexcept
  : pair_ {descriptor, Fcgi_id}
  {}

  inline RequestIdentifier(const RequestIdentifier& request_id) noexcept
  {
    pair_ = request_id.pair_;
  }

  inline RequestIdentifier(RequestIdentifier&& request_id) noexcept
  {
    pair_ = std::move(request_id.pair_);
  }

  inline RequestIdentifier& operator=(const RequestIdentifier& request_id) 
    noexcept
  {
    pair_ = request_id.pair_;
    return *this;
  }

  inline RequestIdentifier& operator=(RequestIdentifier&& request_id) noexcept
  {
    pair_ = std::move(request_id.pair_);
    return *this;
  }

  inline bool operator==(const RequestIdentifier& rhs) const noexcept
  {
    return pair_ == rhs.pair_;
  }

  inline bool operator!=(const RequestIdentifier& rhs) const noexcept
  {
    return pair_ != rhs.pair_;
  }

  inline bool operator<(const RequestIdentifier& rhs) const noexcept
  {
    return pair_ < rhs.pair_;
  }

  inline bool operator<=(const RequestIdentifier& rhs) const noexcept
  {
    return pair_ <= rhs.pair_;
  }

  inline bool operator>(const RequestIdentifier& rhs) const noexcept
  {
    return pair_ > rhs.pair_;
  }

  inline bool operator>=(const RequestIdentifier& rhs) const noexcept
  {
    return pair_ >= rhs.pair_;
  }

  inline explicit operator bool() const noexcept
  {
    return (pair_.first || pair_.second) ? true : false;
  }

  ~RequestIdentifier() = default;

private:
  std::pair<int, std::uint16_t> pair_;
};

} // namespace fcgi
} // namespace a_component

#endif // FCGI_SERVER_INTERFACE_INCLUDE_REQUEST_IDENTIFIER_H_
