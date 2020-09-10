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
#include <limits>
#include <map>
#include <system_error>
#include <type_traits>

#include "test/test_fcgi_client_interface.h"

namespace fcgi_si_test {

bool TestFcgiClientInterface::CloseConnection(int connection)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {connection_map_.find(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  ConnectionState* state_ptr {&(connection_iter->second)};
  if(!(state_ptr->connected))
  {
    return false;
  }

  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator start
    {request_map_.lower_bound({connection, 0U})};
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator end
    {(connection < std::numeric_limits<int>::max()) ?
      request_map_.lower_bound({connection + 1, 0U}) :
      request_map_.end()};

  // Determine if the item in connection_map_ should be erased. When IDs are
  // present, the ConnectionState instance should not be erased.
  if(state_ptr->id_manager.NumberUsedIds())
  {
    // Perform actions which may throw.
    std::queue<ManagementRequestData, std::list<ManagementRequestData>>
      empty_queue {};

    // Update state using calls which can't throw.
    state_ptr->record_state.header_bytes_received  = 0U;
    state_ptr->record_state.content_bytes_expected = 0U;
    state_ptr->record_state.content_bytes_received = 0U;
    state_ptr->record_state.padding_bytes_expected = 0U;
    state_ptr->record_state.padding_bytes_received = 0U;
    state_ptr->record_state.local_buffer.clear();
    // Ensure that queue swap is noexcept.
    static_assert(
      std::is_nothrow_swappable<std::list<ManagementRequestData>>::value ==
      true
    );
    state_ptr->management_queue.swap(empty_queue);
    state_ptr->connected = false;
    // Erasing pending requests requires releasing the RequestIdentifier
    // values which are associated with the requests from id_manager.
    //
    // ReleaseId may throw. If this occurs, a logic error in
    // TestFcgiClientInterface has occurred which prevents a known state
    // fromeasily being specified for recovery. Termination solves this problem.
    try
    {
      for(std::map<fcgi_si::RequestIdentifier, RequestData>::iterator
        start_copy {start}; start_copy != end; ++start_copy)
      {
        state_ptr->id_manager.ReleaseId(start_copy->first.Fcgi_id());
      }
    }
    catch(...)
    {
      std::terminate();
    }
  }
  else
  {
    connection_map_.erase(connection_iter);
  }
  request_map_.erase(start, end);

  if((close(connection) == -1) && (errno != EINTR))
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "close"};
  }
  return true;
}

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
      // Failure of close shouldn't be relevant.
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
  // Pair construction could throw. Insertion could throw.
  try
  {
    // TestFcgiClientInterface allows requests represented by unique
    // RequestIdentifier values to outlive the connection on which they
    // originated. If a connection is closed and the descriptor value of the
    // connection is used for a new connection, unreleased requests on the
    // previous connection must be accunted for when new RequestIdentifier
    // values are chosen. Persisting ConnectionState instances across
    // instances of "connected == true" state allows this through persistence
    // of id_manager instances.
    std::map<int, ConnectionState>::iterator search_iter
      {connection_map_.find(socket_connection)};
    if(search_iter != connection_map_.end())
    {
      bool* conn_ptr {&(search_iter->second.connected)};
      if(*conn_ptr)
      {
        throw std::logic_error {"A connection was made on a file descriptor "
          "which was already connected according to TestFcgiClientInterface."};
      }
      else
      {
        *conn_ptr = true;
        // When a connection is closed and the item in connection_map_ for the
        // connection is not removed, the closure operation should cause the
        // ConnectionState instance of the item to be updated so that, except
        // for the state of id_manager, it has the state of a newly-constructed
        // instance (connected == false, record_state == RecordState {},
        // management_queue.size() == 0U).
      }
    }
    else // A new map item is needed.
    {
      ConnectionState connection_state {};
      std::pair<std::map<int, ConnectionState>::iterator, bool> insert_return
        {connection_map_.insert({socket_connection, std::move(connection_state)})};
      // Set connected to true after insertion to simplify exception handling.
      // No undo step is needed in the event of an exception when this is done.
      insert_return.first->second.connected = true;
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
