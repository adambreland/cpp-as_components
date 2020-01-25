#include <cstdint>

#include <stdexcept>
#include <utility>

#include "external/error_handling/include/error_handling.h"

#include "include/protocol_constants.h"
#include "include/request_identifier.h"

fcgi_si::RequestIdentifier::RequestIdentifier()
: pair_ {0, 0}
{}

fcgi_si::RequestIdentifier::
RequestIdentifier(int descriptor, uint16_t FCGI_id)
: pair_ {descriptor, FCGI_id}
{
  if((pair_.first < 0) || (pair_.second < 0))
  {
    std::string message
      {"A value less than zero was encountered when constructing a RequestIdentifier."};
    throw std::invalid_argument {ERRNO_ERROR_STRING(message)};
  }
}

int fcgi_si::RequestIdentifier::descriptor() const
{
  return pair_.first;
}

uint16_t fcgi_si::RequestIdentifier::FCGI_id() const
{
  return pair_.second;
}

bool fcgi_si::RequestIdentifier::operator==(const RequestIdentifier& rhs) const
{
  return pair_ == rhs.pair_;
}

bool fcgi_si::RequestIdentifier::operator!=(const RequestIdentifier& rhs) const
{
  return pair_ != rhs.pair_;
}

bool fcgi_si::RequestIdentifier::operator<(const RequestIdentifier& rhs) const
{
  // Lexical ordering on Connections X RequestIDs.
  return (pair_ < rhs.pair_);
}

bool fcgi_si::RequestIdentifier::operator<=(const RequestIdentifier& rhs) const
{
  return (pair_ <= rhs.pair_);
}

bool fcgi_si::RequestIdentifier::operator>(const RequestIdentifier& rhs) const
{
  return (pair_ > rhs.pair_);
}

bool fcgi_si::RequestIdentifier::operator>=(const RequestIdentifier& rhs) const
{
  return (pair_ >= rhs.pair_);
}

fcgi_si::RequestIdentifier::operator bool() const
{
  return (pair_.first || pair_.second) ? true : false;
}
