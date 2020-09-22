#include "test/test_fcgi_client_interface.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/uio.h>
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

#include "include/protocol_constants.h"
#include "include/request_identifier.h"
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
    static_cast<struct sockaddr_in6*>(
      static_cast<void*>(&addr_store))->sin6_port = htons(port);
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
  auto CloseAndThrowOnError = [socket_connection]
  (int error, const char* message)->void
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
        {connection_map_.insert(
          {socket_connection, std::move(connection_state)}
        )};
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

void TestFcgiClientInterface::FailedWrite(
  std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr,
  int error_code,
  bool nothing_written,
  bool pop_management_queue,
  const char* system_error_message
)
{
  // An error occurred. Either the server closed the connection or
  // a local error occurred.
  // 1) In all cases, the item which was added to the queue should be
  //    removed.
  // 2) If nothing was written and the server did not close the connection,
  //    then recovery may be possible. In this case, CloseConnection
  //    does not need to be called.
  // 3) If something was written or the server did close the connection,
  //    then the entry for connection must be removed from connection_map_.
  //    CloseConnection should be called.
  try
  {
    if(pop_management_queue)
    {
      entry_ptr->second.management_queue.pop_back();
    }
    if(!(nothing_written && (error_code != EPIPE)))
    {
      CloseConnection(entry_ptr->first);
      micro_event_queue_.push_back(std::unique_ptr<ServerEvent>
        {new ConnectionClosure {entry_ptr->first}});
    }
  }
  catch(...)
  {
    std::terminate();
  }
  if(error_code != EPIPE)
  {
    std::error_code ec {error_code, std::system_category()};
    throw std::system_error {ec, system_error_message};
  }
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
  std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr
    {ConnectedCheck(id.descriptor())};
  if(!entry_ptr)
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
    FailedWrite(entry_ptr, errno, write_return == 0U, false, write_or_select_);
    return false;
  }
  return true;
}

bool TestFcgiClientInterface::SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, const std::uint8_t* byte_ptr, std::size_t length)
{
  std::pair<const int, ConnectionState>* entry_ptr {ConnectedCheck(connection)};
  if(!entry_ptr)
  {
    return false;
  }
  std::vector<std::uint8_t> local_data(byte_ptr, byte_ptr + length);
  ManagementRequestData queue_item {type, {}, std::move(local_data)};
  return SendBinaryManagementRequestHelper(entry_ptr, type,
    std::move(queue_item));
}

bool TestFcgiClientInterface::SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, std::vector<std::uint8_t>&& data)
{
  std::pair<const int, ConnectionState>* entry_ptr {ConnectedCheck(connection)};
  if(!entry_ptr)
  {
    return false;
  }
  ManagementRequestData queue_item {type, {}, std::move(data)};
  return SendBinaryManagementRequestHelper(entry_ptr, type,
    std::move(queue_item));
}

bool TestFcgiClientInterface::SendBinaryManagementRequestHelper(
  std::pair<const int, ConnectionState>* entry_ptr,
  fcgi_si::FcgiType type, ManagementRequestData&& queue_item)
{
  std::size_t length {queue_item.data.size()};
  if(length > static_cast<std::size_t>(fcgi_si::kMaxRecordContentByteLength))
  {
    return false;
  }
  std::uint8_t* data_ptr {queue_item.data.data()};

  // This action will need to be undone by SendManagementRequestHelper if
  // and exception will be thrown.
  entry_ptr->second.management_queue.push_back(std::move(queue_item));

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
  iovec_array[1].iov_base = data_ptr;
  iovec_array[1].iov_len  = length;
  iovec_array[2].iov_base = padding;
  iovec_array[2].iov_len  = padding_length;
  std::size_t number_to_write {fcgi_si::FCGI_HEADER_LEN + length +
    padding_length};

  return SendManagementRequestHelper(entry_ptr, iovec_array,
    iovec_count, number_to_write);
}

bool TestFcgiClientInterface::SendGetValuesRequest(int connection,
  const ParamsMap& params_map)
{
  std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr
    {ConnectedCheck(connection)};
  if(!entry_ptr)
  {
    return false;
  }
  ParamsMap new_map {};
  // All values are supposed to be empty. This is ensured.
  for(ParamsMap::const_iterator i {params_map.begin()}; i != params_map.end();
    ++i)
  {
    new_map.insert({i->first, {}});
  }
  return SendGetValuesRequestHelper(entry_ptr,
    {fcgi_si::FcgiType::kFCGI_GET_VALUES, std::move(new_map), {}});
}

