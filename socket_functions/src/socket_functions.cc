#include <sys/select.h>
#include <sys/time.h>     // For portability for select.
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <tuple>

#include "include/socket_functions.h"

namespace as_components {
namespace socket_functions {

std::size_t
SocketWrite(int fd, const std::uint8_t* buffer_ptr, std::size_t count) noexcept
{
  std::size_t number_remaining {count};
  ssize_t number_returned {};

  while(number_remaining > 0)
  {
    number_returned = write(fd, buffer_ptr, number_remaining);
    if(number_returned == -1)
    {
      if(errno == EINTR)
        number_returned = 0;
      else break; // Error value that doesn't permit re-calling write().
    }
    number_remaining -= number_returned;
    buffer_ptr += number_returned;
  }
  return count - number_remaining;
}

std::tuple<struct iovec*, int, std::size_t>
ScatterGatherSocketWrite(int fd, struct iovec* iovec_ptr, int iovec_count,
  std::size_t number_remaining, bool wait_on_select,
  struct timeval* timeout_ptr) noexcept
{
  ssize_t number_returned {0};

  while(number_remaining > 0)
  {
    number_returned = writev(fd, iovec_ptr, iovec_count);
    if(number_returned == -1)
    {
      // Check if an error occurred which doesn't permit recalling writev.
      if(errno == EINTR)
      {
        continue;
      }
      else if(wait_on_select && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
      {
        bool ready_to_write {true};
        struct timeval  local_timeout {};
        struct timeval* local_timeout_ptr {(timeout_ptr) ? &local_timeout
          : nullptr};
        while(true)
        {
          if(timeout_ptr)
          {
            local_timeout = *timeout_ptr;
          }
          fd_set select_on {};
          FD_ZERO(&select_on);
          FD_SET(fd, &select_on);
          int select_return {select(fd, nullptr, &select_on, nullptr,
            local_timeout_ptr)};
          if(select_return == -1)
          {
            if(errno == EINTR)
            {
              continue;
            }
            else
            {
              ready_to_write = false;
            }
          }
          break;
        }
        if(!ready_to_write)
        {
          break;
        }
        continue;
      }
      else
      {
        break;
      }
    }
    // This cast is always safe as number_returned is always greater than zero
    // at this point.
    number_remaining -= static_cast<std::size_t>(number_returned);
    if(number_remaining > 0)
    {
      std::pair<struct iovec*, int> scatter_update
        {ScatterGatherHelper(number_returned, iovec_ptr,
          iovec_count)};
      iovec_ptr = scatter_update.first;
      iovec_count = scatter_update.second;
    }
  }
  return std::tuple<struct iovec*, int, std::size_t>
    {iovec_ptr, iovec_count, number_remaining};
}

std::size_t
SocketRead(int fd, std::uint8_t* buffer_ptr, std::size_t count) noexcept
{
  std::size_t number_remaining {count};
  ssize_t number_returned {};

  while(number_remaining > 0)
  {
    number_returned = read(fd, (void*)buffer_ptr, number_remaining);
    if(number_returned == 0)
    {
      errno = 0;
      break;
    }
    else if(number_returned == -1)
    {
      if(errno == EINTR) 
        number_returned = 0;
      else 
        break;
    }
    number_remaining -= number_returned;
    buffer_ptr += number_returned;
  }
  return count - number_remaining;
}

std::pair<struct iovec*, int> 
ScatterGatherHelper(ssize_t number_bytes_wr, struct iovec* iov_ptr,
  int iov_array_count) noexcept
{
  // Technically a narrowing conversion.
  // However, it is assumed that writev/readv would have thrown an error if
  // a call was made with buffers whose total length would have caused an
  // overflow for a variable of type ssize_t.
  ssize_t running_total(iov_ptr->iov_len);

  while(running_total <= number_bytes_wr)
  {
    iov_ptr++;
    running_total += iov_ptr->iov_len;
    iov_array_count--;
  }

  ssize_t new_length {running_total - number_bytes_wr};
  ssize_t offset(iov_ptr->iov_len - new_length); // Again, technically narrowing.
  uint8_t* byte_ptr {static_cast<uint8_t*>(iov_ptr->iov_base)};
  byte_ptr += offset;

  iov_ptr->iov_base  = static_cast<void*>(byte_ptr);
  iov_ptr->iov_len   = new_length;

  return std::pair<struct iovec*, int> {iov_ptr, iov_array_count};
}

std::size_t WriteOnSelect(int fd, const std::uint8_t* buffer_ptr,
  std::size_t count, struct timeval* timeout_ptr) noexcept
{
  std::size_t number_remaining {count};
  ssize_t number_returned {};

  int nfds {fd + 1};
  fd_set write_set {};

  struct timeval  local_timeout {};
  struct timeval* local_timeout_ptr {(timeout_ptr) ? &local_timeout : nullptr};

  while(number_remaining > 0)
  {
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);
    if(timeout_ptr)
      local_timeout = *timeout_ptr;
    int select_return {select(nfds, nullptr, &write_set, nullptr, 
      local_timeout_ptr)};
    if(select_return == -1)
    {
      if(errno == EINTR)
        continue;
      else
        break;
    }
    else if(select_return == 0) {
      errno = 0;
      break;
    }
    number_returned = write(fd, buffer_ptr, number_remaining);
    if(number_returned == -1)
    {
      // Spurious wake ups may occur with select for sockets.
      if(errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        number_returned = 0;
      else 
        break; // Error value that doesn't permit re-calling write().
    }
    number_remaining -= number_returned;
    buffer_ptr += number_returned;
  }
  return count - number_remaining;
}

} // namespace socket_functions
} // namepsace as_components
