#ifndef FCGI_SERVER_INTEFRACE_INCLUDE_PAIR_PROCESSING_H_
#define FCGI_SERVER_INTEFRACE_INCLUDE_PAIR_PROCESSING_H_

#include <cstdint>
#include <utility>
#include <vector>

namespace fcgi_si {

// Determines if a sequence of name-value pairs can be encoded in the
// binary FastCGI name-value pair format and transmitted to a client
// using scatter-gather writing.
//
// Parameters:
// pair_iter: an iterator to a range of std::pair objects.
// end: an iterator to one-past-the-last element in the range of std::pair
// objects.
//
// Requires:
// 1) [pair_iter, end) is a valid range.
// 2) When the range [pair_iter, end) is non-empty:
//    a) pair_iter points to a std::pair object which holds containers
//       for sequences of bytes.
//    b) The containers of each std::pair object must store objects
//       contiguously in memory. In particular, for container c, the
//       expression [c.data(), c.data()+c.size()) is defined and gives
//       a valid range of the stored objects.
//    c) For each container, let T be the type of the elements of the container.
//       Let p be a pointer of type T*. The following expression is defined:
//       static_cast<uint8_t*>(p).
//
// Effects:
// 1)
template<typename ByteSeqPairIter>
std::tuple<bool, std::vector<struct iovec>, const std::vector<uint8_t>,
  std::size_t number_to_write>
EncodeNameValuePairs(ByteSeqPairIter pair_iter, ByteSeqPairIter end)
{
  if(pair_iter != end)
  {
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
          return static_cast<uint8_t*>(pair_iter->first.data());
        else
          return static_cast<uint8_t*>(pair_iter->second.data());
      };

    // A binary sequence of length information encoded in the
    // FastCGI name-value pair format is created and returned to
    // the caller.
    std::vector<uint8_t> encoded_name_value_lengths {};
    std::vector<std::size_t> length_offsets {0};

    // iovec_list will hold three instances of struct iovec for every
    // name-value pair. The first instance describes name and value length
    // information. It points to a range of bytes in encoded_name_value_lengths.
    // The second and third instances hold name and value information,
    // repsectively. They point to the buffers of containers from a std::pair
    // object when such buffers exist.
    std::vector<struct iovec> iovec_list {};
    std::vector<std::vector<struct iovec>::size_type> absent_buffer_index_list {};

    // An accumulator for use when checking the return value of a scatter-
    // gather write function.
    std::size_t number_to_write {0};

    // Loop iteration continues even if a name or value is rejected.
    // Rejection status is checked after iteration.
    bool rejected {false};

    uint8_t* next_name_value_iov_base {nullptr};
    for(/*no-op*/; pair_iter != end; ++pair_iter)
    {
      std::size_t iov_len {0};
      std::size_t size_array[2];
      uint8_t* data_array[2];

      for(int i {0}; i < 2; ++i)
      {
        size_array[i] = size_iter(i);
        if(size_array[i])
          data_array[i] = data_iter(i);
        else
        {
          data_array[i] = nullptr;
          absent_buffer_index_list.push_back[iovec_list.size() + 1 + i];
        }
        if(size <= fcgi_si::kNameValuePairSingleByteLength)
        {
          // A safe narrowing of size from std::size_t to uint8_t.
          encoded_name_value_lengths.push_back(size);
          iov_len += 1;
        }
        else if(size <= fcgi_si::kNameValuePairFourByteLength)
        {
          // A safe narrowing of size from std::size_t to a representation of
          // a subset of the range of uint32_t.
          EncodeFourByteLength(size, std::back_inserter(encoded_name_value_lengths));
          iov_len += 4;
        }
        else
        {
          rejected = true;
          encoded_name_value_lengths.push_back(0);
          iov_len += 1;
        }
      }

      iovec_list.insert(iovec_list.end(), {{nullptr, iov_len},
        {data_array[0], size_array[0]}, {data_array[1], size_array[1]}});
      length_offsets.push_back(iov_len);
      number_to_write += iov_len + size_array[0] + size_array[1];
    }
    // Check for rejection which was detected during length extraction.
    // Check for rejection based on int overflow on the number of buffers
    // for scatter-gather writing.
    if(rejected || iovec_list.size() > std::numeric_limits<int>::max())
      return {false, {}, {}, 0};

    // Since the memory of encoded_name_value_lengths is stable, fill in
    // iov_base pointers from encoded_name_value_lengths.data().


    // Process any absent name or value buffers.

    return {true, iovec_list, encoded_name_value_lengths, number_to_write};

  }
  return {true, {}, {}, 0};
}

// Determines and returns the length in bytes of a name or value when that
// length was encoded using four bytes in the FastCGI name-value pair format.
//
// Parameters:
// byte_iter: An iterator to the first byte of a four-byte sequence.
//
// Requires:
// 1) byte_iter has the following properties:
//    a) *byte_iter is convertible to uint8_t.
//    b) The four-byte sequence pointed to by byte_iter is the encoding
//       in the FastCGI name-value pair format of the length of another
//       byte sequence that requires a a four-byte length encoding.
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
//    format. I.e. length is in [2^7; 2^31 - 1] = [128; 2,147,483,647].
// 2) byte_iter has the following properties:
//    a) *byte_iter is convertible to uint8_t.
//    b) Four uint8_t values can be appended to the buffer pointed to by
//       byte_iter.
//
// Effects:
// 1) Four bytes are appended to the buffer pointed to by byte_iter. The
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
//    which represent the collection of name-value parirs. This number does
//    not include the byte length of a FastCGI record header.
//
// Effects:
// 1) If a sequential application of the encoding rules to the encountered
//    length values gives a length which is equal to content_length, a vector
//    is returned of the name-value pairs extracted from content_length bytes.
//    The pairs are of type:
//    std::pair<std::vector<uint8_t>, std::vector<uint8_t>>.
// 2) If content_length was not long enough for the extracted sequence of
//    name-value pairs, an empty vector is returned.
std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
ProcessBinaryNameValuePairs(int content_length, const uint8_t* content_ptr);

std::vector<uint8_t> uint32_tToUnsignedCharacterVector(uint32_t c);

} // namespace fcgi_si

#endif // FCGI_SERVER_INTEFRACE_INCLUDE_PAIR_PROCESSING_H_
