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

#include "test/include/fcgi_si_testing_utilities.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "external/googletest/googletest/include/gtest/gtest.h"

#include "include/fcgi_protocol_constants.h"
#include "include/fcgi_server_interface.h"

namespace as_components {
namespace fcgi {
namespace test {

std::string CaseSuffix(int test_case)
{
  std::string case_suffix {" case "};
  case_suffix.append(std::to_string(test_case));
  case_suffix.append(".");
  return case_suffix;
}

std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
GTestNonFatalCreateInterface(const struct InterfaceCreationArguments& args,
  int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestNonFatalCreateInterface"};
  std::unique_ptr<FcgiServerInterface> interface_uptr {};
  if((args.domain == AF_UNIX) && !args.unix_path)
    return std::make_tuple(std::move(interface_uptr), -1, 0U);
  int socket_fd {socket(args.domain, SOCK_STREAM, 0)};
  bool unix_socket_bound {false};

  auto CleanupForFailure = [&interface_uptr, &unix_socket_bound, socket_fd, 
    &args]
    (std::string message, int errno_value)->
      std::tuple<std::unique_ptr<FcgiServerInterface>, int, in_port_t>
  {
    if(errno_value)
      ADD_FAILURE() << message << '\n' << std::strerror(errno_value);
    else
      ADD_FAILURE() << message;

    if(socket_fd != -1)
      close(socket_fd);
    if(unix_socket_bound)
    {
      if(unlink(args.unix_path) == -1)
      {
        ADD_FAILURE() << "The UNIX socket created by a call to CreateInterface "
          "could not be removed during cleanup.";
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "unlink"};
      }
    }
    return std::make_tuple(std::move(interface_uptr), socket_fd, 0U);
  };

  if(socket_fd < 0)
    return CleanupForFailure("A call to socket failed.", errno);
  if(args.domain == AF_UNIX)
  {
    struct sockaddr_un AF_UNIX_addr {};
    AF_UNIX_addr.sun_family = AF_UNIX;
    std::strcpy(AF_UNIX_addr.sun_path, args.unix_path);
    if(bind(socket_fd, 
      static_cast<struct sockaddr*>(static_cast<void*>(&AF_UNIX_addr)),
      sizeof(AF_UNIX_addr)) == -1)
      return CleanupForFailure("A call to bind for a UNIX socket failed.",
        errno);
    unix_socket_bound = true;
  }  

  if(listen(socket_fd, args.backlog) < 0)
    return CleanupForFailure("A call to listen failed.", errno);

  // Generic state to be used to extract the address of the listening
  // socket when an internet domain is used.
  struct sockaddr* address_ptr {nullptr};
  socklen_t*       length_ptr  {nullptr};
  
  // State for internet domain sockets.
  // AF_INET
  struct sockaddr_in AF_INET_addr {};
  socklen_t AF_INET_socklen {sizeof(AF_INET_addr)};

  // AF_INET6
  struct sockaddr_in6 AF_INET6_addr {};
  socklen_t AF_INET6_socklen {sizeof(AF_INET6_addr)};

  if(args.domain != AF_UNIX)
  {
    if((args.domain == AF_INET) || (args.domain == AF_INET6))
    {
      if(args.domain == AF_INET)
      {
        address_ptr = static_cast<struct sockaddr*>(static_cast<void*>(
          &AF_INET_addr));
        length_ptr = &AF_INET_socklen;
      }
      else if(args.domain == AF_INET6)
      {
        address_ptr = static_cast<struct sockaddr*>(static_cast<void*>(
          &AF_INET6_addr));
        length_ptr = &AF_INET6_socklen;
      }
      if(getsockname(socket_fd, address_ptr, length_ptr) < 0)
        return CleanupForFailure("A call to getsockname failed.", errno);
    }
    else
      return CleanupForFailure("An invalid domain was given.", 0);
  }

  try
  {
    interface_uptr = std::unique_ptr<FcgiServerInterface> 
      {new FcgiServerInterface {socket_fd, args.max_connections, 
        args.max_requests, args.app_status}};
  }
  catch(...)
  {
    CleanupForFailure("interface construction", 0);
    throw;
  }
  
