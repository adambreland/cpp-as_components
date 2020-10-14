#ifndef FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_

#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "include/protocol_constants.h"

namespace a_component {
namespace fcgi {

// The system-dependent maximum struct iovec array length for scatter-gatter
// I/O.
const long iovec_MAX {sysconf(_SC_IOV_MAX)};

// Encodes length in the FastCGI name-value pair format and stores the
// output sequence of four bytes in the byte buffer pointed to by byte_iter.
//
// Parameters:
// length:    The length to be encoded per the FastCGI name-value pair
//            format.
// byte_iter: An iterator to a byte-buffer which will hold the four-byte
//            sequence which encodes length.
//
// Preconditions:
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
// Exceptions:
// 1) May throw exceptions derived from std::exception.
// 2) Throws std::invalid_argument if length is less than 128.
// 3) In the event of a throw, the values of the sequence given by 
//    [byte_iter, byte_iter + 4) are indeterminate.
//
// Effects:
// 1) Four bytes are written to the sequence pointed to by byte_iter. The
//    byte sequence encodes length in the FastCGI name-value pair format.
template<typename ByteIter>
void EncodeFourByteLength(std::int_fast32_t length, ByteIter byte_iter);

// Processes name-value pairs and returns a tuple containing information which
// allows a byte sequence to be written via a scatter-gather I/O system call.
// When written, this byte sequence is the byte sequence of an initial range
// of the name-value pairs when they are encoded as FastCGI records. The
// content of these records is encoded in the FastCGI name-value pair format.
//
// Parameters:
// pair_iter: An iterator to a std::pair object.
//            pair_iter->first is a container for the name byte sequence.
//            pair_iter->second is a container for the value byte sequence.
// end:       An iterator to one-past-the-last element in the range of
//            std::pair objects whose name and value sequences are to be
//            encoded.
// type:      The FastCGI record type of the records to be generated from the
//            sequence of name-value pairs.
// Fcgi_id:   The FastCGI identifier of the records to be generated from the
//            sequence of name-value pairs.
// offset:    A value used to indicate how many bytes have previously been
//            encoded of the first name-value pair pointed to by pair_iter.
//            The first offset bytes of the byte sequence generated from this
//            name-value pair will be omitted from the total byte sequence
//            of the pair.
//
// Preconditions:
// 1) [pair_iter, end) is a valid range.
// 2) When the range [pair_iter, end) is non-empty:
//    a) Each std::pair object of the range holds containers for sequences of
//       bytes.
//    b) The containers of each std::pair object must store objects
//       contiguously in memory. In particular, for container c, the
//       expression [c.data(), c.data()+c.size()) gives a valid range of the
//       stored objects when c.size() is non-zero.
//    c) For each container, let T be the type of the elements of the container.
//       Then sizeof(T) = sizeof(std::uint8_t).
// 3) Invalidation of references, pointers, or iterators to elements of the
//    name and value sequences invalidate the returned vector of iovec
//    instances.
// 4) offset is zero unless a non-zero value for std::get<4> was returned from
//    an immediately-preceding call to EncodeNameValuePairs. When a such a
//    non-zero value was returned, offset is equal to that value.
//
// Effects:
// 1) The sequence of name-value pairs given by [pair_iter, end) is processed
//    in order.
// 2) Meaning of returned tuple values:
//    a) Access: std::get<0>; Type: bool; True if processing occurred
//       without error. False if processing was halted due to an error.
//    b) Access: std::get<1>; Type: std::size_t; The total number of
//       bytes that would be written if all bytes pointed to by the struct
//       iovec instances of the returned std::vector<iovec> instance were
//       written. This value is equal to the sum of iov_len for each struct
//       iovec instance in the returned list. This value allows the actual
//       number of bytes written by a scatter-gather write system call to be
//       compared to the expected number.
//    c) Access: std::get<2>; Type: std::vector<iovec>; A list of iovec
//       instances to be used in a call to writev or a similar function. For a
//       returned tuple t, std::get<2>(t).data() is a pointer to the first
//       iovec instance and may be used in a call to writev.
//    d) Access: std::get<3>; Type: int; The number of FastCGI records which
//       are encoded in the returned struct iovec vector.
//    e) Access: std::get<4>; Type: const std::vector<uint8_t>; A byte sequence
//       that contains FastCGI headers and encoded name and value length
//       information. Destruction of this vector invalidates pointers contained
//       in the iovec instances of the vector accessed by std::get<2>.
//    f) Access: std::get<5>; Type: std::size_t; 
//       1) Zero if all name-value pairs in the encoded range were completely 
//          encoded. 
//       2) Non-zero if the last name-value pair of the encoded range could not
//          be completely encoded. When non-zero, the value indicates the 
//          number of bytes from the encoded FastCGI name-value byte sequence
//          that will be written. This value is intended to be passed to
//          EncodeNameValuePairs in a subsequent call so that the list of iovec
//          structures produced in that call does not contain duplicate
//          information.
//    g) Access: std::get<6>; Type: typename ByteSeqPairIter; An iterator
//       pointing to an element of the range given by pair_iter and end, or
//       an iterator equal to end.
//       a) If the returned boolean value is false, the iterator points
//          to the name-value pair which caused processing to halt. 
//       b) If the returned boolean value is true and std::get<5>(t) == 0, the 
//          iterator points to either end, if all name-value pairs could be
//          encoded, or to the name-value pair which should be encoded next. If
//          the returned boolean value is true and std::get<5>(t) != 0, the
//          iterator points to the name-value pair which could not be
//          completely encoded.
// 3) If the range [pair_iter, end) is empty, then the returned tuple is
//    equal to the tuple initialized by {true, 0, {}, {}, 0, end}.
// 4) In two cases, the boolean value of the tuple returned by the function
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
// 5) Processing is carried out so that a FastCGI header with an empty
//    content_length is never generated. A partial header, meaning a terminal
//    header whose length is less than FCGI_HEADER_LEN, also does not occur.
// 6) All records have a total length which is a multiple of eight bytes.
template<typename ByteSeqPairIter>
std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
  std::vector<std::uint8_t>, std::size_t, ByteSeqPairIter>
EncodeNameValuePairs(ByteSeqPairIter pair_iter, ByteSeqPairIter end,
  FcgiType type, std::uint16_t Fcgi_id, std::size_t offset);

// Returns true if a call to EncodeNameValuePairs could not encode all of its
// input in a single FastCGI record. Returns false otherwise.
//
// Parameters:
// result:   A reference to the return value of a call to EncodeNameValuePairs.
// end_iter: The iterator passed as the value of end in the call to
//           EncodeNameValuePairs whose result is referenced by result.
template<typename ByteSeqPairIter>
inline bool EncodeNVPairSingleRecordFailure(
  const std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
    std::vector<std::uint8_t>, std::size_t, ByteSeqPairIter>& result,
  ByteSeqPairIter end_iter
)
{
  return ((!std::get<0>(result))              ||
          (std::get<3>(result) != 1)          ||
          (std::get<5>(result) != 0U)         ||
          (std::get<6>(result) != end_iter));
}

//    Attempts to extract a collection of name-value pair byte sequences when 
// they are encoded as a sequence of bytes in the FastCGI name-value pair
// encoding.
//    Note: Checking if content_length is zero before a call allows for the
// differentiation of empty and erroneous byte sequences.
//
// Parameters:
// content_ptr:    A pointer to the first byte of the byte sequence.
// content_length: The total size of the sequence of bytes which constitutes
//                 the collection of name-value pairs. Name and value length
//                 information which is present in the encoded data contributes
//                 to this value.
//
// Preconditions:
// 1) content_ptr may only be null if content_length == 0.
// 2) The value of content_length is equal to the number of bytes
//    which represent the collection of name-value pairs. This number does
//    not include the length of FastCGI record headers.
//
// Exceptions:
// 1) May throw exceptions derived from std::exception.
// 2) Throws std::invalid_argument if a nullptr is given and
//    content_length != 0.
//
// Effects:
// 1) The encoded lengths of the name and value byte sequences are extracted
//    in the order of their occurrence. 
//    a) If content_length is not large enough to continue processing given the
//       encountered length values, processing halts and an empty list is
//       returned.
//    b) If content_length is sufficient, the name and value byte sequences
//       are extracted. A list of name-value pairs of byte sequences is
//       returned.
std::vector<std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>>
ExtractBinaryNameValuePairs(const std::uint8_t* content_ptr, 
  std::size_t content_length);

// Attempts to return the length in bytes of a name or value when that length 
// was encoded using four bytes in the FastCGI name-value pair format.
//
// Parameters:
// byte_iter: An iterator to the first byte of a four-byte sequence.
//
// Preconditions:
// 1) The byte sequence given by the range [byte_iter, byte_iter + 4) is the
//    correct encoding in the FastCGI name-value pair format of a length which
//    requires four bytes to be encoded.
// 2) The type of *byte_iter must be able to be safely cast through
//    static_cast<uint8_t>(*byte_iter).
//
// Exceptions:
// 1) May throw exceptions derived from std::exceptions.
// 2) In the event of a throw, the sequence given by [byte_iter, byte_iter + 4)
//    is unchanged.
//
// Effects:
// 1) The length is extracted from [byte_iter, byte_iter + 4) and returned.
template <typename ByteIter>
std::int_fast32_t ExtractFourByteLength(ByteIter byte_iter) noexcept;

//    Determines a partition of the byte sequence defined by [begin_iter,
// end_iter) whose parts can be sent as the content of FastCGI records. 
// Produces headers and scatter-gather write information which produce a
// sequence of FastCGI records.
//    This function is intended to be called in a loop if the byte
// sequence is too long to be handled in one call.
//    If begin_iter == end_iter, an empty (terminal) record is produced.
//
// Parameters:
// begin_iter: An iterator which points to the first byte of the byte
//             sequence to be partitioned into records.
// end_iter:   An iterator to one-past-the-last byte of the byte sequence
//             to be partitioned.
// type:       The FastCGI record type to be used for the produced records.
// Fcgi_id:    The FastCGI request identifier to be used for the produced
//             records.
//
// Preconditions:
// 1) The range formed by [begin_iter, end_iter) must be a contiguous
//    sequence of byte-sized objects.
//
// Exceptions:
// 1) May throw exceptions derived from std::exception. 
// 2) In the event of a throw, the call had no effect (strong exception 
//    guarantee).
//
// Reference invalidation note:
// 1) Modification or destruction of either the returned vector of octets
//    (std::get<0>) or the byte sequence given by [begin_iter, end_iter)
//    invalidates the returned sequence of struct iovec instances.
//
// Effects:
// 1)    A sequence of struct iovec instances is returned. This sequence
//    contains an array which can be used in a call to writev. When written
//    with a scatter-gather write, a sequence of FastCGI records is produced
//    whose content is a prefix of [begin_iter, end_iter). The fixed
//    information contained in the record headers is given by type and Fcgi_id.
//       If begin_iter == end_iter, an empty (terminal) record is produced.
// 2) Meaning of returned tuple elements:
//       Access: std::get<0>; Type: std::vector<std::uint8_t>; A vector of
//    bytes which holds information which is implicitly referenced in
//    the struct iovec instances returned by the call.
//       Access: std::get<1>; Type: std::vector<struct iovec>; A vector
//    of struct iovec instances. These instances hold the information
//    needed for a call to writev. References to bytes in [begin_iter,
//    end_iter) and the vector of bytes returned by the call are referenced
//    in these instances of struct iovec.
//       Access: std::get<2>; Type: std::size_t; The number of bytes that
//    a call to writev would write if all bytes referenced by the returned
//    array of struct iovec instances were written.
//       Access: std::get<3>; Type: ByteIter; If the range given by
//    [begin_iter, end_iter) could be completely encoded, this iterator is
//    equal to end_iter. If the range could not be completely encoded,
//    begin_iter together with this iterator give the range of bytes which 
//    could be encoded. The next call to PartitionByteSequence should use this
//    iterator to initialize begin_iter.
template<typename ByteIter>
std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>, std::size_t, 
  ByteIter>
PartitionByteSequence(ByteIter begin_iter, ByteIter end_iter, FcgiType type,
  std::uint16_t Fcgi_id);

void PopulateBeginRequestRecord(std::uint8_t* byte_ptr, std::uint16_t fcgi_id,
  std::uint16_t role, bool keep_conn) noexcept;

// Generates a FastCGI header and writes it to the indicated buffer. The values
// of the arguments are encoded per the FastCGI record header binary format.
//
// Parameters:
// byte_ptr:       A pointer to the first byte of the buffer which will hold
//                 the header.
// type:           The FastCGI type of the record described by the header.
// Fcgi_id:        The FastCGI request identifier of the record described by
//                 the header.
// content_length: The content length of the record described by the header.
// padding_length: The padding length of the record described by the header.
//
// Preconditions:
// 1) byte_ptr is not null.
// 2) The buffer pointed to by byte_ptr must be able to hold at least
//    FCGI_HEADER_LEN bytes.
//
// Exceptions: noexcept
//
// Effects:
// 1) FCGI_HEADER_LEN bytes, starting at the byte pointed to by byte_ptr,
//    are written. The written byte sequence is a FastCGI header which
//    encodes the values passed to PopulateHeader. Two byte fields are given
//    the same value for every header:
//    a) The first byte is given the value FCGI_VERSION_1 which equals 1.
//    b) The last byte, which is reserved by the FastCGI protocol, is zero.
void PopulateHeader(std::uint8_t* byte_ptr, FcgiType type,
  std::uint16_t fcgi_id, std::uint16_t content_length,
  std::uint8_t padding_length) noexcept;

//    Returns a vector of bytes which represents the integer argument in decimal 
// as a sequence of encoded characters. For example, the value 89 is converted
// to the sequence (0x38, 0x39) = ('8', '9').
//    Negative values are not allowed.
// 
// Parameters:
// c: The value to be converted.
//
// Preconditions: none.
//
// Exceptions:
// 1) May throw exceptions derived from std::exception.
// 2) Throws std::invalid_argument if a negative argument is given.
//
// Effects:
// 1) A vector containing a sequence of bytes which represents the decimal
//    character encoding of the argument is returned.
std::vector<uint8_t> ToUnsignedCharacterVector(int c);

namespace partition_byte_sequence_internal {
std::size_t InitializeMaxForIovec();
std::size_t NeededIovec(std::size_t m);
ssize_t     NeededSsize_t(std::size_t m);
std::size_t NeededLocalData(std::size_t m);
} // namespace partition_byte_sequence_internal

} // namespace fcgi
} // namespace a_component

#include "include/utilities_templates.h"

#endif // FCGI_SERVER_INTERFACE_INCLUDE_UTILITY_H_
