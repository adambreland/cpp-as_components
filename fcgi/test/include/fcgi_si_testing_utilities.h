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

#ifndef AS_COMPONENTS_FCGI_TEST_INCLUDE_FCGI_SI_TESTING_UTILITIES_H_
#define AS_COMPONENTS_FCGI_TEST_INCLUDE_FCGI_SI_TESTING_UTILITIES_H_

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "server_interface_combined.h"

// Key:
// BAZEL DEPENDENCY       This marks use of a feature which is provided by the
//                        Bazel testing run-time environment. 
// GOOGLE TEST DEPENDENCY This marks use of a feature which is provided by
//                        Google Test.

// Note: All modules starting with GTest depend on Google Test. Modules which
//       depend on Google Test but whose names do not start with GTest are
//       marked with the Google Test dependency flag defined above.

namespace as_components {
namespace fcgi {
namespace test {

// Returns the string " case X." where X is the character representation of
// test_case. This string may be used in test messages.
std::string CaseSuffix(int test_case);

//    A utility function used for testing. ExtractContent reads a file which
// contains a sequence of FastCGI records. These records are assumed to be
// from a single, complete record sequence. (Multiple records may be present in
// a sequence when it is associated with a stream record type from the FastCGI
// protocol.) Three operations are performed.
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
//    Third, the number of FastCGI record headers, which is taken to represent
// the number of FastCGI records, is determined and returned.
//
// Parameters:
// fd:   The file descriptor of the file to be read.
// type: The expected FastCGI record type of the record sequence.
// id:   The expected FastCGI request identifier of each record in the sequence.
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
//    not (false). If header errors or an incomplete section occurred, the flag
//    is false.
//       Access: std::get<3>; Type: bool; If no read errors were present
//    and no header or incomplete section errors were present, this flag is
//    true if no records were present or if all processed records had a total
//    record length which was a multiple of eight. The flag is false if header
//    or incomplete section errors were present or if a record was present
//    whose total length was not a multiple of eight bytes.
//       Access: std::get<4>; Type: std::size_t; The number of FastCGI record
//    headers which were encountered. A partial header is not counted.
//       Access: std::get<5>; Type: std::vector<uint8_t>; The extracted
//    content of the records processed up to:
//    a) the point of error (such as the end of a partial record)
//    b) a record with a zero content length
//    c) the end of the file.
std::tuple<bool, bool, bool, bool, std::size_t, std::vector<std::uint8_t>>
ExtractContent(int fd, FcgiType type, std::uint16_t id);

class FcgiRequestIdManager
{
 public:
  std::uint16_t GetId();
  void ReleaseId(std::uint16_t id);

 private:
  void CorruptionCheck();

  std::set<std::uint16_t> available_;
  std::set<std::uint16_t> in_use_;
  bool corrupt_ {false};
};

// GTestNonFatalCreateInterface
// Creates a listening socket for an interface, and constructs an interface
// instance on the heap. Access is provided by a returned unique_ptr to the
// interface. The provided domain is used when the listening socket is created.
//
// Preconditions:
// 1) If domain == AF_UNIX, the length of the string pointed to by unix_path
//    including the terminating null byte must be less than or equal to the
//    path length limit of UNIX sockets.
//
// Exceptions:
// 1) Throws any exceptions thrown by the constructor of FcgiServerInterface. 
//    a) The interface socket file descriptor was closed.
//    b) The pointer to the interface is null.
// 2) Throws std::system_error if a file for a UNIX socket was created and it
//    could not be removed when creation was unsuccessful.
//
// Resource allocation and caller responsibilities:
// 1) On return a listening socket was created. This socket should be closed
//    when the interface instance is no longer needed to prevent a file
//    descriptor leak.
// 2) If domain == AF_UNIX, on return a socket file given by the path string
//    pointed to by unix_path is present. This file should be removed from the
//    file system when the interface is no longer needed.
//
// Effects:
// 1) If creation was successful:
//    a) std::get<0> accesses a unique_ptr which points to the interface.
//    b) std::get<1> accesses the descriptor value of the listening socket of
//       the interface. 
//    c) std::get<2> accesses the port of the listening socket of the interface.
//       The value is in network byte order. When a UNIX domain socket was
//       created, zero is present.
//    d) For the internet domains, the listening socket is bound to the default
//       address and an ephemeral port.
// 2) If creation was not successful, the unique_ptr accessed by 
//    std::get<0> holds nullptr. If a socket was created, its descriptor
//    was closed. If a socket file was created, it was removed.
struct InterfaceCreationArguments
{
  int domain;
  int backlog;
  int max_connections;
  int max_requests;
  int app_status;
  const char* unix_path;
};

std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
GTestNonFatalCreateInterface(const struct InterfaceCreationArguments& args,
  int invocation_line);

//    This class creates an interface with the parameters provided in
// inter_args. client_number sockets are created and connected to the
// interface. These sockets are made non-blocking to facilitate testing
// FcgiServerInterface and related classes using a single process.
//    The client socket descriptors, the interface, and interface information
// are made available through accessors.
//    All socket descriptors which are associated with an instance are closed
// by the destructor. If inter_args.domain == AF_UNIX, the socket file is
// removed by the destructor.
class GTestNonFatalSingleProcessInterfaceAndClients
{
 public:
  inline FcgiServerInterface& interface()
  {
    return *std::get<0>(inter_tuple_);
  }

  inline int interface_descriptor()
  {
    return std::get<1>(inter_tuple_);
  }

  inline struct sockaddr* interface_address_ptr()
  {
    return interface_addr_ptr_;
  }

  inline socklen_t interface_address_length()
  {
    return socket_addr_length_;
  }

  inline const std::vector<int>& client_descriptors()
  {
    return client_descriptors_;
  }

  // No copy.
  GTestNonFatalSingleProcessInterfaceAndClients() = default;
  GTestNonFatalSingleProcessInterfaceAndClients(
    struct InterfaceCreationArguments inter_args,  int client_number,
    int invocation_line);
  GTestNonFatalSingleProcessInterfaceAndClients(
    GTestNonFatalSingleProcessInterfaceAndClients&&) = default;
  GTestNonFatalSingleProcessInterfaceAndClients(
    const GTestNonFatalSingleProcessInterfaceAndClients&) = delete;
  
  GTestNonFatalSingleProcessInterfaceAndClients&
  operator=(GTestNonFatalSingleProcessInterfaceAndClients&&) = default;
  GTestNonFatalSingleProcessInterfaceAndClients&
  operator=(const GTestNonFatalSingleProcessInterfaceAndClients&) = delete;

  inline ~GTestNonFatalSingleProcessInterfaceAndClients()
  {
    CleanUp(__LINE__);
  }

 private:
  void CleanUp(int invocation_line);

  struct InterfaceCreationArguments inter_args_;
  std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  inter_tuple_;
  struct sockaddr* interface_addr_ptr_;
  socklen_t socket_addr_length_;
  std::vector<int> client_descriptors_; 
};

} // namespace test
} // namespace fcgi
} // namespace as_components

#endif  // AS_COMPONENTS_FCGI_TEST_INCLUDE_FCGI_SI_TESTING_UTILITIES_H_