  return std::make_tuple(
    std::move(interface_uptr), 
    socket_fd, 
    (args.domain == AF_UNIX) ? 
      0U : 
      ((args.domain == AF_INET) ? 
        AF_INET_addr.sin_port :
        AF_INET6_addr.sin6_port)
  );
}

std::tuple<bool, bool, bool, bool, std::size_t, std::vector<std::uint8_t>>
ExtractContent(int fd, FcgiType type, std::uint16_t id)
{
  constexpr std::uint16_t buffer_size {1 << 10};
  std::uint8_t byte_buffer[buffer_size];

  std::uint32_t        local_offset                             {0U};
  ssize_t              number_bytes_read                        {0};
  std::uint8_t         local_header[FCGI_HEADER_LEN] = {};
  int                  header_bytes_read                        {0};
  std::vector<uint8_t> content_bytes                            {};
  std::uint16_t        Fcgi_id                                  {};
  std::uint16_t        content_length                           {0U};
  std::uint16_t        content_bytes_read                       {0U};
  std::uint8_t         padding_length                           {0U};
  std::uint8_t         padding_bytes_read                       {0U};
  bool                 read_error                               {false};
  bool                 header_error                             {false};
  bool                 sequence_terminated                      {false};
  bool                 aligned                                  {true};
  int                  state                                    {0};
  std::size_t          header_count                             {0U};

  while((number_bytes_read = read(fd, byte_buffer, buffer_size)))
  {
    if(number_bytes_read == -1)
    {
      if(errno == EINTR)
        continue;
      else
      {
        read_error = true;
        break;
      }
    }

    local_offset = 0;
    while(local_offset < number_bytes_read)
    {
      // Note that, below, the condition "section_bytes_read < expected_amount"
      // implies that local_offset == number_bytes_read.
      switch(state) {
        case 0 : {
          if(header_bytes_read < FCGI_HEADER_LEN)
          {
            // Safe narrowing as this can never exceed FCGI_HEADER_LEN.
            int header_bytes_to_copy(std::min<ssize_t>(FCGI_HEADER_LEN
              - header_bytes_read, number_bytes_read - local_offset));
            std::memcpy((void*)(local_header + header_bytes_read),
              (void*)(byte_buffer + local_offset), header_bytes_to_copy);
            local_offset += header_bytes_to_copy;
            header_bytes_read += header_bytes_to_copy;
            if(header_bytes_read < FCGI_HEADER_LEN)
            {
              break;
            }
            else
            {
              ++header_count;
            }
          }
          // The header is complete and there are some bytes left to process.
          // Extract header information.
          (Fcgi_id = local_header[kHeaderRequestIDB1Index]) <<= 8; 
            // One byte.
          Fcgi_id += local_header[kHeaderRequestIDB0Index];
          (content_length = local_header[kHeaderContentLengthB1Index]) 
            <<= 8;
          content_length += local_header[kHeaderContentLengthB0Index];
          padding_length = local_header[kHeaderPaddingLengthIndex];
          if((content_length + padding_length) % 8 != 0)
            aligned = false;
          // Verify header information.
          if(static_cast<FcgiType>(
             local_header[kHeaderTypeIndex]) != type 
             || (Fcgi_id != id))
          {
            header_error = true;
            break;
          }
          if(content_length == 0)
          {
            sequence_terminated = true;
            break;
          }
          // Set or reset state.
          header_bytes_read = 0;
          state++;
          if(local_offset == number_bytes_read)
            break;
          else
            [[fallthrough]];
        }
        case 1 : {
          if(content_bytes_read < content_length)
          {
            // Safe narrowing as this can never exceed content_length.
            std::uint16_t content_bytes_to_copy(std::min<ssize_t>(
              content_length - content_bytes_read, 
              number_bytes_read - local_offset));
            content_bytes.insert(content_bytes.end(), byte_buffer + local_offset,
              byte_buffer + local_offset + content_bytes_to_copy);
            local_offset += content_bytes_to_copy;
            content_bytes_read += content_bytes_to_copy;
            if(content_bytes_read < content_length)
              break;
          }
          // Set or reset state.
          content_bytes_read = 0;
          state++;
          if(local_offset == number_bytes_read)
            break;
          else
            [[fallthrough]];
        }
        case 2 : {
          if(padding_bytes_read < padding_length)
          {
            // Safe narrowing as this can never exceed padding_length.
            std::uint8_t padding_bytes_to_process(std::min<ssize_t>(
              padding_length - padding_bytes_read, 
              number_bytes_read - local_offset));
            local_offset += padding_bytes_to_process;
            padding_bytes_read += padding_bytes_to_process;
            if(padding_bytes_read < padding_length)
              break;
          }
          padding_bytes_read = 0;
          state = 0;
        }
      }
      if(read_error || header_error || sequence_terminated)
        break;
    }
    if(read_error || header_error || sequence_terminated)
      break;
  }
  // Check for incomplete record sections.
  // Note that, when no error is present and the sequence wasn't terminated
  // by a record with a zero content length, state represents a section which
  // is either incomplete or never began. It is expected that the sequence
  // ends with a header section that "never began."
  bool section_error {false};
  if(!(read_error || header_error || sequence_terminated))
  {
    switch(state) {
      case 0 : {
        if((0 < header_bytes_read) && 
           (header_bytes_read < FCGI_HEADER_LEN))
        {
          section_error = true;
        }
        break;
      }
      case 1 : {
        if(content_bytes_read != content_length)
        {
          section_error = true;
        }
        break;
      }
      case 2 : {
        if(padding_bytes_read != padding_length)
        {
          section_error = true;
        }
        break;
      }
    }
  }

  return std::make_tuple(
    !read_error,
    !(header_error || section_error),
    sequence_terminated,
    (header_error || section_error) ? false : aligned,
    header_count,
    content_bytes
  );
}

void FcgiRequestIdManager::CorruptionCheck()
{
  if(corrupt_)
    throw std::logic_error {"The FcgiRequesIdManager instance was found to "
      "be corrupt."};
}

std::uint16_t FcgiRequestIdManager::GetId()
{
  CorruptionCheck();

  if(available_.size() == 0U)
  {
    if(in_use_.size() == 0U)
    {
      in_use_.insert(1U);
      return 1U;
    }
    std::uint16_t max_in_use {*in_use_.rbegin()};
    if(max_in_use == std::numeric_limits<std::uint16_t>::max())
      throw std::length_error {"All possible request request IDs have "
        "been assigned."};
    std::uint16_t new_id {std::uint16_t(max_in_use + 1)};
    in_use_.insert(in_use_.end(), new_id);
    return new_id;
  }
  else
  {
    std::uint16_t new_id {*available_.begin()};
    std::pair<std::set<std::uint16_t>::iterator, bool> insert_return
      {in_use_.insert(new_id)};
    if(!insert_return.second)
    {
      corrupt_ = true;
      throw std::logic_error {"The state used by the FcgiRequestIdManager "
        "instance to track used IDs was found to be corrupt. An ID was in use "
        "when it was also considered available in a call to GetId."};
    }
    available_.erase(available_.begin());
    return new_id;
  }
}

void FcgiRequestIdManager::ReleaseId(std::uint16_t id)
{
  CorruptionCheck();

  std::set<std::uint16_t>::iterator id_iter {in_use_.find(id)};
  if(id_iter == in_use_.end())
    throw std::out_of_range {"A call to ReleaseId was made for an ID that "
      "was not in use."};
  std::set<std::uint16_t>::reverse_iterator last_id_iter {in_use_.rbegin()};
  if(std::reverse_iterator<std::set<std::uint16_t>::iterator>(id_iter) == 
    last_id_iter)
  {
    std::set<std::uint16_t>::reverse_iterator next_reverse_id_iter 
      {++last_id_iter};
    if(next_reverse_id_iter != in_use_.rend())
    {
      std::uint16_t next_reverse_id {*next_reverse_id_iter};
      if((id - next_reverse_id) > 1)
      {
        std::set<std::uint16_t>::iterator low_iter 
          {available_.find(next_reverse_id + 1)};
        if(low_iter == available_.end())
        {
          corrupt_ = true;
          throw std::logic_error {"The state used by the FcgiRequestIdManager "
            "instance to track available IDs was found to be corrupt. An ID "
            "which should have been present was absent in a call to ReleaseId"};
        }
        available_.erase(low_iter, available_.end());
      }
    }
  }
  else
  {
    available_.insert(id);
  }
  in_use_.erase(id_iter);
}

GTestNonFatalSingleProcessInterfaceAndClients::
GTestNonFatalSingleProcessInterfaceAndClients(
  struct InterfaceCreationArguments inter_args,  int client_number,
  int invocation_line)
: inter_args_ {inter_args}
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestNonFatalSingleProcessInterfaceAndClients::"
    "GTestNonFatalSingleProcessInterfaceAndClients"};

