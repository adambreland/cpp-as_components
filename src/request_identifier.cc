#include <stdexcept>

#include "include/data_types.h"

fcgi_synchronous_interface::RequestIdentifier::
RequestIdentifier(int descriptor, int FCGI_id)
: pair_ {descriptor, FCGI_id}
{
  if((pair_.first < 0) || (pair_.second < 0))
    throw std::invalid_argument
    {"A value less than zero was encountered when constructing"
     "a RequestIdentifier."
     + '\n' + __FILE__ + '\n'
     + "Line: " + std::to_string(__LINE__) + '\n'};
}

fcgi_synchronous_interface::RequestIdentifier::operator bool()
{
  return (pair_.first || pair_.second) ? true : false;
}

bool fcgi_synchronous_interface::RequestIdentifier::
operator<(const RequestIdentifier& lhs, const RequestIdentifier& rhs)
{
  // Lexical ordering on Connections X RequestIDs.
  return std::less(lhs.pair_, rhs.pair_);
}

int fcgi_synchronous_interface::RequestIdentifier::descriptor()
{
  return pair_.first;
}

int fcgi_synchronous_interface::RequestIdentifier::FCGI_id()
{
  return pair_.second;
}
