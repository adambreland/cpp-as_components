#include <stdexcept>

#include "external/error_handling/include/error_handling.h"

#include "include/data_types.h"

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

fcgi_si::RequestIdentifier::operator bool()
{
  return (pair_.first || pair_.second) ? true : false;
}

bool fcgi_si::RequestIdentifier::
operator<(const RequestIdentifier& rhs)
{
  // Lexical ordering on Connections X RequestIDs.
  return (pair_ < rhs.pair_);
}

int fcgi_si::RequestIdentifier::descriptor() const
{
  return pair_.first;
}

uint16_t fcgi_si::RequestIdentifier::FCGI_id() const
{
  return pair_.second;
}