bool TestFcgiClientInterface::SendGetValuesRequest(int connection,
  ParamsMap&& params_map)
{
  std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr
    {ConnectedCheck(connection)};
  if(!entry_ptr)
  {
    return false;
  }
  // All values are supposed to be empty. This is ensured.
  for(ParamsMap::iterator i {params_map.begin()}; i != params_map.end();
    ++i)
  {
    i->second.clear();
  }
  return SendGetValuesRequestHelper(entry_ptr,
    {fcgi_si::FcgiType::kFCGI_GET_VALUES, std::move(params_map), {}});
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

  std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
    std::vector<std::uint8_t>, std::size_t, ParamsMap::iterator> encode_return
    {fcgi_si::EncodeNameValuePairs(queue_item.params_map.begin(),
       queue_item.params_map.end(), queue_item.type, 0U, 0U)};
  
  if(fcgi_si::EncodeNVPairSingleRecordFailure(encode_return,
    queue_item.params_map.end()))
  {
    return false;
  }
  
  entry_ptr->second.management_queue.push_back(std::move(queue_item));

  return SendManagementRequestHelper(entry_ptr,
    std::get<2>(encode_return).data(), std::get<2>(encode_return).size(),
    std::get<1>(encode_return));
}

bool TestFcgiClientInterface::SendManagementRequestHelper(
  std::pair<const int, TestFcgiClientInterface::ConnectionState>* entry_ptr,
  struct iovec iovec_array[], int iovec_count, std::size_t number_to_write)
{
  std::tuple<struct iovec*, int, std::size_t> write_return
    {socket_functions::ScatterGatherSocketWrite(entry_ptr->first, iovec_array,
      iovec_count, number_to_write, true, nullptr)};
  std::size_t number_remaining {std::get<2>(write_return)};
  if(number_remaining != 0U)
  {
    FailedWrite(entry_ptr, errno, number_remaining == number_to_write, true,
      write_or_select_);
    return false;
  }
  return true;
}

