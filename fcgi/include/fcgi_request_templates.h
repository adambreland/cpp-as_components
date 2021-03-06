// MIT License
//
// Copyright (c) 2021 Adam J. Breland
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Template definitions for FcgiRequest. This file is included as
// a template definition header by the FcgiRequest header.

#ifndef AS_COMPONENTS_FCGI_INCLUDE_FCGI_REQUEST_TEMPLATES_H_
#define AS_COMPONENTS_FCGI_INCLUDE_FCGI_REQUEST_TEMPLATES_H_

#include "fcgi/include/fcgi_request.h"

#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include "fcgi/include/fcgi_protocol_constants.h"
#include "fcgi/include/fcgi_utilities.h"

namespace as_components {
namespace fcgi {

template<typename ByteIter>
bool FcgiRequest::WriteHelper(ByteIter begin_iter, ByteIter end_iter,
  FcgiType type)
{
  if(completed_ || (associated_interface_id_ == 0U))
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
} // namespace as_components

#endif // AS_COMPONENTS_FCGI_INCLUDE_FCGI_REQUEST_TEMPLATES_H_
