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
#include <set>
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
    {pending_request_map_.lower_bound({connection, 0U})};
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator end
    {(connection < std::numeric_limits<int>::max()) ?
      pending_request_map_.lower_bound({connection + 1, 0U}) :
      pending_request_map_.end()};

  // Determine if the item in connection_map_ should be erased. When request
  // IDs for completed requests are present, the item should not be erased.
  if(completed_request_map_.size())
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
    // The erasure of pending requests requires releasing the RequestIdentifier
    // values which are associated with these requests from id_manager.
    //
    // ReleaseId may throw. If this occurs, a logic error in
    // TestFcgiClientInterface has occurred which prevents a known state
    // from easily being specified for recovery. Termination solves this
    // problem.
    try // noexcept-equivalent block
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
  pending_request_map_.erase(start, end);

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
    char* path_ptr {
      static_cast<struct sockaddr_un*>(
        static_cast<void*>(
          &addr_store))
      ->sun_path
    };
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
          "which was already connected in a call to "
          "TestFcgiClientInterface::Connect."};
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

// std::vector<std::unique_ptr<ServerEvent>>
// TestFcgiClientInterface::ReceiveResponses()
// {

// }

bool TestFcgiClientInterface::ReleaseId(fcgi_si::RequestIdentifier id)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {connection_map_.find(id.descriptor())};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  else
  {
    a_component::IdManager* id_manager_ptr
      {&(connection_iter->second.id_manager)};
    std::uint16_t fcgi_id {id.Fcgi_id()};

    bool pending {pending_request_map_.find(id) != pending_request_map_.end()};
    std::set<fcgi_si::RequestIdentifier>::iterator completed_iter
        {completed_request_map_.find(id)};
    bool completed {completed_iter != completed_request_map_.end()};
    bool used {id_manager_ptr->IsUsed(fcgi_id)};

    // Once a connection entry is known to exist, of the eight permutations of
    // the above boolean values, only three do not imply logic errors.
    if((!used && !pending && !completed) || (used && pending && !completed))
    {
      return false;
    }
    else if(used && !pending && completed)
    {
      id_manager_ptr->ReleaseId(fcgi_id);
      completed_request_map_.erase(completed_iter);
      return true;
    }
    else
    {
      throw std::logic_error {"A discrepancy between stored request IDs was "
        "found in an instance of TestFcgiClientInterface in a call to "
        "ReleaseId."};
    }
  }
}

bool TestFcgiClientInterface::ReleaseId(int connection)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {connection_map_.find(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  a_component::IdManager* id_manager_ptr
    {&(connection_iter->second.id_manager)};
  std::set<fcgi_si::RequestIdentifier>::iterator start
    {completed_request_map_.lower_bound({connection, 0U})};
  std::set<fcgi_si::RequestIdentifier>::iterator end
    {(connection < std::numeric_limits<int>::max()) ?
      completed_request_map_.lower_bound({connection + 1, 0U}) :
      completed_request_map_.end()};
  std::vector<std::uint16_t> fcgi_id_cache {};
  // Ensure that each completed request is present in the id_manager.
  //
  // Occurrence of a completed request in the pending map is not checked.
  for(std::set<fcgi_si::RequestIdentifier>::iterator
    start_copy {start}; start_copy != end; ++start_copy)
  {
    std::uint16_t local_id {start_copy->Fcgi_id()};
    fcgi_id_cache.push_back(local_id);
    if(!(id_manager_ptr->IsUsed(local_id)))
    {
      throw std::logic_error {"A completed and unreleased request was not "
        "present in the appropriate IdManager instance in a call to "
        "TestFcgiClientInterface::ReleaseId."};
    }
  }
  for(std::vector<std::uint16_t>::iterator i {fcgi_id_cache.begin()};
    i != fcgi_id_cache.end(); ++i)
  {
    id_manager_ptr->ReleaseId(*i);
  }
  completed_request_map_.erase(start, end);
  return true;
}

// bool TestFcgiClientInterface::SendAbortRequest(fcgi_si::RequestIdentifier)
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

// bool TestFcgiClientInterface::SendGetValuesRequest(int connection,
//   const ParamsMap& params_map)
// {

// }

// bool TestFcgiClientInterface::SendGetValuesRequest(int connection,
//   ParamsMap&& params_map)
// {

// }

// fcgi_si::RequestIdentifier
// TestFcgiClientInterface::SendRequest(int connection, const FcgiRequest& request)
// {

// }

// fcgi_si::RequestIdentifier
// TestFcgiClientInterface::SendRequest(int connection, FcgiRequest&& request)
// {

// }

} // namespace fcgi_si_test