  // Verify interface listening socket parameters.
  if(!((inter_args_.domain == AF_UNIX)    || 
       (inter_args_.domain == AF_INET)    ||
       (inter_args_.domain == AF_INET6)))
  {
    ADD_FAILURE() << "Invalid domain argument in a call to "
      "SingleProcessInterfaceAndClients.";
    throw std::logic_error {""};
  }
  // 92 comes from the lowest known size of sun_path in struct sockaddr_un
  // across distributions. One is added to the length as a terminating
  // null byte must be copied as well.
  if((inter_args_.domain == AF_UNIX) && 
     ((std::strlen(inter_args.unix_path) + 1) > 92))
  {
    ADD_FAILURE() << "The interface path was too long.";
    throw std::logic_error {""};
  }

  try
  {
    inter_tuple_ = GTestNonFatalCreateInterface(inter_args_, __LINE__);
  }
  catch(const std::exception& e)
  {
    ADD_FAILURE() << "A call to CreateInterface threw an exception."
      << '\n' << e.what();
    throw e;
  }
  if(!std::get<0>(inter_tuple_))
  {
    ADD_FAILURE() << "An interface was not constructed by a call to "
      "CreateInterface when constructing an instance of "
      "SingleProcessInterfaceAndClients.";
    throw std::logic_error {""};
  }

