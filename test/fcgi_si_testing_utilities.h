#ifndef FCGI_SI_TEST_TEST_FCGI_SI_TESTING_UTILITIES_H_
#define FCGI_SI_TEST_TEST_FCGI_SI_TESTING_UTILITIES_H_

#include <cstdint>
#include <tuple>
#include <vector>

#include "include/protocol_constants.h"

// Key:
// BAZEL DEPENDENCY       This marks use of a feature which is provided by the
//                        Bazel testing run-time environment. 
// GOOGLE TEST DEPENDENCY This marks use of a feature which is provided by
//                        Google Test.

namespace fcgi_si_test {

// Create a temporary file in the temporary directory offered by Bazel.
// The descriptor value is written to the int pointed to by descriptor_ptr.
//
// Failures are reported as Google Test fatal failures.
//
// BAZEL DEPENDENCY: TEST_TMPDIR environment variable.
// GOOGLE TEST DEPENDENCY
void CreateBazelTemporaryFile(int* descriptor_ptr);

// Truncate a file to zero length and seek to the beginning.
//
// Failures are reported as Google Test non-fatal failures.
//
// GOOGLE TEST DEPENDENCY
bool PrepareTemporaryFile(int descriptor);

//    A utility function used for testing. ExtractContent reads a file which
// contains a sequence of FastCGI records. These records are assumed to be
// from a single, complete record sequence. (Multiple records may be present in
// a sequence when it is associated with a stream record type from the FastCGI
// protocol.) Two operations are performed.
//
//    First, several error checks are performed.
// 1) Each header is validated for type and request identifer. Header
//    errors terminate sequence processing.
// 2) The actual number of bytes present for each section of a record is
//    compared to the expected number. Logcially, incomplete sections may only
//    occur when the end of the file is reached.
// 3) The total length of each record is verified to be a multiple of eight
//    bytes.
//
//    Second, the content byte sequence formed from the concatenation of
// the record content sections is constructed and returned.
//
// Parameters:
// fd: The file descriptor of the file to be read.
// type: The expected FastCGI record type of the record sequence.
// id: The expected FastCGI request identifier of each record in the sequence.
//
// Preconditions:
// 1) The file offset of fd is assumed to be at the start of the record
//    sequence.
// 2) It is assumed that no other data is present in the file.
// 3) Only EINTR is handled when fd is read. (Other errors cause function
//    return with a false value for the first boolean variable of the returned
//    tuple.)
//
// Effects:
// 1) Meaning of returned tuple elements.
//       Access: std::get<0>; Type: bool; True if no unrecoverable errors
//    were encountered when the file was read. False otherwise. The values of
//    the other members of the tuple are unspecified when this member is false.
//       Access: std::get<1>; Type: bool; True if neither a FastCGI type error
//    nor an identifier error was present and no incomplete record section was
//    present. False otherwise.
//       Access: std::get<2>; Type: bool; If no header errors or incomplete
//    section occurred while reading the sequence, this flag indicates if the
//    sequence was terminated by a record with zero content length (true) or
//    not (false). If header errors or incomplete section occurred, the flag is
//    false.
//       Access: std::get<3>; Type: bool; If no read errors were present
//    and no header or incomplete section errors were present, this flag is
//    true if no records were present or if all processed records had a total
//    record length which was a multiple of eight. The flag is false if header
//    or incomplete section errors were present or if a record was present
//    whose total length was not a multiple of eight bytes.
//       Access: std::get<4>; Type: std::vector<uint8_t>; The extracted
//    content of the records processed up to:
//    a) the point of error (such as the end of a partial record)
//    b) a record with a zero content length
//    c) the end of the file.
std::tuple<bool, bool, bool, bool, std::vector<std::uint8_t>>
ExtractContent(int fd, fcgi_si::FCGIType type, std::uint16_t id);

} // namespace fcgi_si_test

#endif  // FCGI_SI_TEST_TEST_FCGI_SI_TESTING_UTILITIES_H_
