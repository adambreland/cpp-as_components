// Template definitions for FcgiRequest. This file is included as
// a template definition header by the FcgiRequest header.

#ifndef A_COMPONENT_FCGI_INCLUDE_FCGI_REQUEST_TEMPLATES_H_
#define A_COMPONENT_FCGI_INCLUDE_FCGI_REQUEST_TEMPLATES_H_

#include "include/fcgi_request.h"

#include <sys/types.h>     // For ssize_t.
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_utilities.h"

namespace a_component {
namespace fcgi {

template<typename ByteIter>
bool FcgiRequest::WriteHelper(ByteIter begin_iter, ByteIter end_iter,
  FcgiType type)
{
  if(completed_ || associated_interface_id_ == 0U)
    return false;

  bool write_success {true};
  while(write_success && (begin_iter != end_iter))
  {
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
      std::size_t, ByteIter> 
    partition_return {PartitionByteSequence(begin_iter, end_iter, 
      type, request_identifier_.Fcgi_id())};

    write_success = ScatterGatherWriteHelper(
      std::get<1>(partition_return).data(),
      std::get<1>(partition_return).size(), 
      std::get<2>(partition_return),
      false
    );
    begin_iter = std::get<3>(partition_return);
  }
  return write_success;
}

template<typename ByteIter>
bool FcgiRequest::Write(ByteIter begin_iter, ByteIter end_iter)
{
  return WriteHelper(begin_iter, end_iter, FcgiType::kFCGI_STDOUT);
}

template<typename ByteIter>
bool FcgiRequest::WriteError(ByteIter begin_iter, ByteIter end_iter)
{
  return WriteHelper(begin_iter, end_iter, FcgiType::kFCGI_STDERR);
}

} // namespace fcgi
} // namespace a_component

#endif // A_COMPONENT_FCGI_INCLUDE_FCGI_REQUEST_TEMPLATES_H_
