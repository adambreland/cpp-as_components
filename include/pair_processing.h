#ifndef FCGI_SERVER_INTERFACE_INCLUDE_PAIR_PROCESSING_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_PAIR_PROCESSING_H_

#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "include/protocol_constants.h"
#include "include/utility.h"

namespace fcgi_si {

// Encodes length in the FastCGI name-value pair format and stores the
// output sequence of four bytes in the byte buffer pointed to by byte_iter.
//
// Parameters:
// length:    The length to be encoded per the FastCGI name-value pair
//            format.
// byte_iter: An iterator to a byte-buffer which will hold the four-byte
//            sequence which encodes length.
//
// Requires:
// 1) length requires four bytes when encoded in the FastCGI name-value pair
//    format and is less than or equal to the maximum value able to be
//    encoded. Hence, length is in [2^7; 2^31 - 1] = [128; 2,147,483,647].
// 2) byte_iter has the following properties:
//    a) The size of the type of the object pointed to by byte_iter is equal
//       to sizeof(uint8_t).
//    b) *byte_iter can be assigned a value from uint8_t without narrowing.
//    b) Four uint8_t values can be written to the buffer pointed to by
//       byte_iter.
//
// Effects:
// 1) Four bytes are written to the buffer pointed to by byte_iter. The
//    byte sequence encodes length in the FastCGI name-value pair format.
template<typename ByteIter>
void EncodeFourByteLength(uint32_t length, ByteIter byte_iter)
{
  // TODO Add template type property checking with static asserts.

  // Set the leading bit to 1 to indicate that a four-byte sequence is
  // present.
  *byte_iter = (static_cast<uint8_t>(length >> 24) | 0x80U);

  for(char i {0}; i < 3; i++)
  {
    ++byte_iter;

    *byte_iter = length >> (16 - (8*i));
  }
}

// Determines and returns the length in bytes of a name or value when that
// length was encoded using four bytes in the FastCGI name-value pair format.
//
// Parameters:
// byte_iter: An iterator to the first byte of a four-byte sequence.
//
// Requires:
// The four-byte sequence pointed to by byte_iter is the encoding
// in the FastCGI name-value pair format of the length of another
// byte sequence that requires a four-byte length encoding.
//
// Effects:
// 1) The value returned is the number of bytes encoded in the four-byte
//    sequence pointed to by byte_iter.
template <typename ByteIter>
uint32_t ExtractFourByteLength(ByteIter byte_iter)
{
  // TODO Add template type property checking with static_asserts.

  // Mask out the leading 1 bit which must be present per the FastCGI
  // name-value pair format. This bit does not encode length information.
  // It indicates that the byte sequence has four elements instead of one.
  uint32_t length {static_cast<uint32_t>(
    static_cast<uint8_t>(*byte_iter) & 0x7fU)};
  // Perform three shifts by 8 bits to extract all four bytes.
  for(char i {0}; i < 3; i++)
  {
    length <<= 8;
    ++byte_iter;

    length += static_cast<uint8_t>(*byte_iter);
  }
  return length;
}

// Processes name-value pairs and returns a tuple containing information which
// allows a byte sequence to be written via a scatter-gather I/O system call.
// When written, this byte sequence is the byte sequence of the name-value
// pairs when they are encoded in the FastCGI name-value pair format.
//
// Parameters:
// pair_iter: an iterator to a std::pair object.
// end: an iterator to one-past-the-last element in the range of std::pair
// objects whose name and value sequences are to be encoded.
//
// Requires:
// 1) [pair_iter, end) is a valid range.
// 2) When the range [pair_iter, end) is non-empty:
//    a) Each std::pair object of the range holds containers for sequences of
//       bytes.
//    b) The containers of each std::pair object must store objects
//       contiguously in memory. In particular, for container c, the
//       expression [c.data(), c.data()+c.size()) gives a valid range of the
//       stored objects.
//    c) For each container, let T be the type of the elements of the container.
//       Then sizeof(T) = sizeof(uint8_t).
// 3) Invalidation of references, pointers, or iterators to elements of the
//    name and value sequences invalidate the returned vector of iovec
//    instances.
//
// Effects:
// 0) The sequence of name-value pairs given by [pair_iter, end) is processed
//    in order.
// 1) Meaning of returned tuple values:
//       Access: std::get<0>; Type: bool; True if processing occurred
//    without error. False if processing was halted due to an error. See below.
//       Access: std::get<1>; Type: std::size_t; The total number of
//    bytes that would be written if all bytes pointed to by the struct iovec
//    instances of the returned std::vector<iovec> instance were written.
//    This value is equal to the sum of iov_len for each struct iovec instance
//    in the returned list. This value allows the actual number of bytes
//    written by a system call to be compared to the expected number.
//       Access: std::get<2>; Type: std::vector<iovec>; A list of
//    iovec instances to be used in a call to writev or a similar
//    function. For a returned tuple t, std::get<2>(t).data() is a pointer
//    to the first iovec instance and may be used in a call to writev.
//       Access: std::get<3>; Type: const std::vector<uint8_t>; A
//    byte sequence that contains FastCGI headers and encoded name and value
//    length information. Destruction of this vector invalidates pointers
//    contained in the iovec instances of the vector accessed by std::get<2>.
//       Access: std::get<4>; Type: std::size_t; Zero if all name-value
//    pairs in the encoded range were completely encoded. Non-zero if the last
//    name-value pair of the encoded range could not be completely encoded.
//    When non-zero, the value indicates the number of bytes from the encoded
//    FastCGI name-value byte sequence that will be written. This value is
//    intended to be passed to EncodeNameValuePairs in a subsequent call so
//    that the list of iovec structures produced does not contain duplicate
//    information.
//       Access: std::get<5>; Type: typename ByteSeqPairIter; An
//    iterator pointing to an element of the range given by pair_iter
//    and end. If the returned boolean value is false, the iterator points
//    to the name-value pair which caused processing to halt. If the returned
//    boolean value is true and std::get<4>(t) == 0, the iterator points to
//    either end, if all name-value pairs could be encoded, or to the
//    name-value pair which should be encoded next. If the returned boolean
//    value is true and std::get<4>(t) != 0, the iterator points to the name-
//    value pair which could not be completely encoded.
// 2) If the range [pair_iter, end) is empty, then the returned tuple is
//    equal to the tuple initialized by {true, 0, {}, {}, 0, end}.
// 3) In two cases, the boolean value of the tuple returned by the function
//    is false. This occurs when values are detected that cause normal
//    processing to halt. In these cases, any data for previously processed
//    name-value pairs is returned and no data for the rejected name-value pair
//    is returned. The returned iterator points to the name-value pair which
//    caused processing to halt.
//    a) Processing halts if the length of the name or value of a name-value
//       pair exceeds the limit of the FastCGI name-value pair format. This
//       limit is 2^31 - 1.
//    b) Processing halts if the implementation of the function detects
//       that an internal overflow would occur.
template<typename ByteSeqPairIter>
std::tuple<bool, std::size_t, std::vector<iovec>, const std::vector<uint8_t>,
  std::size_t, ByteSeqPairIter>
EncodeNameValuePairs(ByteSeqPairIter pair_iter, ByteSeqPairIter end,
  fcgi_si::FCGIType type, std::uint16_t FCGI_id, std::size_t offset)
{
  if(pair_iter == end)
    return {true, 0, {}, {}, 0, end};

  const std::size_t size_t_MAX {std::numeric_limits<std::size_t>::max()};
  const ssize_t ssize_t_MAX {std::numeric_limits<ssize_t>::max()};
  const uint16_t aligned_record_MAX {fcgi_si::kMaxRecordContentByteLength - 7};
  // Reduce by 7 to ensure that the length of a "full" record is a
  // multiple of 8.

  // Determine the maximum number of iovec structures that can
  // be used for scatter-gather writing.
  long iovec_max {sysconf(_SC_IOV_MAX)};
  // Use the current Linux default if information cannot be obtained.
  if(iovec_max == -1)
    iovec_max = 1024;
  iovec_max = std::min<long>(iovec_max, std::numeric_limits<int>::max());
  // Determine the initial values of the break variables.
  // Reduce by one to ensure that a struct for padding is always available.
    // (Safe conversion from signed to unsigned.)
  std::size_t remaining_iovec_count(iovec_max - 1);
  // Reduce by FCGI_HEADER_LEN - 1 to ensure that padding can always be added.
  std::size_t remaining_byte_count {ssize_t_MAX - (FCGI_HEADER_LEN - 1)};
  // Lambda functions to simplify the loops below.
  auto size_iter = [&](int i)->std::size_t
  {
    if(i == 0)
      return pair_iter->first.size();
    else
      return pair_iter->second.size();
  };

  auto data_iter = [&](int i)->uint8_t*
  {
    if(i == 0)
      return (uint8_t*)(pair_iter->first.data());
    else
      return (uint8_t*)(pair_iter->second.data());
  };
  // A binary sequence of headers and length information encoded in the
  // FastCGI name-value pair format is created and returned to
  // the caller. A pair which holds an index into iovec_list and an index into
  // local_buffers is stored whenever a record
  // is referred to by an iovec_list element. This pair allows pointer values
  // to be determined once the memory allocated for local_buffers
  // will no longer change.
  // An initial header's worth is allocated in local_buffers.
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
    std::size_t sums[3] = {};
    std::vector<uint8_t>::size_type name_value_buffer_offset
      {local_buffers.size()};
    std::uint8_t* data_array[2];
    // Reset for a new pair.
    nv_pair_bytes_placed = offset;
    // Collect information about the name and value byte sequences.
    for(int i {1}; i < 3; i++)
    {
      size_array[i] = size_iter(i-1);
      if(size_array[i])
        data_array[i-1] = data_iter(i-1);
      else
        data_array[i-1] = nullptr;
      if(size_array[i] <= fcgi_si::kNameValuePairSingleByteLength)
      {
        // A safe narrowing of size_array[i] from std::size_t to std::uint8_t.
        local_buffers.push_back(size_array[i]);
        size_array[0] += 1;
      }
      else if(size_array[i] <= fcgi_si::kNameValuePairFourByteLength)
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
          // and fcgi_si::FCGI_HEADER_LEN - 1 bytes were reserved.
          if((remaining_iovec_count >= 2) &&
             (remaining_byte_count >= (fcgi_si::FCGI_HEADER_LEN + 1)))
          {
            previous_header_offset = local_buffers.size();
            index_pairs.push_back({iovec_list.size(), previous_header_offset});
            iovec_list.push_back({nullptr, fcgi_si::FCGI_HEADER_LEN});
            local_buffers.insert(local_buffers.end(), fcgi_si::FCGI_HEADER_LEN, 0);
            fcgi_si::PopulateHeader(local_buffers.data() + previous_header_offset,
              type, FCGI_id, 0, 0);
            number_to_write += fcgi_si::FCGI_HEADER_LEN;
            remaining_byte_count -= fcgi_si::FCGI_HEADER_LEN;
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
              iovec_list.push_back({data_array[index-1] + (size_array[index]
                - local_remaining), local_number_to_place});
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
      (void*)(local_buffers.data() + pair.second);
  // Check for rejection based on a limit or name or value that was too big.
  return std::make_tuple(!name_or_value_too_big && !overflow_detected,
    number_to_write, std::move(iovec_list), std::move(local_buffers),
    ((incomplete_nv_write) ? nv_pair_bytes_placed : 0), pair_iter);
}

// Extracts a collection of name-value pairs when they are encoded as a
// sequence of bytes in the FastCGI name-value pair encoding.
// Note: Checking if content_length is zero before calling allows for
// the detection of an empty collection of name-value pairs.
//
// Parameters:
// content_length: the total size of the sequence of bytes which constitutes
// the collection of name-value pairs.
// content_ptr: points to the first byte of the byte sequence.
//
// Requires:
// 1) The value of content_length is exactly equal to the number of bytes
//    which represent the collection of name-value pairs. This number does
//    not include the length of a FastCGI record header.
// 2) content_ptr may only be null if content_length == 0.
//
// Effects:
// 1) If a sequential application of the encoding rules to the encountered
//    length values gives a length which is equal to content_length, a vector
//    is returned of the name-value pairs extracted from content_length bytes.
// 2) If content_length was not long enough for the extracted sequence of
//    name-value pairs, an empty vector is returned.
std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
ProcessBinaryNameValuePairs(uint32_t content_length, const uint8_t* content_ptr);

std::vector<uint8_t> uint32_tToUnsignedCharacterVector(uint32_t c);

} // namespace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_PAIR_PROCESSING_H_
