// Template definitions for fcgi_si::FCGIRequest. This file is included as
// a template definition header by the FCGIRequest header.

#ifndef FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_TEMPLATES_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_TEMPLATES_H_

#include <sys/types.h>     // For ssize_t.
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "include/fcgi_request.h"
#include "include/protocol_constants.h"
#include "include/utility.h"

template<typename ByteIter>
bool fcgi_si::FCGIRequest::WriteHelper(ByteIter begin_iter, ByteIter end_iter,
  FCGIType type)
{
  if(completed_)
    return false;

  bool write_success {true};
  while(write_success && (begin_iter != end_iter))
  {
    std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
      std::size_t, ByteIter> 
    partition_return {PartitionByteSequence(begin_iter, end_iter, 
      type, request_identifier_.FCGI_id())};

    write_success = ScatterGatherWriteHelper(
      std::get<1>(partition_return).data(),
      std::get<1>(partition_return).size(), 
      std::get<2>(partition_return), false);
    begin_iter = std::get<3>(partition_return);
  }
  return write_success;
}

template<typename ByteIter>
bool fcgi_si::FCGIRequest::Write(ByteIter begin_iter, ByteIter end_iter)
{
  return WriteHelper(begin_iter, end_iter, FCGIType::kFCGI_STDOUT);
}

template<typename ByteIter>
bool fcgi_si::FCGIRequest::WriteError(ByteIter begin_iter, ByteIter end_iter)
{
  return WriteHelper(begin_iter, end_iter, FCGIType::kFCGI_STDERR);
}

#endif // FCGI_SERVER_INTERFACE_INCLUDE_FCGI_REQUEST_TEMPLATES_H_