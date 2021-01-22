#ifndef AS_COMPONENTS_SOCKET_FUNCTIONS_INCLUDE_SOCKET_FUNCTIONS_H_
#define AS_COMPONENTS_SOCKET_FUNCTIONS_INCLUDE_SOCKET_FUNCTIONS_H_

#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>       // For scatter-gather I/O.

#include <cstdint>         // For std::uint8_t.
#include <cstdlib>         // For std::size_t.
#include <utility>

namespace as_components {
namespace socket_functions {

// A utility wrapper for write which handles EINTR errors. A short count
// indicates that another error prevented a full write.
//
// Parameters:
// fd:         The file descriptor on which to write.
// buffer_ptr: A pointer to the byte buffer from which to write.
// count:      The number of bytes to write.
//
// Preconditions:
// 1) The caller must ensure that SIGPIPE signals are appropriately handled.
//
// Effects:
// 1) The value returned is in the range [0, count]. It indicates the number of
//    bytes written.
// 2) If the value returned is less than count, an error other than EINTR
//    prevented further writes.  
// 3) Does not modify errno.
std::size_t SocketWrite(int fd, const std::uint8_t* buffer_ptr, 
  std::size_t count) noexcept;

// A utility function for performing scatter-gather writes with writev.
// This function is intended to be used in contexts were short counts are
// expected, such as when writing to a connected socket. EINTR errors are
// handled.
//
// Parameters:
// fd:              The file descriptor of the file to be written to.
// iovec_ptr:       The first struct iovec instance in an array of such.
//                  Note that the pointed-to struct iovec instances may be 
//                  altered by a call.
// iovec_count:     The number of struct iovec instances from which to
//                  write data.
// number_to_write: The total number of bytes that would be written if all
//                  byte ranges in the sequence of struct iovec instances 
//                  [iovec_ptr, iovec_ptr + iovec_count) were written.
// wait_on_select:  A flag to indicate if EAGAIN and EWOULDBLOCK errors from a
//                  call to writev should cause the call to return or should
//                  cause a call to select to be made. When true, a call to
//                  select is made when these errors occur. This flag is
//                  intended to be used with non-blocking sockets.
// timeout_ptr:     When wait_on_select == true, this flag allows a
//                  struct timeval pointer to be passed to select calls if
//                  such calls are needed. A null value denotes no timeout
//                  limit.
//
// Preconditions: none.
//
// Exceptions: noexcept
//
// Effects:
// 1) Write success or failure is determined by inspecting one of the values
//    of the returned tuple. Let t be the returned tuple.
//    a) If std::get<2>(t) == 0, then all bytes were written.
//    b) If std::get<2>(t) != 0, then an error prevented all bytes from being
//       written. The value of errno should be checked. This value will not be
//       EINTR. If wait_on_select was true, errno will not hold EAGAIN or
//       EWOULDBLOCK. When the error is resolved, another call may be made
//       using the returned tuple values.
// 2) Meaning of returned tuple values.
//       Access: std::get<0>; Type: struct iovec*; A pointer to one of the
//    struct iovec instances in the range [iovec_ptr, iovec_ptr + iovec_count).
//    The iov_base and iov_len values were updated as necessary to allow a call
//    to be made again. The struct iovec instances before the instance pointed
//    to may have been modified.
//       Access: std::get<1>; Type: int; The number of struct iovec instances
//    for which some data remains to be written.    
//       Access: std::get<2>; Type: std::size_t; The number of bytes which
//    remain to be written.
std::tuple<struct iovec*, int, std::size_t>
ScatterGatherSocketWrite(int fd, struct iovec* iovec_ptr, int iovec_count,
  std::size_t number_to_write, bool wait_on_select = false,
  struct timeval* timeout_ptr = nullptr) noexcept;

inline std::tuple<struct iovec*, int, std::size_t>
ScatterGatherSocketWrite(int fd, struct iovec* iovec_ptr, 
  int iovec_count, bool wait_on_select = false,
  struct timeval* timeout_ptr = nullptr) noexcept
{
  // Calculate the total number of bytes to be written.
  std::size_t number_remaining {0U};
  struct iovec* iovec_counter_ptr {iovec_ptr};
  for(int i {0}; i < iovec_count; ++i)
  {
    number_remaining += iovec_counter_ptr->iov_len;
    ++iovec_counter_ptr;
  }
  return ScatterGatherSocketWrite(fd, iovec_ptr, iovec_count,
    number_remaining, wait_on_select, timeout_ptr);
}

// Reads while automatically re-calling read if EINTR is encountered. If a
// short count is returned, errno should be checked. Note that read is
// called again after a previous call returned if an error did not occur in
// that call. This behavior makes SocketRead primarily suitable for
// non-blocking sockets.
//
// Parameters:
// fd:         The socket descriptor which will be read.
// buffer_ptr: A pointer to the first location where read data will be written.
// count:      The number of bytes to attempt to read.
//
// Preconditions:
// 1) buffer_ptr must be large enough to accommodate count bytes.
//
// Effects:
// 1) Returns the number of bytes read and written starting at buffer_ptr.
//    The returned number is in the range [0, count].
// 2) If the number returned is less than count, errno will either:
//    a) Hold the error returned by the call to read which prevented further
//       reads. As EINTR is handled, errno will never equal EINTR after the
//       call returns.
//    b) Hold 0. If errno == 0, EOF was reached.
std::size_t SocketRead(int fd, std::uint8_t* buffer_ptr, std::size_t count)
  noexcept;

std::pair<struct iovec*, int> ScatterGatherHelper(ssize_t count,
  struct iovec* iov_ptr, int iovcnt) noexcept;

// A utility function intended to be used to write to a non-blocking socket.
// Short counts, errno error EINTR, and errno blocking errors EAGAIN and
// EWOULDBLOCK are handled. If the write would block, WriteOnSelect blocks in
// an internal call to select.
//
// Parameters:
// fd:          The socket descriptor which will be written to.
// buffer_ptr:  A pointer to the first location where data will be read to 
//              write.
// count:       The number of bytes of the buffer pointed to by buffer_ptr to
//              write.
// timeout_ptr: A pointer to a struct timeval instance to be used in
//              internal calls to select. A nullptr value implies no limit.
//
// Preconditions: none
//
// Effects:
// 1) The number of bytes written was returned. The returned value is in the
//    range [0, count].
// 2) a) If a short count was not returned, then count bytes were written to fd
//       starting from buffer_ptr. Blocking was handled with an internal call
//       to select.
//    b) If a short count was returned, then an error or a timeout prevented 
//       count bytes from being written.
//       1) If errno == 0, then a call to select timed out relative to the
//          information which was provided in timeout_ptr.
//          Any information returned by a call to select through a write to the
//          struct timeval instance referenced in the call is not available.
//       2) If errno != 0, then errno describes the error which prevented
//          further writes.
std::size_t WriteOnSelect(int fd, const std::uint8_t* buffer_ptr, 
  std::size_t count, struct timeval* timeout_ptr = nullptr) noexcept;

} // namespace socket_functions
} // namespace as_components

#endif // AS_COMPONENTS_SOCKET_FUNCTIONS_INCLUDE_SOCKET_FUNCTIONS_H_
