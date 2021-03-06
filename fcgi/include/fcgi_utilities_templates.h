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

#ifndef AS_COMPONENTS_FCGI_INCLUDE_FCGI_UTILITIES_TEMPLATES_H_
#define AS_COMPONENTS_FCGI_INCLUDE_FCGI_UTILITIES_TEMPLATES_H_

#include "fcgi/include/fcgi_utilities.h"

#include <sys/select.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "fcgi/include/fcgi_protocol_constants.h"

namespace as_components {
namespace fcgi {

template<typename ByteIter>
void EncodeFourByteLength(std::int_fast32_t length, ByteIter byte_iter)
{
  // TODO Add template type property checking with static asserts.

  if(length < 128)
    throw std::invalid_argument {"An invalid length was given."};

  // Set the leading bit to 1 to indicate that a four-byte sequence is
  // present.
  *byte_iter = (static_cast<uint8_t>(length >> 24) | 0x80U);
  for(int i {0}; i < 3; i++)
  {
    ++byte_iter;
    *byte_iter = length >> (16 - (8*i));
  }
}

template <typename ByteIter>
std::int_fast32_t ExtractFourByteLength(ByteIter byte_iter) noexcept
{
  // TODO Add template type property checking with static_asserts.

  // Mask out the leading 1 bit which must be present per the FastCGI
  // name-value pair format. This bit does not encode length information.
  // It indicates that the byte sequence has four elements instead of one.
  std::int_fast32_t length 
    {static_cast<std::int_fast32_t>(static_cast<uint8_t>(*byte_iter) & 0x7fU)};
  // Perform three shifts by 8 bits to extract all four bytes.
  for(int i {0}; i < 3; ++i)
  {
    length <<= 8;
    ++byte_iter;
    length += static_cast<uint8_t>(*byte_iter);
  }
  return length;
}

template<typename ByteSeqPairIter>
std::tuple<bool, std::size_t, std::vector<iovec>, int,
  std::vector<std::uint8_t>, std::size_t, ByteSeqPairIter>
EncodeNameValuePairs(ByteSeqPairIter pair_iter, ByteSeqPairIter end,
  FcgiType type, std::uint16_t Fcgi_id, std::size_t offset)
{
  if(pair_iter == end)
    return {true, 0U, {}, 0, {}, 0U, end};

  constexpr std::size_t size_t_MAX {std::numeric_limits<std::size_t>::max()};
  constexpr ssize_t ssize_t_MAX {std::numeric_limits<ssize_t>::max()};
  constexpr uint16_t aligned_record_MAX {kMaxRecordContentByteLength - 7U};
  // Reduce by 7 to ensure that the length of a "full" record is a
  // multiple of 8.

  // Determine the initial values of the break variables.

  long remaining_iovec_count {sysconf(_SC_IOV_MAX)};
  // Use the current Linux default if information cannot be obtained.
  if(remaining_iovec_count == -1)
    remaining_iovec_count = 1024;
  remaining_iovec_count = std::min<long>(remaining_iovec_count, 
    std::numeric_limits<int>::max());
  // Reduce by one to ensure that a struct for padding is always available.
  remaining_iovec_count--;
 
  // Reduce by FCGI_HEADER_LEN - 1 = 7 to ensure that padding can always be
  // added.
  std::size_t remaining_byte_count {ssize_t_MAX - 7U};

  // Lambda functions to simplify the loops below.
  auto size_iter = [&](int i)->std::size_t
  {
    if(i == 0)
      return pair_iter->first.size();
    else
      return pair_iter->second.size();
  };

  auto data_iter = [&](int i)->const std::uint8_t*
  {
    if(i == 0)
      return static_cast<const std::uint8_t*>(static_cast<const void*>(
        pair_iter->first.data()));
    else
      return static_cast<const std::uint8_t*>(static_cast<const void*>(
        pair_iter->second.data()));
  };
  // A binary sequence of headers and length information encoded in the
  // FastCGI name-value pair format is created and returned to
  // the caller. A pair which holds an index into iovec_list and an index into
  // local_buffers is stored whenever a record
  // is referred to by an iovec_list element. This pair allows pointer values
  // to be determined once the memory allocated for local_buffers
  // will no longer change.
  std::vector<std::uint8_t> local_buffers {};
  std::vector<std::pair<std::vector<iovec>::size_type,
    std::vector<uint8_t>::size_type>> index_pairs {};
  // iovec_list will usually hold three instances of iovec for every
  // name-value pair. The first instance describes name and value length
  // information. It points to a range of bytes in local_buffers.
  // The second and third instances hold name and value information,
  // repsectively. They point to the buffers of containers from a source
  // std::pair object when such buffers exist.
  std::vector<iovec> iovec_list {};

  std::size_t                     number_to_write         {0U};
  std::size_t                     previous_content_length {0U};
  std::vector<uint8_t>::size_type previous_header_offset  {0U};
  std::size_t                     nv_pair_bytes_placed    {0U};
  // Record count may be int as the number of records is always less than the
  // number of struct iovec instances and this number is less than or equal to
  // the maximum int value.
  int                             record_count            {0}; 
  bool                            incomplete_nv_write     {false};
  bool                            name_or_value_too_big   {false};
  bool                            overflow_detected       {false};

  for(/*no-op*/; pair_iter != end; ++pair_iter)
  {
    if(!remaining_iovec_count || !remaining_byte_count)
      break;
    // Variables for name and value information.
    std::size_t size_array[3] = {};
    // sums starts at zero and holds partial sums of size_array.
    // It is used to check for potential numeric overflow.
    std::size_t sums[3] = {};
    std::vector<uint8_t>::size_type name_value_buffer_offset
      {local_buffers.size()};
    const std::uint8_t* data_array[2];
    // Reset for a new pair.
    nv_pair_bytes_placed = offset;
    // Collect information about the name and value byte sequences.
    for(int i {1}; i < 3; ++i)
    {
      size_array[i] = size_iter(i-1);
      if(size_array[i])
        data_array[i-1] = data_iter(i-1);
      else
        data_array[i-1] = nullptr;
      if(size_array[i] <= kNameValuePairSingleByteLength)
      {
        // A safe narrowing of size_array[i] from std::size_t to std::uint8_t.
        local_buffers.push_back(size_array[i]);
        size_array[0] += 1;
      }
      else if(size_array[i] <= kNameValuePairFourByteLength)
      {
        // A safe narrowing of size from std::size_t to a representation of
        // a subset of the range of uint32_t.
        EncodeFourByteLength(size_array[i],
          std::back_inserter(local_buffers));
        size_array[0] += 4;
      }
      else
      {
        name_or_value_too_big = true;
        break;
      }
    }
    sums[1] = size_array[0];
    // Check if processing must stop, including because of an overflow from
    // name and value lengths.
    if(name_or_value_too_big || size_array[1] > (size_t_MAX - sums[1])
       || (sums[2] = size_array[1] + sums[1],
           size_array[2] > (size_t_MAX - sums[2])))
    {
      if(size_array[0])
      {
        local_buffers.erase(local_buffers.end() - size_array[0],
          local_buffers.end());
      }
      if(!name_or_value_too_big)
        overflow_detected = true;
      break; // Stop iterating over pairs.
    }
    else // We can proceed normally to iteratively produce FastCGI records.
    {
      std::size_t total_length {size_array[2] + sums[2]};
      std::size_t remaining_nv_bytes_to_place {total_length
        - nv_pair_bytes_placed};

      auto determine_index = [&]()->std::size_t
      {
        int i {0};
        for(/*no-op*/; i < 2; i++)
          if(nv_pair_bytes_placed < sums[i+1])
            return i;
        return i;
      };

      bool padding_limit_reached {false};
      // Start loop which produces records.
      while(remaining_nv_bytes_to_place && !padding_limit_reached)
      {
        if(!previous_content_length) // Start a new record.
        {
          // Need enough iovec structs for a header, data, and padding.
          // Need enough bytes for a header and some data. An iovec struct
          // and FCGI_HEADER_LEN - 1 bytes were reserved.
          if((remaining_iovec_count >= 2) &&
             (remaining_byte_count >= (FCGI_HEADER_LEN + 1)))
          {
            previous_header_offset = local_buffers.size();
            index_pairs.push_back({iovec_list.size(), previous_header_offset});
            iovec_list.push_back({nullptr, FCGI_HEADER_LEN});
            local_buffers.insert(local_buffers.end(), FCGI_HEADER_LEN, 0);
            PopulateHeader(local_buffers.data() + previous_header_offset,
              type, Fcgi_id, 0, 0);
            number_to_write += FCGI_HEADER_LEN;
            remaining_byte_count -= FCGI_HEADER_LEN;
            remaining_iovec_count--;
            record_count++;
          }
          else
          {
            incomplete_nv_write = true; // As remaining_nv_bytes_to_place != 0.
            break;
          }
        }
        // Start loop over the three potential buffers.
        std::size_t index {determine_index()};
        for(/*no-op*/; index < 3; index++)
        {
          // Variables which determine how much we can write.
          std::size_t remaining_content_capacity
            {aligned_record_MAX - previous_content_length};
          std::size_t current_limit
            {std::min(remaining_byte_count, remaining_content_capacity)};
          std::size_t number_to_place
            {std::min(remaining_nv_bytes_to_place, current_limit)};
          // Determine how many we can write for a given buffer.
          std::size_t local_remaining
            {size_array[index] - (nv_pair_bytes_placed - sums[index])};
          std::size_t local_number_to_place
            {std::min(local_remaining, number_to_place)};
          // Write the determined amount.
          if(index == 0) // Special processing for name-value length info.
          {
            iovec_list.push_back({nullptr, local_number_to_place});
            // If we are in the name value length information byte sequence,
            // i.e. index == 0, then nv_pair_bytes_placed acts as an offset
            // into a subsequence of these bytes.
            index_pairs.push_back({iovec_list.size() - 1,
              name_value_buffer_offset + nv_pair_bytes_placed});
            remaining_iovec_count--;
          }
          else // Adding an iovec structure for name or value byte sequence.
          {
            // Either of size_array[1] or size_array[2] may be zero. For
            // example, we may add a iovec instance for size_array[0]
            // that specifies an empty name or value.
            if(local_number_to_place)
            {
              struct iovec new_iovec {
                const_cast<uint8_t*>(data_array[index-1])
                  + (size_array[index] - local_remaining),
                local_number_to_place
              };
              iovec_list.push_back(new_iovec);
              remaining_iovec_count--;
            }
          }
          // Update tracking variables.
          nv_pair_bytes_placed += local_number_to_place;
          remaining_nv_bytes_to_place -= local_number_to_place;
          number_to_write += local_number_to_place;
          remaining_byte_count -= local_number_to_place;
          // Update record information.
          previous_content_length += local_number_to_place;
          local_buffers[previous_header_offset
            + kHeaderContentLengthB1Index]
            = static_cast<uint8_t>(previous_content_length >> 8);
          local_buffers[previous_header_offset
            + kHeaderContentLengthB0Index]
            = static_cast<uint8_t>(previous_content_length);
          // Check if a limit was reached. Need at least an iovec struct for
          // padding. Need enough bytes for padding. These limits were
          // reserved in the initialization of remaining_iovec_count and
          // remaining_byte_count.
          if(!remaining_iovec_count || !remaining_byte_count)
          {
            padding_limit_reached = true;
            if(nv_pair_bytes_placed < total_length)
              incomplete_nv_write = true;
            break;
          }
          // Check if the record was finished.
          if(previous_content_length == aligned_record_MAX)
          {
            previous_content_length = 0;
            break; // Need to start a new record.
          }
        }
      }
    }
    offset = 0;
    if(incomplete_nv_write)
      break;
  }
  // Check if padding is needed.
  // (Safe narrowing.)
  uint8_t pad_mod(previous_content_length % FCGI_HEADER_LEN);
  if(pad_mod)
  {
    uint8_t pad_mod_complement(FCGI_HEADER_LEN - pad_mod);
      // Safe narrowing.
    index_pairs.push_back({iovec_list.size(), local_buffers.size()});
    iovec_list.push_back({nullptr, pad_mod_complement});
    local_buffers.insert(local_buffers.end(), pad_mod_complement, 0);
    local_buffers[previous_header_offset + kHeaderPaddingLengthIndex]
      = pad_mod_complement;
    number_to_write += pad_mod_complement;
  }
  // Since the memory of local_buffers is stable, fill in
  // iov_base pointers from local_buffers.data().
  for(auto pair : index_pairs)
    iovec_list[pair.first].iov_base =
      static_cast<void*>(local_buffers.data() + pair.second);
  // Check for rejection based on a limit or name or value that was too big.
  return std::make_tuple(
    !name_or_value_too_big && !overflow_detected,
    number_to_write,
    std::move(iovec_list),
    record_count,
    std::move(local_buffers),
    ((incomplete_nv_write) ? nv_pair_bytes_placed : 0),
    pair_iter
  );
}

namespace partition_byte_sequence_internal {
   // The content length of a record should be a multiple of 8 whenever possible.
  // kMaxRecordContentByteLength = 2^16 - 1
  // (2^16 - 1) - 7 = 2^16 - 8 = 2^16 - 2^3 = 2^3*(2^13 - 1) = 8*(2^13 - 1)
  constexpr uint16_t max_aligned_content_length
    {kMaxRecordContentByteLength - 7};
  // The maximum number of bytes that can be written in one call to writev.
  constexpr ssize_t ssize_t_MAX {std::numeric_limits<ssize_t>::max()};