fcgi_si::RequestIdentifier
TestFcgiClientInterface::SendRequest(int connection, const FcgiRequest& request)
{
  std::pair<const int, ConnectionState>* entry_ptr {ConnectedCheck(connection)};
  if(!entry_ptr)
  {
    return fcgi_si::RequestIdentifier {};
  }
  ConnectionState* state_ptr       {&(entry_ptr->second)};
  std::uint16_t    new_id          {state_ptr->id_manager.GetId()};
  bool             write_error     {false};
  bool             nothing_written {true};
  int              saved_errno     {0};
  std::uint16_t    role            {request.role};
  // Two kinds of errors are handled below:
  // 1) Errors from writes to the server which cannot throw exceptions.
  // 2) Errors which are represented as exceptions.
  try
  {
    do
    {
      auto SocketWriteHelper = [&]
      (
        std::uint8_t* byte_ptr, 
        std::size_t   write_length
      )->bool
      {
        std::size_t write_return {socket_functions::SocketWrite(connection,
          byte_ptr, write_length)};
        if(write_return < write_length)
        {
          write_error = true;
          if(write_return)
          {
            nothing_written = false;
          }
          saved_errno = errno;
          return false;
        }
        return true;
      };

      constexpr int_fast32_t begin_length {2U * fcgi_si::FCGI_HEADER_LEN};
      std::uint8_t begin_record[begin_length] = {};
      fcgi_si::PopulateBeginRequestRecord(begin_record, new_id, role,
        request.keep_conn);
      if(!SocketWriteHelper(begin_record, begin_length))
      {
        break;
      }
      nothing_written = false;

      // Capturing all variables up to this point by reference is simpler than
      // specifying a parameter list.
      auto DataAndStdinWriter = [&](fcgi_si::FcgiType type)->bool
      {
        bool is_data {type == fcgi_si::FcgiType::kFCGI_DATA};
        // PartitionByteSequence will produce ending stream records if
        // begin_iter == end_iter. terminated is used to ensure that one
        // ending record is always produced.
        bool terminated
          {((is_data) ? request.data_length : request.stdin_length) == 0U};
        const std::uint8_t* start_iter
          {(is_data) ? request.fcgi_data_ptr : request.fcgi_stdin_ptr};
        const std::uint8_t* end_iter 
          {start_iter + ((is_data) ? 
            request.data_length : request.stdin_length)};
        do
        {
          std::tuple<std::vector<std::uint8_t>, std::vector<struct iovec>,
            std::size_t, const std::uint8_t*> partition 
            {fcgi_si::PartitionByteSequence(start_iter, end_iter,
              type, new_id)};
          std::tuple<struct iovec*, int, std::size_t> partition_write
            {socket_functions::ScatterGatherSocketWrite(connection,
              std::get<1>(partition).data(),
              std::get<1>(partition).size(), std::get<2>(partition),
              false, nullptr)};
          if(std::get<2>(partition_write))
          {
            write_error = true;
            saved_errno = errno;
            return false;
          }
          start_iter = std::get<3>(partition);
          if(!terminated && (start_iter == end_iter))
          {
            terminated = true;
            continue;
          }
        } while(start_iter != end_iter);
        return true;
      };

      if(!(((role == fcgi_si::FCGI_RESPONDER) ||
            (role == fcgi_si::FCGI_AUTHORIZER))  && 
          (request.data_length == 0U)))
      {
        if(!DataAndStdinWriter(fcgi_si::FcgiType::kFCGI_DATA))
        {
          break;
        }
      }

      if(!((role == fcgi_si::FCGI_AUTHORIZER) && 
           (request.stdin_length == 0U)))
      {
        if(!DataAndStdinWriter(fcgi_si::FcgiType::kFCGI_STDIN))
        {
          break;
        }
      }

      if(!(request.params_map.empty()))
      {
        ParamsMap::const_iterator start_iter {request.params_map.begin()};
        ParamsMap::const_iterator end_iter   {request.params_map.end()};
        std::size_t offset {0U};
        do
        {
          std::tuple<bool, std::size_t, std::vector<struct iovec>, int,
            std::vector<std::uint8_t>, std::size_t, ParamsMap::const_iterator>
          params_encoding {fcgi_si::EncodeNameValuePairs(start_iter, end_iter,
            fcgi_si::FcgiType::kFCGI_PARAMS, new_id, offset)};
          if(!std::get<0>(params_encoding))
          {
            saved_errno = EINVAL;
            write_error = true;
            break;
          }
          std::tuple<struct iovec*, int, std::size_t> params_write
            {socket_functions::ScatterGatherSocketWrite(connection,
              std::get<2>(params_encoding).data(),
              std::get<2>(params_encoding).size(), false, nullptr)};
          if(std::get<2>(params_write))
          {
            write_error = true;
            saved_errno = errno;
            break;
          }
          offset     = std::get<5>(params_encoding);
          start_iter = std::get<6>(params_encoding);
        } while(start_iter != end_iter);
      }
      if(!write_error)
      {
        // A terminal FCGI_PARAMS record must be sent in all cases.
        std::uint8_t params_record[fcgi_si::FCGI_HEADER_LEN] = {};
        fcgi_si::PopulateHeader(params_record, fcgi_si::FcgiType::kFCGI_PARAMS,
          new_id, 0U, 0U);
        SocketWriteHelper(params_record, fcgi_si::FCGI_HEADER_LEN);
      }
    } while(false);
    if(write_error)
    {
      try // noexcept-equivalent block.
      {
        state_ptr->id_manager.ReleaseId(new_id);
      }
      catch(...)
      {
        std::terminate();
      }
      FailedWrite(entry_ptr, saved_errno, nothing_written, false,
        "write");
      return fcgi_si::RequestIdentifier {};
    }
    // Insert a new RequestData instance to pending_request_map_.
    pending_request_map_.insert({{connection, new_id}, {request, {}, {}}});
  }
  catch(...)
  {
    // The first write happens to be use operations which cannot throw.
    // Accordingly, if this catch block is reached, then some data will have
    // been written.
    try // noexcept-equivalent block.
    {
      CloseConnection(entry_ptr->first);
      micro_event_queue_.push_back(std::unique_ptr<ServerEvent>
        {new ConnectionClosure {entry_ptr->first}});
    }
    catch(...)
    {
      std::terminate();
    }
    throw;
  }
  return fcgi_si::RequestIdentifier {connection, new_id};
}

} // namespace fcgi_si_test