  // Prepare the interface address so a client can connect.
  struct sockaddr_un AF_UNIX_interface_address {};
  struct sockaddr_in AF_INET_interface_address {};
  struct sockaddr_in6 AF_INET6_interface_address {};
  if(inter_args_.domain == AF_UNIX)
  {
    AF_UNIX_interface_address.sun_family = AF_UNIX;
    std::strcpy(AF_UNIX_interface_address.sun_path, inter_args.unix_path);
    interface_addr_ptr_ = static_cast<struct sockaddr*>(static_cast<void*>(
      &AF_UNIX_interface_address));
    socket_addr_length_ = sizeof(AF_UNIX_interface_address);
  }
  else if(inter_args_.domain == AF_INET)
  {
    AF_INET_interface_address.sin_family      = AF_INET;
    AF_INET_interface_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    AF_INET_interface_address.sin_port        = std::get<2>(inter_tuple_);
    interface_addr_ptr_ = static_cast<struct sockaddr*>(static_cast<void*>(
      &AF_INET_interface_address));
    socket_addr_length_ = sizeof(AF_INET_interface_address);
  }
  else if(inter_args_.domain == AF_INET6)
  {
    AF_INET6_interface_address.sin6_family = AF_INET6;
    AF_INET6_interface_address.sin6_addr   = in6addr_loopback;
    AF_INET6_interface_address.sin6_port   = std::get<2>(inter_tuple_);
    interface_addr_ptr_ = static_cast<struct sockaddr*>(static_cast<void*>(
      &AF_INET6_interface_address));
    socket_addr_length_ = sizeof(AF_INET6_interface_address);
  }

