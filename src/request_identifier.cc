#include <stdexcept>

#include "include/data_types.h"

fcgi_synchronous_interface::RequestIdentifier::
RequestIdentifier(int descriptor, uint16_t FCGI_id)
: pair_ {descriptor, FCGI_id}
{
  if((pair_.first < 0) || (pair_.second < 0))
  {
    std::string error_message {"A value less than zero was encountered when constructing a RequestIdentifier.\n"};
    error_message += __FILE__;
    error_message += "\nLine :";
    error_message += std::to_string(__LINE__);
    error_message += '\n';

    throw std::invalid_argument {error_message};
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
