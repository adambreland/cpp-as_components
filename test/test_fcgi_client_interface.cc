#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

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
#include <utility>

#include "external/id_manager/include/id_manager_template.h"
#include "external/socket_functions/include/socket_functions.h"

#include "test/test_fcgi_client_interface.h"
#include "include/utility.h"

namespace fcgi_si_test {

// Invariants and properties of completed_request_set_, connection_map_, and
// pending_request_map_.
// 1) If a RequestIdentifier instance ri is present in completed_request_set_
//    or pending_request_map_, then an entry for ri.descriptor() must be
//    present in connection_map_.
// 2) If a connection as represented by a ConnectionState instance c of
//    connection_map_ is not connected (c.connected == false), then no requests
//    which are associated with the connection may be present in
//    pending_request_map_.
// 3) If a connection as represented by a ConnectionState instance c of
//    connection_map_ is not connected, then there must exist
//    completed-but-unreleased requests in completed_request_set_ which are
//    associated with the connection of c.
//    (In other words, if a connection is not connected and the last completed-
//    but-unreleased request is released, then the connection must be removed
//    from connection_map_.)
// 4) completed_request_set_ and pending_request_map_ have disjoint sets of
//    keys.
// 5) The set of FCGI_id values of the id_manager instance for a connection of
//    a ConnectionState instance c of connection_map_ is identical to the union
//    of the sets of FCGI_id values of the RequestIdentifier instances which
//    are associated with the connection of c and which are derived from
//    completed_request_set_ and pending_request_map_.
// 6) The functions of the ReleaseId overload set can only release completed-
//    but-unreleased requests.
//    a) Pending requests are cancelled by either closing the connection on
//       which the request was made (which cancels all pending requests on the
//       connection) or by calling SendAbortRequest and waiting for a response
//       from the server about the aborted request.

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

  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_start
    {pending_request_map_.lower_bound({connection, 0U})};
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_end
    {(connection < std::numeric_limits<int>::max()) ?
      pending_request_map_.lower_bound({connection + 1, 0U}) :
      pending_request_map_.end()};

  // Determine if the item in connection_map_ should be erased. When request
  // IDs for completed requests are present, the item should not be erased.
  std::set<fcgi_si::RequestIdentifier>::iterator completed_start
    {completed_request_set_.lower_bound({connection, 0U})};
  // Are completed-but-unreleased requests present?
  if((completed_start != completed_request_set_.end()) &&
     (completed_start->descriptor() == connection))
  {
    // Perform actions which may throw.
    std::list<ManagementRequestData> empty_queue {};
    // Check that each ID which will be released is being tracked by the
    // id_manager.
    a_component::IdManager<std::uint16_t>* id_manager_ptr
      {&(state_ptr->id_manager)};
    for(std::map<fcgi_si::RequestIdentifier, RequestData>::iterator
      pending_start_copy {pending_start};
      pending_start_copy != pending_end;
      ++pending_start_copy)
    {
      if(!(id_manager_ptr->IsUsed(pending_start_copy->first.Fcgi_id())))
      {
        throw std::logic_error {"A discrepancy between stored request IDs was "
          "found during a call to TestFcgiClientInterface::CloseConnection."};
      }
    }

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
    // It is assumed that ReleaseId will not throw as the check above
    // succeeded.
    while(pending_start != pending_end)
    {
      id_manager_ptr->ReleaseId(pending_start->first.Fcgi_id());
      std::map<fcgi_si::RequestIdentifier, RequestData>::iterator safe_eraser
        {pending_start};
      ++pending_start;
      pending_request_map_.erase(safe_eraser);
    }
  }
  else // No completed-but-unreleased requests are present for connection.
  {
    connection_map_.erase(connection_iter);
    pending_request_map_.erase(pending_start, pending_end);
  }


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
      // An error occurred. See if connection can be retried.
      close(socket_connection);
      if(errno == EINTR)
      {
        continue;
      }
      else if((errno == ECONNREFUSED) || (errno == EACCES) ||
        (errno == ETIMEDOUT))
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
  // Make the descriptor non-blocking for later I/O multiplexing.
  auto CloseAndThrowOnError = [socket_connection](int error, const char* message)->void
  {
    std::error_code ec {error, std::system_category()};
    std::system_error se {ec, message};
    close(socket_connection);
    throw se;
  };
  int flags {fcntl(socket_connection, F_GETFL)};
  if(flags == -1)
  {
    CloseAndThrowOnError(errno, "fcntl with F_GETFL");
  }
  flags |= O_NONBLOCK;
  if(fcntl(socket_connection, F_SETFL, flags) == -1)
  {
    CloseAndThrowOnError(errno, "fcntl with F_SETFL");
  }

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
        throw std::logic_error {"In a call to "
          "TestFcgiClientInterface::Connect, a connection was made on a file "
          "descriptor which was already considered to be connected."};
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

// std::unique_ptr<ServerEvent> TestFcgiClientInterface::RetrieveServerEvent()
// {

// }

std::pair<const int, TestFcgiClientInterface::ConnectionState>*
TestFcgiClientInterface::ConnectedCheck(int connection)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {connection_map_.find(connection)};
  if(connection_iter == connection_map_.end())
  {
    return nullptr;
  }
  if(!(connection_iter->second.connected))
  {
    return nullptr;
  }
  return &(*connection_iter);
}

