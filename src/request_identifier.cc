#include <cstdint>

#include <stdexcept>
#include <utility>

#include "external/error_handling/include/error_handling.h"

#include "include/protocol_constants.h"
#include "include/request_identifier.h"

namespace fcgi_si {

RequestIdentifier::RequestIdentifier() noexcept
: pair_ {0, 0}
{}

RequestIdentifier::
RequestIdentifier(int descriptor, uint16_t FCGI_id) noexcept
: pair_ {descriptor, FCGI_id}
{}

RequestIdentifier::
RequestIdentifier(const RequestIdentifier& request_id) noexcept
{
  pair_ = request_id.pair_;
}

RequestIdentifier::
RequestIdentifier(RequestIdentifier&& request_id) noexcept
{
  pair_ = request_id.pair_;
}

RequestIdentifier& RequestIdentifier::
operator=(const RequestIdentifier& request_id) noexcept
{
  pair_ = request_id.pair_;
  return *this;
}

RequestIdentifier& RequestIdentifier::
operator=(RequestIdentifier&& request_id) noexcept
{
  pair_ = request_id.pair_;
  return *this;
}

int RequestIdentifier::descriptor() const noexcept
{
  return pair_.first;
}

uint16_t RequestIdentifier::FCGI_id() const noexcept
{
  return pair_.second;
}

bool RequestIdentifier::
operator==(const RequestIdentifier& rhs) const noexcept
{
  return pair_ == rhs.pair_;
}

bool RequestIdentifier::
operator!=(const RequestIdentifier& rhs) const noexcept
{
  return pair_ != rhs.pair_;
}

bool RequestIdentifier::
operator<(const RequestIdentifier& rhs) const noexcept
{
  // Lexical ordering on Connections X RequestIDs.
  return (pair_ < rhs.pair_);
}

bool RequestIdentifier::
operator<=(const RequestIdentifier& rhs) const noexcept
{
  return (pair_ <= rhs.pair_);
}

bool RequestIdentifier::
operator>(const RequestIdentifier& rhs) const noexcept
{
  return (pair_ > rhs.pair_);
}

bool RequestIdentifier::
operator>=(const RequestIdentifier& rhs) const noexcept
{
  return (pair_ >= rhs.pair_);
}

RequestIdentifier::operator bool() const noexcept
{
  return (pair_.first || pair_.second) ? true : false;
}

} // namespace fcgi_si
