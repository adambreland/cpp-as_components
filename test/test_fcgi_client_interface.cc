#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <system_error>

#include "test/test_fcgi_client_interface.h"

namespace fcgi_si_test {

int TestFcgiClientInterface::Connect(const char* address, std::uint16_t port)
{
  if(address == nullptr)
  {
    errno = EINVAL;
    return -1;
  }

  // Determine the domain. Try Ipv4 and then IPv6. If neither, assume Unix.
  // Initialize the server address appropriately for the determined domain.
  int domain {};
  struct sockaddr_storage addr_store {};
  if(inet_pton(AF_INET, address, &addr_store) > 0)
  {
    domain = AF_INET;
    static_cast<struct sockaddr_in*>(static_cast<void*>(&addr_store))->sin_port
      = htons(port);
  }
  else if(inet_pton(AF_INET6, address, &addr_store) > 0)
  {
    domain = AF_INET6;
    static_cast<struct sockaddr_in6*>(static_cast<void*>(&addr_store))->sin6_port
      = htons(port);
  }
  else
  {
    domain = AF_UNIX;
    std::size_t address_length {std::strlen(address)};
    // The value 91 comes from the current portable limit for UNIX address
    // lengths (where one byte is saved for the null terminator).
    if((address_length == 0) || (address_length > 91))
    {
      errno = EINVAL;
      return -1;
    }
    char* path_ptr {static_cast<struct sockaddr_un*>(static_cast<void*>(&addr_store))->sun_path};
    std::strcpy(path_ptr, address); // Include null byte.
  }

  // Create a socket and try to connect to the server.
  int socket_connection {};
  while(true)
  {
    socket_connection = socket(domain, SOCK_STREAM, 0);
    if(socket_connection == -1)
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "socket"};
    }
    if(connect(socket_connection, 
      static_cast<const struct sockaddr*>(static_cast<void*>(&addr_store)),
      sizeof(struct sockaddr_storage)) == -1)
    {
      close(socket_connection);
      if(errno == EINTR)
      {
        continue;
      }
      else if((errno == ECONNREFUSED) || (errno == EACCES))
      {
        return -1;
      }
      else
      {
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "connect"};
      }
    }
    break;
  }
  // socket_connection must now refer to a connected socket descriptor.

  // Update internal state.
  try
  {
    // Constructing the pair could throw. Insertion could throw.
    ConnectionState connection_state {};
    connection_state.connected = true;
    bool insertion_status {connection_map_.insert({socket_connection, 
      std::move(connection_state)}).second};
    if(!insertion_status) // A defensive check for descriptor management errors.
    {
      throw std::logic_error {"A connection was made on a file descriptor "
        "which was already tracked by TestFcgiClientInterface."};
    }
  }
  catch(...)
  {
    close(socket_connection);
    throw;
  }
  
  return socket_connection;
}

// bool TestFcgiClientInterface::SendGetValuesRequest(int connection, 
//   const ParamsMap& params_map)
// {

// }

// bool TestFcgiClientInterface::SendGetValuesRequest(int connection, 
//   ParamsMap&& params_map)
// {

// }

// bool TestFcgiClientInterface::SendBinaryManagementRequest(int connection, 
//   fcgi_si::FcgiType type, const std::uint8_t* byte_ptr, std::size_t length)
// {

// }

// bool TestFcgiClientInterface::SendBinaryManagementRequest(int connection,
//   fcgi_si::FcgiType type, std::vector<std::uint8_t>&& data)
// {

// }

} // namespace fcgi_si_test