bool TestFcgiClientInterface::ReleaseId(fcgi_si::RequestIdentifier id)
{
  int connection {id.descriptor()};
  std::map<int, ConnectionState>::iterator connection_iter
    {connection_map_.find(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  else
  {
    std::set<fcgi_si::RequestIdentifier>::iterator completed_end
      {completed_request_set_.end()};
    a_component::IdManager<std::uint16_t>* id_manager_ptr
      {&(connection_iter->second.id_manager)};
    std::uint16_t fcgi_id {id.Fcgi_id()};

    bool pending {pending_request_map_.find(id) != pending_request_map_.end()};
    std::set<fcgi_si::RequestIdentifier>::iterator completed_iter
        {completed_request_set_.find(id)};
    bool completed {completed_iter != completed_end};
    bool used {id_manager_ptr->IsUsed(fcgi_id)};

    // Once a connection entry is known to exist, of the eight permutations of
    // the above boolean values, only three do not imply logic errors.
    if((!used && !pending && !completed) || (used && pending && !completed))
    {
      return false;
    }
    else if(used && !pending && completed)
    {
      // The request must be removed from completed_request_set_.
      // If the connection is not connected and the request which will be
      // removed is the only completed-but-unreleased request, then the
      // entry for the connection in connection_map_ must be removed.
      bool more_before {false};
      if(completed_iter != completed_request_set_.begin())
      {
        std::set<fcgi_si::RequestIdentifier>::iterator previous_completed
          {completed_iter};
        --previous_completed;
        if(previous_completed->descriptor() == connection)
        {
          more_before = true;
        }
      }
      std::set<fcgi_si::RequestIdentifier>::iterator next_completed
        {completed_iter};
      next_completed++;
      bool more_after {(next_completed != completed_end) ?
        (next_completed->descriptor() == connection) : false};
      bool only_one {!more_before && !more_after};

      if(only_one && !(connection_iter->second.connected))
      {
        connection_map_.erase(connection_iter);
      }
      else
      {
        id_manager_ptr->ReleaseId(fcgi_id);
      }
      completed_request_set_.erase(completed_iter);
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
  bool connected {connection_iter->second.connected};
  a_component::IdManager<std::uint16_t>* id_manager_ptr
    {&(connection_iter->second.id_manager)};
  std::set<fcgi_si::RequestIdentifier>::iterator start
    {completed_request_set_.lower_bound({connection, 0U})};
  std::set<fcgi_si::RequestIdentifier>::iterator end
    {(connection < std::numeric_limits<int>::max()) ?
      completed_request_set_.lower_bound({connection + 1, 0U}) :
      completed_request_set_.end()};
  // Perform actions on connection_map_ here. Actions on completed_request_set_
  // are performed below.
  if(connected)
  {
    std::vector<std::uint16_t> fcgi_id_cache {};
    // Ensure that each completed request is present in the id_manager.
    //
    // Absence of a completed requests in the pending map is not verified.
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
    // It is assumed that ReleaseId won't throw because of the success of the
    // previous check.
    for(std::vector<std::uint16_t>::iterator i {fcgi_id_cache.begin()};
      i != fcgi_id_cache.end(); ++i)
    {
      id_manager_ptr->ReleaseId(*i);
    }
  }
  else
  {
    // If no completed-but-unreleased requests are present in
    // completed_request_set_, an invariant has been violated.
    //
    // The absence of any request in pending_request_map_ for connection is not
    // verified.
    if(start == end)
    {
      throw std::logic_error {"In a call to "
        "TestFcgiClientInterface::ReleaseId(int), a disconnected socket "
        "descriptor was found for which no completed-but-unreleased requests "
        "were present."};
    }
    connection_map_.erase(connection_iter);
  }
  completed_request_set_.erase(start, end);
  return true;
}

bool TestFcgiClientInterface::SendAbortRequest(fcgi_si::RequestIdentifier id)
{
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_iter
    {pending_request_map_.find(id)};
  if(pending_iter == pending_request_map_.end())
  {
    return false;
  }
  std::uint8_t abort_header[fcgi_si::FCGI_HEADER_LEN] = {};
  fcgi_si::PopulateHeader(abort_header, fcgi_si::FcgiType::kFCGI_ABORT_REQUEST,
    id.Fcgi_id(), 0U, 0U);
  int connection {id.descriptor()};
  std::size_t write_return {socket_functions::WriteOnSelect(connection,
    abort_header, fcgi_si::FCGI_HEADER_LEN, nullptr)};
  if(write_return < fcgi_si::FCGI_HEADER_LEN)
  {
    // Either an error occurred or the server closed the connection. If nothing
    // was written, there is the possibility to recover from the error.
    // If some data was written, the connection is regarded as corrupted.
    //
    // As an exception from CloseConnection leaves the interface in an
    // unspecified state, CloseConnection is treated as noexcept here.
    int previous_errno {errno};
    std::error_code ec {errno, std::system_category()};
    std::system_error se {ec, "write"};
    if(write_return == 0U)
    {
      throw se;
    }
    else
    {
      try // noexcept-equivalent block.
      {
        // It is assumed that connection is associated with an entry in
        // connection_map_ and is connected. Since a pending request was
        // present for id, if this was not true, then an invariant of the
        // class would have been violated.
        //
        // It is then assumed that CloseConnection(connection) either returns
        // true or throws.
        CloseConnection(connection);
        micro_event_queue_.push_back(std::unique_ptr<ServerEvent>
          {new ConnectionClosure {connection}});
      }
      catch(...)
      {
        std::terminate();
      }
      if(previous_errno == EPIPE)
      {
        return false;
      }
      else
      {
        throw se;
      }
    }
  }
  return true;
}

bool TestFcgiClientInterface::SendBinaryManagementRequestHelper(int connection,
  fcgi_si::FcgiType type, ManagementRequestData&& queue_item)
{
  std::size_t length {queue_item.data.size()};
  if(length > static_cast<std::size_t>(fcgi_si::kMaxRecordContentByteLength))
  {
    return false;
  }
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

  state_ptr->management_queue.push_back(std::move(queue_item));

  std::uint8_t padding[7] = {}; // Aggregate initialize to zeroes.
  // The literal value 8 comes from the alignment recommendation of the FastCGI
  // protocol.
  std::uint8_t mod_length {static_cast<std::uint8_t>(length % 8)};
  std::uint8_t padding_length {static_cast<std::uint8_t>(
    (mod_length) ? (8 - mod_length) : 0U)};
  std::uint8_t header[fcgi_si::FCGI_HEADER_LEN] = {};
  fcgi_si::PopulateHeader(header, type, 0U, length, padding_length);
  constexpr int iovec_count {3};
  struct iovec iovec_array[iovec_count] = {};
  iovec_array[0].iov_base = header;
  iovec_array[0].iov_len  = fcgi_si::FCGI_HEADER_LEN;
  iovec_array[1].iov_base = state_ptr->management_queue.back().data.data();
  iovec_array[1].iov_len  = length;
  iovec_array[2].iov_base = padding;
  iovec_array[2].iov_len  = padding_length;
  std::size_t number_to_write {fcgi_si::FCGI_HEADER_LEN + length +
    padding_length};

  return SendManagementRequestHelper(connection, state_ptr, iovec_array,
    iovec_count, number_to_write);
}

bool TestFcgiClientInterface::SendGetValuesRequestHelper(
  std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr,
  ManagementRequestData&& queue_item)
{
  // Responsibilities:
  // 1) Check that a single FCGI_GET_VALUES record, which is an FCGI_GET_VALUES
  //    management request, will be used to encode the request.
  // 2) Add queue_item to the appropriate management request queue.
  // 3) Call SendManagementRequestHelper and return its return value.



  // std::tuple<bool, std::size_t, std::vector<struct iovec>,
  //   std::vector<std::uint8_t>, std::size_t, MapNameIterator> encode_return
  //   {}

  // state_ptr->management_queue.push_back(std::move(queue_item));

}

bool TestFcgiClientInterface::SendManagementRequestHelper(int connection,
  ConnectionState* state_ptr, struct iovec iovec_array[], int iovec_count,
  std::size_t number_to_write)
{
  std::tuple<struct iovec*, int, std::size_t> write_return
    {socket_functions::ScatterGatherSocketWrite(connection, iovec_array,
      iovec_count, number_to_write, true, nullptr)};
  std::size_t number_remaining {std::get<2>(write_return)};
  if(number_remaining != 0U)
  {
    // An error occurred. Either the server closed the connection or
    // a local error occurred.
    // 1) In all cases, the item which was added to the queue should be
    //    removed.
    // 2) If nothing was written and the server did not close the connection,
    //    then recovery may be possible. In this case, the entry for connection
    //    in connection_map_ does not need to be removed.
    // 3) If something was written or the server did close the connection,
    //    then the entry for connection must be removed from connection_map_.
    std::error_code ec {errno, std::system_category()};
    std::system_error se {ec, "write or select"};
    try
    {
      state_ptr->management_queue.pop_back();
      if(!((number_remaining == number_to_write) &&
           (se.code().value() != EPIPE)))
      {
        // Some data was written; the connection is now corrupt;
        CloseConnection(connection);
        micro_event_queue_.push_back(std::unique_ptr<ServerEvent>
          {new ConnectionClosure {connection}});
      }
    }
    catch(...)
    {
      std::terminate();
    }
    if(se.code().value() == EPIPE)
    {
      return false;
    }
    else
    {
      throw se;
    }
  }
  return true;
}

// fcgi_si::RequestIdentifier
// TestFcgiClientInterface::SendRequest(int connection, const FcgiRequest& request)
// {

// }

// fcgi_si::RequestIdentifier
// TestFcgiClientInterface::SendRequest(int connection, FcgiRequest&& request)
// {

// }

} // namespace fcgi_si_test
