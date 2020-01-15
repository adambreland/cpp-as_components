#ifndef FCGI_SERVER_INTEFRACE_PAIR_PROCESSING_H_
#define FCGI_SERVER_INTEFRACE_PAIR_PROCESSING_H_

#include <cstdint>
#include <utility>
#include <vector>

namespace fcgi_si {

// Returns the length in bytes of a name or value when it is encoded
// using four bytes in the FastCGI name-value pair encoding. Names and
// values are variable length byte arrays.
//
// Parameters:
// content_ptr: points to the first byte of the byte sequence which
// determines the length of the corresponding name or value byte sequence.
//
// Requires:
// 1) The byte pointed to by content_ptr and the next three bytes constitute
//    a four-byte length as per the FastCGI name-value encoding.
//
// Effects:
// 1) The value returned is the length in bytes of the corresponding name or
//    value byte array.
uint32_t ExtractFourByteLength(const uint8_t* content_ptr);

void EncodeFourByteLength(uint32_t length, std::vector<uint8_t>* string_ptr);

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

std::vector<uint8_t>
ConvertToByteVector(const uint8_t* content_ptr, uint32_t content_length);

std::vector<uint8_t> uint32_tToUnsignedCharacterVector(uint32_t c);

} // namespace fcgi_si

#endif // FCGI_SERVER_INTEFRACE_PAIR_PROCESSING_H_
