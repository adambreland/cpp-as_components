#ifndef FCGI_SERVER_INTEFRACE_INCLUDE_PAIR_PROCESSING_H_
#define FCGI_SERVER_INTEFRACE_INCLUDE_PAIR_PROCESSING_H_

#include <cstdint>
#include <utility>
#include <vector>

namespace fcgi_si {

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
