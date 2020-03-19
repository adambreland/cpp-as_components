// Template definitions for fcgi_si::FCGIRequest. This file is included as
// a definition header by the FCGIRequest header.

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
std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, std::size_t, 
  ByteIter>
fcgi_si::FCGIRequest::PartitionByteSequence(ByteIter begin_iter, ByteIter end_iter,
  FCGIType type)
{
  // Verify that ByteIter iterates over units of data which are the size of
  // a byte.
  static_assert(sizeof(std::uint8_t) == sizeof(decltype(*begin_iter)),
    "A call to PartitionByteSequence<> used an iterator type which did "
    "not iterate over data in units of bytes.");

  // The content length of a record should be a multiple of 8 whenever possible.
  // kMaxRecordContentByteLength = 2^16 - 1
  // (2^16 - 1) - 7 = 2^16 - 8 = 2^16 - 2^3 = 2^3*(2^13 - 1) = 8*(2^13 - 1)
  constexpr uint16_t max_aligned_content_length
    {kMaxRecordContentByteLength - 7};

  // The maximum number of bytes that can be written in one call to writev.
  constexpr ssize_t ssize_t_MAX {std::numeric_limits<ssize_t>::max()};

  long remaining_iovec {fcgi_si::iovec_MAX};
  // Use the current Linux default if information cannot be obtained.
  if(remaining_iovec == -1)
      remaining_iovec = 1024;
  remaining_iovec = std::min<long>(iovec_MAX, std::numeric_limits<int>::max());

  std::ptrdiff_t remaining_content_length {&(*end_iter) - &(*end_iter)};
  ssize_t remaining_ssize_t {ssize_t_MAX};
  
  uint16_t current_record_content_length {};

  // The first FCGI_HEADER_LEN (8) bytes are zero for padding.
  std::vector<uint8_t> noncontent_record_information(FCGI_HEADER_LEN, 0);
  std::vector<struct iovec> iovec_list {};
  ssize_t number_to_write {0};

  while(true)
  {
    // Check if any content can be written in a new record.
    if(remaining_content_length == 0 || remaining_ssize_t < 2*FCGI_HEADER_LEN ||
        remaining_iovec < 2)
      break;
    // Check if unaligned content must be written so that a padding section is
    // necessary and if this can be done.
    if(remaining_content_length < 8 && remaining_iovec == 2)
      break;

    uint16_t current_record_content_length {std::min<ssize_t>(
      remaining_ssize_t - FCGI_HEADER_LEN, remaining_content_length, 
        max_aligned_content_length)};
    uint8_t padding_length {0};
      
    // Check if we must produce a record with aligned content.
    if(remaining_iovec == 2)
    {
      current_record_content_length = 8*(current_record_content_length / 8);
    }
    // Check if we need padding.
    if(uint8_t padding_length_complement = current_record_content_length % 8)
    {
      padding_length = 8 - padding_length_complement;
    }
    // Update non-content information.
    std::vector<uint8_t>::iterator header_iter 
      {noncontent_record_information.insert(noncontent_record_information.end(),
        FCGI_HEADER_LEN, 0)};
    PopulateHeader(&(*header_iter), type, request_identifier_.FCGI_id(),
      current_record_content_length, padding_length);
    // Update iovec information.
    iovec_list.push_back({static_cast<void*>(&(*header_iter)), FCGI_HEADER_LEN});
    iovec_list.push_back({static_cast<void*>(&(*begin_iter)), 
      current_record_content_length});
    if(padding_length)
      iovec_list.push_back({static_cast<void*>(
        &(*noncontent_record_information.begin())), padding_length});
    
    ssize_t total_record_bytes {FCGI_HEADER_LEN + current_record_content_length + 
      padding_length};
    // Update tracking variables.
    remaining_ssize_t -= total_record_bytes;
    number_to_write += total_record_bytes;
    remaining_iovec -= 2 + (padding_length > 0);
    remaining_content_length -= current_record_content_length;
    begin_iter.advance(current_record_content_length);
  }
  return std::make_tuple(std::move(noncontent_record_information), 
    std::move(iovec_list), number_to_write, begin_iter);
}

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
      type)};

    write_success = ScatterGatherWriteHelper(std::get<1>(partition_return).data(),
      std::get<1>(partition_return).size(), std::get<2>(partition_return),
        false);
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