  try
  {
    client_descriptors_ = std::vector<int>(client_number, -1);
    for(int i {0}; i < client_number; ++i)
    {
      // Create a client socket and make it non-blocking.
      client_descriptors_[i] = socket(inter_args_.domain, SOCK_STREAM, 0);
      if(client_descriptors_[i] == -1)
      {
        ADD_FAILURE() << "A call to socket failed when constructing an instance "
          "of SingleProcessInterfaceAndClients." << '\n'
          << std::strerror(errno);
        throw std::exception {};
      }
      int f_getfl_return {fcntl(client_descriptors_[i], F_GETFL)};
      if(f_getfl_return == -1)
      {
        ADD_FAILURE() << "A call to fcntl with F_GETFL failed when "
          "constructing an instance of SingleProcessInterfaceAndClients." 
          << '\n' << std::strerror(errno);
        throw std::exception {};
      }
      f_getfl_return |= O_NONBLOCK;
      if(fcntl(client_descriptors_[i], F_SETFL, f_getfl_return) == -1)
      {
        ADD_FAILURE() << "A call to fcntl with F_SETFL failed when "
          "constructing an instance of SingleProcessInterfaceAndClients." 
          << '\n' << std::strerror(errno);
        throw std::exception {};
      }

      // Connect the socket to the interface.
      int connect_return {connect(client_descriptors_[i], 
        interface_addr_ptr_, socket_addr_length_)};
      if(connect_return == -1)
      {
        if(((inter_args_.domain == AF_UNIX) && (errno != EAGAIN)) || 
            ((inter_args_.domain != AF_UNIX) && (errno != EINPROGRESS)))
        {
          ADD_FAILURE() << "A call to connect failed with an unexpected "
            "error when constructing an instance of "
            "SingleProcessInterfaceAndClients." << '\n'
            << std::strerror(errno);
          throw std::exception {};
        }
      }

      // Allow the interface to process the connection.
      alarm(1U);
      std::get<0>(inter_tuple_)->AcceptRequests();
      alarm(0U);

      // Ensure connection readiness.
      if(connect_return == -1)
      {
        fd_set descriptor_set {};
        FD_ZERO(&descriptor_set);
        FD_SET(client_descriptors_[i], &descriptor_set);
        // Ensure that select does not block for long --- blocking indicates
        // an error.
        alarm(1U);
        int select_return {select(client_descriptors_[i] + 1, 
          nullptr, &descriptor_set, nullptr, nullptr)};
        alarm(0U);
        if(select_return == -1)
        {
          ADD_FAILURE() << "A call to select failed when constructing an "
            "instance of SingleProcessInterfaceAndClients." << '\n'
            << std::strerror(errno);
          throw std::exception();
        }
        int getsockopt_buffer {};
        socklen_t getsockopt_buffer_length {sizeof(getsockopt_buffer)};
        int getsockopt_return {getsockopt(client_descriptors_[i],
          SOL_SOCKET, SO_ERROR, &getsockopt_buffer, &getsockopt_buffer_length)};
        if((getsockopt_return == -1) || (getsockopt_buffer != 0))
        {
          ADD_FAILURE() << "A call to getsocket either failed or returned "
            " a failed connection status after a call to select when "
            "constructing an instance of "
            "SingleProcessInterfaceAndClients." 
            << '\n' << std::strerror(errno);
          throw std::exception();
        }
      }
    }
  }
  catch(const std::exception& e)
  {
    // Cleanup state before the exception leaves the constructor.
    ADD_FAILURE() << "An exception was thrown when constructing an instance "
      "of SingleProcessInterfaceAndClients." << '\n' << e.what();
    CleanUp(__LINE__);
    throw e;
  }
}

void GTestNonFatalSingleProcessInterfaceAndClients::
CleanUp(int invocation_line)
{
  ::testing::ScopedTrace tracer {__FILE__, invocation_line,
    "GTestNonFatalSingleProcessInterfaceAndClients::CleanUp"};

  // Cleanup interface state.
  if(std::get<0>(inter_tuple_))
  {
    close(std::get<1>(inter_tuple_));
    if(inter_args_.domain == AF_UNIX)
    {
      if(unlink(inter_args_.unix_path) == -1)
        ADD_FAILURE() << "A call to unlink encountered an error when " 
          "destroying an instance of SingleProcessInterfaceAndClients."
          << '\n' << std::strerror(errno);
    }
  }
  // Cleanup client state.
  for(int descriptor : client_descriptors_)
  {
    if(descriptor >= 0)
      if(close(descriptor) == -1)
        ADD_FAILURE() << "A call to close on a client descriptor failed "
          "when destroying an instance of "
          "SingleProcessInterfaceAndClients." << '\n' 
          << std::strerror(errno);
  }
}

} // namespace test
} // namespace fcgi
} // namespace as_components
