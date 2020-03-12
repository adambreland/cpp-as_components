#include <cstdint>

#include <stdexcept>
#include <utility>

#include "external/error_handling/include/error_handling.h"

#include "include/protocol_constants.h"
#include "include/request_identifier.h"

fcgi_si::RequestIdentifier::RequestIdentifier() noexcept
: pair_ {0, 0}
{}

fcgi_si::RequestIdentifier::
RequestIdentifier(int descriptor, uint16_t FCGI_id) noexcept
: pair_ {descriptor, FCGI_id}
{}

fcgi_si::RequestIdentifier::
RequestIdentifier(const RequestIdentifier& request_id) noexcept
{
  pair_ = request_id.pair_;
}

fcgi_si::RequestIdentifier::
RequestIdentifier(RequestIdentifier&& request_id) noexcept
{
  pair_ = request_id.pair_;
}

fcgi_si::RequestIdentifier& fcgi_si::RequestIdentifier::
operator=(const RequestIdentifier& request_id) noexcept
{
  pair_ = request_id.pair_;
  return *this;
}

fcgi_si::RequestIdentifier& fcgi_si::RequestIdentifier::
operator=(RequestIdentifier&& request_id) noexcept
{
  pair_ = request_id.pair_;
  return *this;
}

int fcgi_si::RequestIdentifier::descriptor() const noexcept
{
  return pair_.first;
}

uint16_t fcgi_si::RequestIdentifier::FCGI_id() const noexcept
{
  return pair_.second;
}

bool fcgi_si::RequestIdentifier::
operator==(const RequestIdentifier& rhs) const noexcept
{
  return pair_ == rhs.pair_;
}

bool fcgi_si::RequestIdentifier::
operator!=(const RequestIdentifier& rhs) const noexcept
{
  return pair_ != rhs.pair_;
}

bool fcgi_si::RequestIdentifier::
operator<(const RequestIdentifier& rhs) const noexcept
{
  // Lexical ordering on Connections X RequestIDs.
  return (pair_ < rhs.pair_);
}

bool fcgi_si::RequestIdentifier::
operator<=(const RequestIdentifier& rhs) const noexcept
{
  return (pair_ <= rhs.pair_);
}

bool fcgi_si::RequestIdentifier::
operator>(const RequestIdentifier& rhs) const noexcept
{
  return (pair_ > rhs.pair_);
}

bool fcgi_si::RequestIdentifier::
operator>=(const RequestIdentifier& rhs) const noexcept
{
  return (pair_ >= rhs.pair_);
}

fcgi_si::RequestIdentifier::operator bool() const noexcept
{
  return (pair_.first || pair_.second) ? true : false;
}