  inline constexpr std::size_t CeilingOfQuotient(std::size_t numerator,
    std::size_t denominator) 
  {
    return ((numerator/denominator) +
            (static_cast<std::size_t>((numerator % denominator) > 0U)));
  }

  inline constexpr std::size_t InitializeMaxForSsize_t()
  {
    constexpr std::size_t macl {static_cast<std::size_t>(
                                  max_aligned_content_length)};
    constexpr std::size_t inter_1 {8U*(static_cast<std::size_t>(
                                         ssize_t_MAX)/8U)};
    constexpr std::size_t inter_2 {CeilingOfQuotient(inter_1, macl)};
    return (inter_1 - (8U*inter_2));
  }

  constexpr std::size_t max_for_ssize_t     {InitializeMaxForSsize_t()};
  const     std::size_t max_for_iovec       {InitializeMaxForIovec()};
  const     std::size_t min_max             {std::min<std::size_t>(
    partition_byte_sequence_internal::max_for_ssize_t,
    max_for_iovec
  )};
  const     ssize_t     working_ssize_t_max {NeededSsize_t(min_max)};
  const     std::size_t working_iovec_max   {NeededIovec(min_max)};
} // partition_byte_sequence_internal

template<typename ByteIter>
std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, std::size_t, 
  ByteIter>
PartitionByteSequence(ByteIter begin_iter, ByteIter end_iter, FcgiType type, 
  std::uint16_t Fcgi_id)
{
  // Verify that ByteIter iterates over units of data which are the size of
  // a byte. Note that this assertion disallows ByteIter being equal to void*.
  static_assert(sizeof(std::uint8_t) == sizeof(decltype(*begin_iter)),
    "A call to PartitionByteSequence<ByteIter> used an iterator type which did "
    "not iterate over data in units of bytes.");

  // Disallow unusually small ssize_t.
  static_assert(std::numeric_limits<ssize_t>::max() >=
                std::numeric_limits<std::uint16_t>::max());

                      // Begin processing input.

  // Determine the number of bytes of the input which will be processed.
  // Check that ptrdiff_t can to hold the byte length.
  static_assert(
    std::numeric_limits<decltype(std::distance(begin_iter, end_iter))>::max() <= 
    std::numeric_limits<std::ptrdiff_t>::max()
  );
  std::ptrdiff_t s_byte_length {std::distance(begin_iter, end_iter)};
  if(s_byte_length < 0)
  {
    throw std::invalid_argument {"end_iter was before begin_iter or an error "
      "occurred to a call to std::distance(begin_iter, end_iter) in "
      "a call to PartitionByteSequence."};
  }
  std::size_t byte_length   {static_cast<std::size_t>(s_byte_length)};
  ssize_t working_ssize_t
    {partition_byte_sequence_internal::working_ssize_t_max};
  std::size_t working_iovec
    {partition_byte_sequence_internal::working_iovec_max};
  std::size_t bytes_remaining {partition_byte_sequence_internal::min_max};
  if(byte_length < bytes_remaining)
  {
    bytes_remaining = byte_length;
    working_ssize_t =
      partition_byte_sequence_internal::NeededSsize_t(byte_length);
    working_iovec   =
      partition_byte_sequence_internal::NeededIovec(byte_length);
  }
  std::size_t local_length
    {partition_byte_sequence_internal::NeededLocalData(bytes_remaining)};

  // The first FCGI_HEADER_LEN (8) bytes are zero for padding.
  std::vector<uint8_t> noncontent_record_information(FCGI_HEADER_LEN, 0);
  noncontent_record_information.reserve(local_length);
  std::uint8_t* padding_ptr {noncontent_record_information.data()};
  std::vector<struct iovec> iovec_list {};
  iovec_list.reserve(working_iovec);
  ssize_t number_to_write {0};

  // Handle the special case of no content.
  if(begin_iter == end_iter)
  {
    std::vector<uint8_t>::iterator header_iter 
      {noncontent_record_information.insert(noncontent_record_information.end(),
        FCGI_HEADER_LEN, 0)};
    PopulateHeader(&(*header_iter), type, Fcgi_id, 0, 0);
    iovec_list.push_back({&(*header_iter), FCGI_HEADER_LEN});
    number_to_write += FCGI_HEADER_LEN;
  }
  else
  {
    // While records can be produced and need to be produced, produce a record
    // with the largest content length up to the contingent maximum.
    while(bytes_remaining)
    {
      uint16_t current_record_content_length {
        static_cast<std::uint16_t>(
          std::min<ssize_t>(
            static_cast<ssize_t>(bytes_remaining),
            static_cast<ssize_t>(
              partition_byte_sequence_internal::max_aligned_content_length)
          )
        )
      };
      // Check if we need padding.
      uint8_t padding_length {0U};
      if(uint8_t padding_length_complement = current_record_content_length % 8)
      {
        padding_length = (padding_length_complement) ?
          (8 - padding_length_complement) : 0U;
      }
      // Update non-content information.
      std::vector<uint8_t>::iterator header_iter
        {noncontent_record_information.insert(
          noncontent_record_information.end(),
          FCGI_HEADER_LEN, 0)};
      std::uint8_t* local_ptr {&(*header_iter)};
      PopulateHeader(local_ptr, type, Fcgi_id,
        current_record_content_length, padding_length);
      // Update iovec with header.
      iovec_list.push_back({local_ptr, FCGI_HEADER_LEN});
      // The const_cast is necessary as struct iovec contains a void* member
      // and a client may pass in a const_iterator.
      iovec_list.push_back({
        const_cast<void*>(static_cast<const void*>(&(*begin_iter))),
        current_record_content_length
      });
      // Update iovec with padding if needed. Update relocation information.
      if(padding_length)
      {
        iovec_list.push_back({padding_ptr, padding_length});
      }
      // Update tracking variables and increment iterator.
      number_to_write += (FCGI_HEADER_LEN + current_record_content_length
        + padding_length);
      bytes_remaining -= current_record_content_length;
      std::advance(begin_iter, current_record_content_length);
    }
    // Check if an error was made in the vector length calculations.
    if((number_to_write > working_ssize_t) ||
       (iovec_list.size() > working_iovec) ||
       (noncontent_record_information.size() > local_length))
    {
      throw std::logic_error {"An error in the estimation of internal vector "
        "lengths occurred in a call to PartitionByteSequence."};
    }
  }
  return std::make_tuple(std::move(noncontent_record_information), 
    std::move(iovec_list), number_to_write, begin_iter);
}

} // namespace fcgi
} // namespace as_components

#endif // AS_COMPONENTS_FCGI_INCLUDE_FCGI_UTILITIES_TEMPLATES_H_
