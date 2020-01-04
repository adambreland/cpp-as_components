#include <stdexcept>

#include "external/error_handling/include/error_handling.h"

#include "include/data_types.h"

fcgi_synchronous_interface::RequestIdentifier::
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

fcgi_synchronous_interface::RequestIdentifier::operator bool()
{
  return (pair_.first || pair_.second) ? true : false;
}

bool fcgi_synchronous_interface::RequestIdentifier::
operator<(const RequestIdentifier& rhs)
{
  // Lexical ordering on Connections X RequestIDs.
  return (pair_ < rhs.pair_);
}

int fcgi_synchronous_interface::RequestIdentifier::descriptor()
{
  return pair_.first;
}

uint16_t fcgi_synchronous_interface::RequestIdentifier::FCGI_id()
{
  return pair_.second;
}
