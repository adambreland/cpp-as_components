#ifndef FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_TEMPLATES_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_TEMPLATES_H_

#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "include/protocol_constants.h"

namespace fcgi_si {

template<typename ByteIter>
void EncodeFourByteLength(std::int_fast32_t length, ByteIter byte_iter)
{
  // TODO Add template type property checking with static asserts.

  if(length < 128)
    throw std::invalid_argument {"A negative length was given."};

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
std::tuple<bool, std::size_t, std::vector<iovec>, 
  std::vector<std::uint8_t>, std::size_t, ByteSeqPairIter>
EncodeNameValuePairs(ByteSeqPairIter pair_iter, ByteSeqPairIter end,
  FCGIType type, std::uint16_t FCGI_id, std::size_t offset)
{
  if(pair_iter == end)
    return {true, 0U, {}, {}, 0U, end};

  const std::size_t size_t_MAX {std::numeric_limits<std::size_t>::max()};
  const ssize_t ssize_t_MAX {std::numeric_limits<ssize_t>::max()};
  const uint16_t aligned_record_MAX {kMaxRecordContentByteLength - 7U};
  // Reduce by 7 to ensure that the length of a "full" record is a
  // multiple of 8.

  // Determine the initial values of the break variables.

  long remaining_iovec_count {iovec_MAX};
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

  std::size_t number_to_write {0};
  std::size_t previous_content_length {0};
  std::vector<uint8_t>::size_type previous_header_offset {0};
  std::size_t nv_pair_bytes_placed {0};
  bool incomplete_nv_write {false};
  bool name_or_value_too_big {false};
  bool overflow_detected {false};

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
              type, FCGI_id, 0, 0);
            number_to_write += FCGI_HEADER_LEN;
            remaining_byte_count -= FCGI_HEADER_LEN;
            remaining_iovec_count--;
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
            + fcgi_si::kHeaderContentLengthB1Index]
            = static_cast<uint8_t>(previous_content_length >> 8);
          local_buffers[previous_header_offset
            + fcgi_si::kHeaderContentLengthB0Index]
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
  uint8_t pad_mod(previous_content_length % fcgi_si::FCGI_HEADER_LEN);
  if(pad_mod)
  {
    uint8_t pad_mod_complement(fcgi_si::FCGI_HEADER_LEN - pad_mod);
      // Safe narrowing.
    index_pairs.push_back({iovec_list.size(), local_buffers.size()});
    iovec_list.push_back({nullptr, pad_mod_complement});
    local_buffers.insert(local_buffers.end(), pad_mod_complement, 0);
    local_buffers[previous_header_offset + fcgi_si::kHeaderPaddingLengthIndex]
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
    std::move(local_buffers),
    ((incomplete_nv_write) ? nv_pair_bytes_placed : 0),
    pair_iter
  );
}

template<typename ByteIter>
std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, std::size_t, 
  ByteIter>
PartitionByteSequence(ByteIter begin_iter, ByteIter end_iter, FCGIType type, 
  std::uint16_t FCGI_id)
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
  remaining_iovec = std::min<long>(remaining_iovec, 
    std::numeric_limits<int>::max());
  ssize_t remaining_content_length {std::distance(begin_iter, end_iter)};
  ssize_t remaining_ssize_t {ssize_t_MAX};

  // The first FCGI_HEADER_LEN (8) bytes are zero for padding.
  std::vector<uint8_t> noncontent_record_information(FCGI_HEADER_LEN, 0);
  std::vector<struct iovec> iovec_list {};
  ssize_t number_to_write {0};

  // Handle the special case of no content.
  if(begin_iter == end_iter)
  {
    std::vector<uint8_t>::iterator header_iter 
      {noncontent_record_information.insert(noncontent_record_information.end(),
        FCGI_HEADER_LEN, 0)};
    PopulateHeader(&(*header_iter), type, FCGI_id, 0, 0);
    iovec_list.push_back({&(*header_iter), FCGI_HEADER_LEN});
    number_to_write += FCGI_HEADER_LEN;
  }

  // While records can be produced and need to be produced, produce a record
  // with the largest content length up to the contingent maximum.
  while(true)
  {
    // Check if any content can be written in a new record.
    if((remaining_content_length == 0) || 
       (remaining_ssize_t < 2*FCGI_HEADER_LEN) || (remaining_iovec < 2))
      break;
    // Check if unaligned content must be written so that a padding section is
    // necessary and if this can be done.
    if((remaining_content_length < 8) && (remaining_iovec == 2))
      break;

    uint16_t current_record_content_length {
      std::min<ssize_t>({
        remaining_ssize_t - FCGI_HEADER_LEN,
        remaining_content_length,
        max_aligned_content_length
      })
    };
    uint8_t padding_length {0U};
      
    // Check if we must produce a record with aligned content.
    if(remaining_iovec == 2)
    {
      current_record_content_length -= current_record_content_length % 8;
    }
    // Check if we need padding.
    if(uint8_t padding_length_complement = current_record_content_length % 8)
    {
      padding_length = (padding_length_complement) ?
        8 - padding_length_complement : 0U;
    }
    // Update non-content information.
    std::vector<uint8_t>::iterator header_iter 
      {noncontent_record_information.insert(noncontent_record_information.end(),
        FCGI_HEADER_LEN, 0)};
    PopulateHeader(&(*header_iter), type, FCGI_id,
      current_record_content_length, padding_length);
      
    // Update iovec information.
    iovec_list.push_back({static_cast<void*>(&(*header_iter)),
      FCGI_HEADER_LEN});
      // The const_cast is necessary as struct iovec contains a void* member,
      // but a client may pass in a const_iterator.
    iovec_list.push_back({
      const_cast<void*>(static_cast<const void*>(&(*begin_iter))), 
      current_record_content_length
    });
    if(padding_length)
      iovec_list.push_back({static_cast<void*>(
        &(*noncontent_record_information.begin())), padding_length});
    
    ssize_t total_record_bytes {FCGI_HEADER_LEN + current_record_content_length 
      + padding_length};
    // Update tracking variables.
    remaining_ssize_t -= total_record_bytes;
    number_to_write += total_record_bytes;
    remaining_iovec -= 2 + (padding_length > 0);
    remaining_content_length -= current_record_content_length;
    std::advance(begin_iter, current_record_content_length);
  }
  return std::make_tuple(std::move(noncontent_record_information), 
    std::move(iovec_list), number_to_write, begin_iter);
}

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_TEMPLATES_H_
