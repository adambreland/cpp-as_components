#include "test/test_fcgi_client_interface.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
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
//
// Invariants on I/O multiplexing tracking state.
// 1) remaining_ready_ >= 0.
// 2) next_connection_ != connection_map_.end() if and only if
//    remaining_ready_ > 0.
// 3) If remaining_ready_ > 0, then select_set_ contains a set of connected
//    socket descriptors which were deemed ready for reading by a call to
//    select.
// 4) If remaining_ready_ > 0, then the number of connected socket descriptors
//    which are greater than or equal to next_connection_ and which are ready
//    for reading per select_set_ is equal to remaining_ready_.
//
// Invariants and disciplines for connection and disconnection:
// 1) Connected socket descriptors may only be introduced when a user calls
//    Connect.
// 2) Connected socket descriptors may only be closed (disconnected) through a
//    call to CloseConnection. CloseConnection maintains class invariants when
//    a connected socket descriptor is closed.
// 3) a) number_connected_ holds the number of connected socket descriptors
//       which are managed by TestFcgiClientInterface.
//    b) number_connected_ is less than or equal to connection_map_.size().
//       Equality does not hold as connection_map_ may store information which
//       is associated with socket descriptors which have been closed. In
//       particular, connection_map_ may store IdManager instances for closed
//       socket descriptors so that a user can defer reuse of FastCGI request
//       identifiers on particular socket descriptors.

// Exception guarantees which may require verification:
// std::vector<std::uint8_t>
static_assert(std::is_nothrow_default_constructible<std::vector<std::uint8_t>>::value);
static_assert(std::is_nothrow_move_constructible<std::vector<std::uint8_t>>::value);
static_assert(std::is_nothrow_move_assignable<std::vector<std::uint8_t>>::value);
// ParamsMap
static_assert(std::is_nothrow_default_constructible<ParamsMap>::value);
static_assert(std::is_nothrow_move_constructible<ParamsMap>::value);
static_assert(std::is_nothrow_move_assignable<ParamsMap>::value);
// ManagementRequestData
static_assert(std::is_nothrow_default_constructible<ManagementRequestData>::value);
static_assert(std::is_nothrow_move_constructible<ManagementRequestData>::value);
static_assert(std::is_nothrow_move_assignable<ManagementRequestData>::value);
// FcgiResponse
static_assert(std::is_nothrow_default_constructible<FcgiResponse>::value);
static_assert(std::is_nothrow_move_constructible<FcgiResponse>::value);
static_assert(std::is_nothrow_move_assignable<FcgiResponse>::value);
// InvalidRecord
static_assert(std::is_nothrow_default_constructible<InvalidRecord>::value);
static_assert(std::is_nothrow_move_constructible<InvalidRecord>::value);
static_assert(std::is_nothrow_move_assignable<InvalidRecord>::value);
// GetValuesResult
static_assert(std::is_nothrow_default_constructible<GetValuesResult>::value);
static_assert(std::is_nothrow_move_constructible<GetValuesResult>::value);
static_assert(std::is_nothrow_move_assignable<GetValuesResult>::value);
// UnknownType
static_assert(std::is_nothrow_default_constructible<UnknownType>::value);
static_assert(std::is_nothrow_move_constructible<UnknownType>::value);
static_assert(std::is_nothrow_move_assignable<UnknownType>::value);

TestFcgiClientInterface::TestFcgiClientInterface()
: completed_request_set_ {},
  connection_map_        {},
  pending_request_map_   {},
  micro_event_queue_     {},
  number_connected_      {0},
  remaining_ready_       {0},
  next_connection_       {connection_map_.end()},
  select_set_            {}
{
  FD_ZERO(&select_set_);
}

TestFcgiClientInterface::~TestFcgiClientInterface()
{
  // Socket descriptors are the only resource which is not otherwise handled.
  for(std::map<int, ConnectionState>::iterator connection_iter 
    {connection_map_.begin()}; connection_iter != connection_map_.end();
    ++connection_iter)
  {
    if(connection_iter->second.connected)
    {
      close(connection_iter->first);
    }
  }
}

bool TestFcgiClientInterface::CloseConnection(int connection)
{
  std::map<int, TestFcgiClientInterface::ConnectionState>::iterator
  connection_iter {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
    return false;
  ConnectionState* state_ptr {&(connection_iter->second)};

  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_start
    {pending_request_map_.lower_bound({connection, 0U})};
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_end
    {(connection < std::numeric_limits<int>::max()) ?
      pending_request_map_.lower_bound({connection + 1, 0U}) :
      pending_request_map_.end()};
  
  bool connection_is_ready {(remaining_ready_ > 0)                     &&
                            (next_connection_->first <= connection)    &&
                            (FD_ISSET(connection, &select_set_) > 0)};
  bool disconnect_next {connection_is_ready &&
                        (next_connection_ == connection_iter)};

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
    state_ptr->record_state.invalidated            = false;
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
    if(disconnect_next)
    {
      ++next_connection_;
    }
  }
  else // No completed-but-unreleased requests are present for connection.
  {
    if(disconnect_next)
    {
      ++next_connection_;
    }
    connection_map_.erase(connection_iter);
    pending_request_map_.erase(pending_start, pending_end);
  }

  // No throw state update to maintain class invariants.
  //
  // Handle the case that connection has been marked as ready for reading in
  // a call to RetrieveServerEvent.
  if(connection_is_ready)
  {
    --remaining_ready_;
  }
  --number_connected_;

  if((close(connection) == -1) && (errno != EINTR))
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "close"};
  }
  return true;
}

int TestFcgiClientInterface::Connect(const char* address, in_port_t port)
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
  socklen_t addr_size {};
  if(inet_pton(AF_INET, address, &addr_store) > 0)
  {
    domain = AF_INET;
    addr_size = sizeof(struct sockaddr_in);
    static_cast<struct sockaddr_in*>(
      static_cast<void*>(&addr_store))->sin_family = AF_INET;
    static_cast<struct sockaddr_in*>(
      static_cast<void*>(&addr_store))->sin_port = port;
  }
  else if(inet_pton(AF_INET6, address, &addr_store) > 0)
  {
    domain = AF_INET6;
    addr_size = sizeof(struct sockaddr_in6);
    static_cast<struct sockaddr_in6*>(
      static_cast<void*>(&addr_store))->sin6_family = AF_INET6;
    static_cast<struct sockaddr_in6*>(
      static_cast<void*>(&addr_store))->sin6_port = port;
  }
  else
  {
    domain = AF_UNIX;
    // Calls to connect failed during testing with AF_UNIX and
    // addrlen != sizeof(struct sockaddr_un).
    addr_size = sizeof(struct sockaddr_un);
    std::size_t address_length {std::strlen(address)};
    // The value 91 comes from the current portable limit for UNIX address
    // lengths (where one byte is saved for the null terminator).
    if((address_length == 0) || (address_length > 91))
    {
      errno = EINVAL;
      return -1;
    }
    static_cast<struct sockaddr_un*>(static_cast<void*>(&addr_store))->
      sun_family = AF_UNIX;
    char* path_ptr {static_cast<struct sockaddr_un*>(static_cast<void*>(
      &addr_store))->sun_path};
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
    if(socket_connection >= FD_SETSIZE)
    {
      close(socket_connection);
      throw std::runtime_error
        {"In a call to TestFcgiClientInterface::Connect, a file descriptor "
         "which was returned by a call to socket was too large to be "
         "used in a call to select in a call to "
         "TestFcgiClientInterface::RetrieveServerEvent."};
    }
    if(connect(socket_connection,
      static_cast<const struct sockaddr*>(static_cast<void*>(&addr_store)),
      addr_size) == -1)
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
  ++number_connected_;
  return socket_connection;
}

std::map<int, TestFcgiClientInterface::ConnectionState>::iterator
TestFcgiClientInterface::ConnectedCheck(int connection)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {connection_map_.find(connection)};
  std::map<int, ConnectionState>::iterator connection_end
    {connection_map_.end()};
  if((connection_iter == connection_end)  ||
     !(connection_iter->second.connected))
  {
    return connection_end;
  }
  return connection_iter;
}

void TestFcgiClientInterface::ExamineSelectReturn()
{
  std::map<int, ConnectionState>::iterator connection_end
    {connection_map_.end()};
  while(next_connection_ != connection_end)
  {
    if(next_connection_->second.connected)
    {
      int descriptor {next_connection_->first};
      if(FD_ISSET(descriptor, &select_set_))
      {
        ConnectionState* state_ptr {&(next_connection_->second)};
        // An interator to pending_request_map_ is declared here as a way
        // to save searches over pending_request_map_ in the likely event
        // that multiple record parts are received in one read. Partitioning
        // of received data by the below buffer size is irrelevant to this
        // optimization and, as such, pending_iter is declared here instead
        // of inside the next loop.
        //
        // Note that, whenever pending_iter takes on a non-end value,
        // pending_iter->first.descriptor() == descriptor. In other words,
        // in the loop below, pending_iter will always refer to the end or to a
        // pending request in the collection of requests for descriptor.
        std::map<fcgi_si::RequestIdentifier, RequestData>::iterator
        pending_end {pending_request_map_.end()};
        std::map<fcgi_si::RequestIdentifier, RequestData>::iterator
        pending_iter {pending_end};

        // Start reading until the connection blocks.
        constexpr unsigned int buffer_size {1U << 9U};
        std::uint8_t buffer[buffer_size];

        // next_connection_ must be updated to the next connection by the time
        // that this loop exits.
        while(true)
        {
          unsigned int read_return {static_cast<unsigned int>(
            socket_functions::SocketRead(descriptor, buffer, buffer_size))};
          int saved_errno {errno};
          unsigned int remaining_data {read_return};
          std::uint8_t* current_byte {buffer};
          while(remaining_data > 0U)
          {
            // Header
            std::uint8_t received_header
              {state_ptr->record_state.header_bytes_received};
            if(received_header < fcgi_si::FCGI_HEADER_LEN)
            {
              std::uint8_t remaining_header {static_cast<std::uint8_t>(
                fcgi_si::FCGI_HEADER_LEN - received_header)};
              std::uint8_t copy_size {(remaining_data >= remaining_header) ?
                remaining_header : static_cast<std::uint8_t>(remaining_data)};
              std::memcpy(state_ptr->record_state.header + received_header,
                current_byte, copy_size);
              current_byte                                  += copy_size;
              remaining_data                                -= copy_size;
              received_header                               += copy_size;
              state_ptr->record_state.header_bytes_received  = received_header;
              if(received_header == fcgi_si::FCGI_HEADER_LEN)
              {
                try
                {
                  pending_iter = UpdateOnHeaderCompletion(next_connection_,
                    pending_iter);
                  if((state_ptr->record_state.content_bytes_expected == 0U) && 
                    (state_ptr->record_state.padding_bytes_expected == 0U))
                  {
                    // ProcessCompleteRecord may invalidate pending_iter during
                    // its execution. It is required to return a valid value
                    // for pending_iter.
                    pending_iter = ProcessCompleteRecord(next_connection_,
                      pending_iter);
                    continue;
                  }
                }
                catch(...)
                {
                  std::terminate(); 
                  // Failure of UpdateOnHeaderCompletion and
                  // ProcessCompleteRecord will cause invariants of the class
                  // to be violated.
                }
              }
            }
            if(remaining_data == 0U)
              break;
            
            // A lambda to ensure that the assumptions on pending_iter
            // are met and to update pending_iter when that is needed.
            //
            // Also, some common state for Content and Padding.
            std::uint16_t fcgi_id {state_ptr->record_state.fcgi_id};
            fcgi_si::FcgiType record_type {state_ptr->record_state.type};
            auto PendingIterCheckAndUpdate =
            [&pending_iter, &pending_end, &descriptor, &fcgi_id, this]()->void
            {
              fcgi_si::RequestIdentifier id {descriptor, fcgi_id};
              if((pending_iter == pending_end) || (pending_iter->first != id))
              {
                pending_iter = pending_request_map_.find(id);
                if(pending_iter == pending_end)
                {
                  throw std::logic_error {"A request was not present when "
                    "expected in a call to "
                    "TestFcgiClientInterface::RetrieveServerEvent."};
                }
              }
            };

            // Content
            std::uint16_t received_content
              {state_ptr->record_state.content_bytes_received};
            std::uint16_t expected_content
              {state_ptr->record_state.content_bytes_expected};
            if(received_content < expected_content)
            {
              std::uint16_t remaining_content {static_cast<std::uint16_t>(
                expected_content - received_content)};
              std::uint16_t copy_size {(remaining_data >= remaining_content) ?
                remaining_content : static_cast<std::uint16_t>(remaining_data)};              
              std::uint8_t* current_end {current_byte + copy_size};
              if(!(state_ptr->record_state.invalidated) &&
                 (fcgi_id != 0U)                        &&
                 (record_type != fcgi_si::FcgiType::kFCGI_END_REQUEST))
              {
                // Type is either FCGI_STDOUT or FCGI_STDERR.
                try
                {
                  PendingIterCheckAndUpdate();
                }
                catch(...)
                {
                  std::terminate();
                  // A throw indicates an internal logic error than cannot be
                  // recovered from. Logging may be put here later. 
                }
                bool is_out {record_type == fcgi_si::FcgiType::kFCGI_STDOUT};
                std::vector<std::uint8_t>::iterator content_end_iter {(is_out) ?
                  pending_iter->second.fcgi_stdout.end() :
                  pending_iter->second.fcgi_stderr.end()};
                try
                {
                  (is_out) ?
                    pending_iter->second.fcgi_stdout.insert(content_end_iter,
                      current_byte, current_end) :
                    pending_iter->second.fcgi_stderr.insert(content_end_iter,
                      current_byte, current_end);
                }
                catch(...)
                {
                  std::terminate();
                  // Termination is performed for simplicity as this is an
                  // interface for testing. Recovery may be possible.
                }
              }
              else
              {
                if(record_type == fcgi_si::FcgiType::kFCGI_END_REQUEST)
                {
                  try
                  {
                    PendingIterCheckAndUpdate();
                  }
                  catch(...)
                  {
                    std::terminate();
                    // A throw indicates an internal logic error than cannot be
                    // recovered from. Logging may be put here later. 
                  }
                }
                std::vector<std::uint8_t>* local_buffer_ptr
                  {&(state_ptr->record_state.local_buffer)};
                std::vector<std::uint8_t>::iterator local_buffer_end
                  {local_buffer_ptr->end()};
                try
                {
                  local_buffer_ptr->insert(local_buffer_end, current_byte,
                    current_byte + copy_size);
                }
                catch(...)
                {
                  std::terminate();
                  // Termination is performed for simplicity as this is an
                  // interface for testing. Recovery may be possible.
                }
              }
              current_byte                                  += copy_size;
              remaining_data                                -= copy_size;
              received_content                              += copy_size;
              state_ptr->record_state.content_bytes_received = received_content;
              // Check if the record is complete.
              if((received_content == expected_content) &&
                 (state_ptr->record_state.padding_bytes_expected == 0U))
              {
                try
                {
                  // Note that PendingIterCheckAndUpdate was called in the
                  // three cases in which this is required, i.e. record_type is
                  // FCGI_END_REQUEST, FCGI_STDERR, or FCGI_STDOUT).
                  pending_iter = ProcessCompleteRecord(next_connection_,
                    pending_iter);
                  continue;
                }
                catch(...)
                {
                  std::terminate();
                  // As above, failure is an invariant violation that cannot be
                  // recovered from.
                }
              }
            }
            if(remaining_data == 0U)
              break;

            // Padding
            std::uint8_t expected_padding
              {state_ptr->record_state.padding_bytes_expected};
            std::uint8_t received_padding
              {state_ptr->record_state.padding_bytes_received};
            if(received_padding < expected_padding)
            {
              std::uint8_t remaining_padding
                {static_cast<std::uint8_t>(expected_padding - received_padding)};
              std::uint8_t copy_size {static_cast<std::uint8_t>(
                (remaining_data >= remaining_padding) ?
                  remaining_padding : remaining_data)};
              // Padding is discarded.
              current_byte                                  += copy_size;
              remaining_data                                -= copy_size;
              received_padding                              += copy_size;
              state_ptr->record_state.padding_bytes_received = received_padding;

              // Check if the record is complete.
              if(received_padding == expected_padding)
              {
                try
                {
                  // Ensure that pending_iter refers to the appropriate pending
                  // request.
                  if((record_type == fcgi_si::FcgiType::kFCGI_END_REQUEST) ||
                    (record_type == fcgi_si::FcgiType::kFCGI_STDERR)      ||
                    (record_type == fcgi_si::FcgiType::kFCGI_STDOUT))
                  {
                    PendingIterCheckAndUpdate();
                  }
                  pending_iter = ProcessCompleteRecord(next_connection_,
                    pending_iter);
                }
                catch(...)
                {
                  std::terminate();
                  // As above, failure of either PendingIterCheckAndUpdate or
                  // ProcessCompleteRecord cannot be recovered from (though
                  // for different reasons).
                }
              }
            }
          } // End iterating over data received from a call to SocketRead.

          // Handle errors that may have occurred when SocketRead was called
          // and conditionally break;
          if(read_return < buffer_size)
          {
            // Note that saved_errno == EINTR is not possible per the semantics
            // of SockedRead.
            if((saved_errno == 0) || (saved_errno == EAGAIN) ||
               (saved_errno == EWOULDBLOCK))
            {
              // saved_errno == 0 implies that the peer closed the connection.
              // The other cases imply that no more data can be read.
              //
              // All of these cases require that the select return tracking
              // variables be updated.
              try
              {
                --remaining_ready_;
                if(remaining_ready_ == 0)
                {
                  next_connection_ = connection_end;
                }
                else
                {
                  std::map<int, ConnectionState>::iterator local_iter
                    {next_connection_};
                  ++local_iter;
                  if(local_iter == connection_end)
                  {
                    throw std::logic_error {"A discrepancy was detected between "
                      "the ready connection count and the number of "
                      "connections in a call to "
                      "TestFcgiClientInterface::RetrieveServerEvent."};
                  }
                  else
                  {
                    next_connection_ = local_iter;
                  }
                }
                if(saved_errno == 0)
                {
                  // Close the connection.
                  CloseConnection(descriptor);
                  micro_event_queue_.push_back(std::unique_ptr<ServerEvent> {
                    new ConnectionClosure {descriptor}});
                }
              }
              catch(...)
              {
                std::terminate();
              }
              return;
            }
            else
            {
              std::error_code ec {saved_errno, std::system_category()};
              throw std::system_error {ec, "read"};
            }
          }
       // else: continue reading.
        } // while(true) on SocketRead.
          // If this loop was entered, which is equivalent to the select ready
          // check condition of the previous if statement being true, then the
          // function will either return or throw (or the program will
          // terminate).
      } // Select ready check.
   // else: Fall through to ++next_connection_.
    } // Connected check.
 // else: Fall through to ++next_connection_.
    ++next_connection_;
  } // Loop over next_connection_.

  // If this point was reached, then an error occurred.
  throw std::logic_error {"An error occurred while tracking connections which "
    "were ready to be read in a call to "
    "TestFcgiClientInterface::RetrieveServerEvent. No such connections were "
    "present when some were expected."};
}

void TestFcgiClientInterface::FailedWrite(
  std::map<int, ConnectionState>::iterator connection_iter,
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
      connection_iter->second.management_queue.pop_back();
    }
    if(!(nothing_written && (error_code != EPIPE)))
    {
      CloseConnection(connection_iter->first);
      micro_event_queue_.push_back(std::unique_ptr<ServerEvent>
        {new ConnectionClosure {connection_iter->first}});
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

std::size_t TestFcgiClientInterface::ManagementRequestCount(int connection) const
{
  std::map<int, ConnectionState>::const_iterator connection_iter
    {connection_map_.find(connection)};
  if(connection_iter == connection_map_.cend())
  {
    throw std::invalid_argument {"In a call to "
      "fcgi_si_test::TestFcgiClientInterface::ManagementRequestCount, "
      "connection was not managed by the interface instance."};
  }
  return connection_iter->second.management_queue.size();
}

std::map<fcgi_si::RequestIdentifier, 
  TestFcgiClientInterface::RequestData>::iterator
TestFcgiClientInterface::ProcessCompleteRecord(
  std::map<int, ConnectionState>::iterator connection_iter,
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_iter)
{
  // The organization of the implementations of the cases below allows
  // the strong exception guarantee to be satisfied. This requires the
  // somewhat elaborate implementations of the cases.

  auto TryToAssignLastQueueItemPointer = [this]
  (std::unique_ptr<ServerEvent>** assign_to)->void
  {
    try
    {
      *assign_to = &(micro_event_queue_.back());
    }
    catch(...)
    {
      // Assumes that pop_back is effectively noexcept.
      micro_event_queue_.pop_back();
      throw;
    }
  };

  ConnectionState* state_ptr {&(connection_iter->second)};
  if(state_ptr->record_state.invalidated)
  {
    std::unique_ptr<InvalidRecord> new_event {new InvalidRecord {}};
    micro_event_queue_.push_back(std::unique_ptr<ServerEvent> {});
    std::unique_ptr<ServerEvent>* back_ptr {nullptr};
    TryToAssignLastQueueItemPointer(&back_ptr);
    *new_event = InvalidRecord
    {
      state_ptr->record_state.header[fcgi_si::kHeaderVersionIndex],
      state_ptr->record_state.type,
      {connection_iter->first, state_ptr->record_state.fcgi_id},
      std::move(state_ptr->record_state.local_buffer),
      state_ptr->record_state.padding_bytes_expected
    };
    *back_ptr = std::move(new_event); // Upcast to ServerEvent*.
  }
  else
  {
    switch(state_ptr->record_state.type) {
      case fcgi_si::FcgiType::kFCGI_END_REQUEST : {
        // Extract the application status.
        std::uint8_t* data_ptr {state_ptr->record_state.local_buffer.data()};
        std::int32_t local_app_status {*data_ptr};
        for(int i {0}; i < 3; ++i)
        {
          local_app_status <<= 8U;
          ++data_ptr;
          local_app_status += *data_ptr;
        }
        // Extract the protocol status.
        ++data_ptr;
        std::uint8_t local_protocol_status {*data_ptr};
        std::unique_ptr<FcgiResponse> new_event {new FcgiResponse {}};
        micro_event_queue_.push_back(std::unique_ptr<ServerEvent> {});
        std::unique_ptr<ServerEvent>* back_ptr {nullptr};
        TryToAssignLastQueueItemPointer(&back_ptr);
        if(!(completed_request_set_.insert(pending_iter->first).second))
        {
          throw std::logic_error {"A request was found to be present in the "
            "completed request tracking set when it should not have been "
            "in a call to TestFcgiClientInterface::RetrieveServerEvent."};
        }
        *new_event = FcgiResponse
        {
          local_app_status,
          std::move(pending_iter->second.fcgi_stderr),
          std::move(pending_iter->second.fcgi_stdout),
          local_protocol_status,
          pending_iter->second.request,
          pending_iter->first
        };
        *back_ptr = std::move(new_event); // Upcast to ServerEvent*.
        pending_request_map_.erase(pending_iter);
        // The previous call invalidated pending iter. It must be brought to a
        // valid state.
        pending_iter = pending_request_map_.end();
        break;
      }
      case fcgi_si::FcgiType::kFCGI_STDOUT : {
        if(state_ptr->record_state.content_bytes_expected == 0U)
        {
          pending_iter->second.stdout_completed = true;
        }
        break;
      }
      case fcgi_si::FcgiType::kFCGI_STDERR : {
        if(state_ptr->record_state.content_bytes_expected == 0U)
        {
          pending_iter->second.stderr_completed = true;
        }
        break;
      }
      case fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT : {
        // An attempt is made to extract a name-value pair map from the
        // byte sequence which was sent by the server. If a failure occurs,
        // which includes the presence of duplicate names, an empty map is
        // returned and the corruption flag of the GetValuesResult instance
        // is set.
        std::vector<std::uint8_t>* buffer_ptr
          {&(state_ptr->record_state.local_buffer)};
        ParamsMap params_result {};
        bool params_error {true};
        std::vector<std::uint8_t>::size_type local_buffer_size
          {buffer_ptr->size()};
        if(local_buffer_size)
        {
          using NameValuePair = 
            std::pair<std::vector<std::uint8_t>, std::vector<std::uint8_t>>;
          std::vector<NameValuePair> name_value_list 
            {fcgi_si::ExtractBinaryNameValuePairs(
              buffer_ptr->data(), local_buffer_size)};
          if(name_value_list.size())
          {
            auto NameValuePairNameCompare = []
            (
              const NameValuePair& lhs,
              const NameValuePair& rhs
            )->bool
            {
              return lhs.first < rhs.first;
            };
            std::sort(name_value_list.begin(), name_value_list.end(),
              NameValuePairNameCompare);
            std::vector<NameValuePair>::iterator duplicate_iter
              {std::adjacent_find(name_value_list.begin(),
                name_value_list.end())};
            if(duplicate_iter == name_value_list.end())
            {
              params_error = false;
              std::move_iterator<std::vector<NameValuePair>::iterator> 
              begin_move {name_value_list.begin()};
              std::move_iterator<std::vector<NameValuePair>::iterator>
              end_move   {name_value_list.end()};
              params_result = ParamsMap {begin_move, end_move};
            }
          }
        }
        std::unique_ptr<GetValuesResult> new_event {new GetValuesResult {}};
        micro_event_queue_.push_back(std::unique_ptr<ServerEvent> {});
        std::unique_ptr<ServerEvent>* back_ptr {nullptr};
        TryToAssignLastQueueItemPointer(&back_ptr);
        *new_event = GetValuesResult
        {
          (local_buffer_size) ? params_error : false,
          {connection_iter->first, 0U},
          std::move(state_ptr->management_queue.front().params_map),
          std::move(params_result)
        };
        *back_ptr = std::move(new_event); // Upcast to ServerEvent*.
        // Assumes that pop_front is effectively noexcept.
        state_ptr->management_queue.pop_front();
        break;
      }
      case fcgi_si::FcgiType::kFCGI_UNKNOWN_TYPE : {
        std::unique_ptr<UnknownType> new_event {new UnknownType {}};
        micro_event_queue_.push_back(std::unique_ptr<ServerEvent> {});
        std::unique_ptr<ServerEvent>* back_ptr {nullptr};
        TryToAssignLastQueueItemPointer(&back_ptr);
        *new_event = UnknownType
        {
          {connection_iter->first, 0U},
          state_ptr->record_state.local_buffer[0],
          std::move(state_ptr->management_queue.front())
        };
        *back_ptr = std::move(new_event); // Upcast to ServerEvent*.
        // Assumes that pop_front is effectively noexcept.
        state_ptr->management_queue.pop_front();
        break;
      }
      default : {
        throw std::logic_error {"An unaccepted type was present but regarded "
          "as valid in a call to TestFcgiClientInterface::RetrieveServerEvent."};
      }
    }
  }
  // Re-initialize the RecordState instance.
  state_ptr->record_state = RecordState {};
  return pending_iter;
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

std::unique_ptr<ServerEvent> TestFcgiClientInterface::RetrieveServerEvent()
{
  // Outline:
  // 1) micro_event_queue_ is always emptied before ready descriptors are
  //    read.
  // 2) If the queue is empty, then the next ready descriptor is read until
  //    it blocks.
  // 3) Once a descriptor blocks, the microevent queue is checked as in 1. The
  //    above process continues until some event is returned or the ready
  //    descriptors are exhausted.
  // 4) If the ready descriptors are exhausted, a call to select is made. When
  //    the call returns, 2 is performed (as if the queue was empty).
  while(true)
  {
    if(micro_event_queue_.size())
    {
      std::unique_ptr<ServerEvent> new_event
        {std::move(micro_event_queue_.front())};
      micro_event_queue_.pop_front();
      return new_event;
    }
    if(remaining_ready_ > 0)
    {
      ExamineSelectReturn();
      continue;
    }
    // Prepare to call select.
    // select_set_ is filled with all connected connections. If no connected
    // connections are present, an exceptions is thrown.
    FD_ZERO(&select_set_);
    int max_for_select {-1};
    std::map<int, ConnectionState>::reverse_iterator r_iter
      {connection_map_.rbegin()};
    std::map<int, ConnectionState>::reverse_iterator r_end
      {connection_map_.rend()};
    while(r_iter != r_end)
    {
      if(r_iter->second.connected)
      {
        max_for_select = r_iter->first;
        break;
      }
      ++r_iter;
    }
    while(r_iter != r_end)
    {
      if(r_iter->second.connected)
      {
        FD_SET(r_iter->first, &select_set_);
      }
      ++r_iter;
    }
    if(max_for_select == -1)
    {
      throw std::logic_error {"A call to "
        "TestFcgiClientInterface::RetrieveServerEvent was made when no "
        "server connections were active."};
    }
    int number_ready {0};
    while(((number_ready = select(max_for_select + 1, &select_set_, nullptr,
      nullptr, nullptr)) == -1) && (errno == EINTR))
      continue;
    if(number_ready == -1)
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "select"};
    }
    remaining_ready_ = number_ready;
    next_connection_ = connection_map_.begin();
    ExamineSelectReturn();
  }
}

bool TestFcgiClientInterface::SendAbortRequest(fcgi_si::RequestIdentifier id)
{
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_iter
    {pending_request_map_.find(id)};
  if(pending_iter == pending_request_map_.end())
  {
    return false;
  }
  int connection {id.descriptor()};
  std::map<int, TestFcgiClientInterface::ConnectionState>::iterator
  connection_iter {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  std::uint8_t abort_header[fcgi_si::FCGI_HEADER_LEN] = {};
  fcgi_si::PopulateHeader(abort_header, fcgi_si::FcgiType::kFCGI_ABORT_REQUEST,
    id.Fcgi_id(), 0U, 0U);
  std::size_t write_return {socket_functions::WriteOnSelect(connection,
    abort_header, fcgi_si::FCGI_HEADER_LEN, nullptr)};
  if(write_return < fcgi_si::FCGI_HEADER_LEN)
  {
    FailedWrite(connection_iter, errno, write_return == 0U, false,
      write_or_select_);
    return false;
  }
  return true;
}

bool TestFcgiClientInterface::SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, const std::uint8_t* byte_ptr, std::size_t length)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  std::vector<std::uint8_t> local_data(byte_ptr, byte_ptr + length);
  ManagementRequestData queue_item {type, {}, std::move(local_data)};
  return SendBinaryManagementRequestHelper(connection_iter, type,
    std::move(queue_item));
}

bool TestFcgiClientInterface::SendBinaryManagementRequest(int connection,
    fcgi_si::FcgiType type, std::vector<std::uint8_t>&& data)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  ManagementRequestData queue_item {type, {}, std::move(data)};
  return SendBinaryManagementRequestHelper(connection_iter, type,
    std::move(queue_item));
}

bool TestFcgiClientInterface::SendBinaryManagementRequestHelper(
  std::map<int, ConnectionState>::iterator connection_iter,
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
  connection_iter->second.management_queue.push_back(std::move(queue_item));

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

  return SendManagementRequestHelper(connection_iter, iovec_array,
    iovec_count, number_to_write);
}

bool TestFcgiClientInterface::SendGetValuesRequest(int connection,
  const ParamsMap& params_map)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
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
  return SendGetValuesRequestHelper(connection_iter,
    {fcgi_si::FcgiType::kFCGI_GET_VALUES, std::move(new_map), {}});
}

bool TestFcgiClientInterface::SendGetValuesRequest(int connection,
  ParamsMap&& params_map)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
  {
    return false;
  }
  // All values are supposed to be empty. This is ensured.
  for(ParamsMap::iterator i {params_map.begin()}; i != params_map.end();
    ++i)
  {
    i->second.clear();
  }
  return SendGetValuesRequestHelper(connection_iter,
    {fcgi_si::FcgiType::kFCGI_GET_VALUES, std::move(params_map), {}});
}

bool TestFcgiClientInterface::SendGetValuesRequestHelper(
  std::map<int, ConnectionState>::iterator connection_iter,
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
  
  connection_iter->second.management_queue.push_back(std::move(queue_item));

  return SendManagementRequestHelper(connection_iter,
    std::get<2>(encode_return).data(), std::get<2>(encode_return).size(),
    std::get<1>(encode_return));
}

bool TestFcgiClientInterface::SendManagementRequestHelper(
  std::map<int, ConnectionState>::iterator connection_iter,
  struct iovec iovec_array[], int iovec_count, std::size_t number_to_write)
{
  std::tuple<struct iovec*, int, std::size_t> write_return
    {socket_functions::ScatterGatherSocketWrite(connection_iter->first,
      iovec_array, iovec_count, number_to_write, true, nullptr)};
  std::size_t number_remaining {std::get<2>(write_return)};
  if(number_remaining != 0U)
  {
    FailedWrite(connection_iter, errno, number_remaining == number_to_write,
      true, write_or_select_);
    return false;
  }
  return true;
}

fcgi_si::RequestIdentifier
TestFcgiClientInterface::SendRequest(int connection,
  const FcgiRequest& request)
{
  std::map<int, ConnectionState>::iterator connection_iter
    {ConnectedCheck(connection)};
  if(connection_iter == connection_map_.end())
  {
    return fcgi_si::RequestIdentifier {};
  }
  ConnectionState* state_ptr       {&(connection_iter->second)};
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
    // Note that the order of stream transmission is important. FCGI_PARAMS
    // is sent last to ensure that a request is not prematurely completed as
    // may occur for Responder and Authorizer roles.
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
          {(is_data) ? request.data_begin  == request.data_end :
                       request.stdin_begin == request.stdin_end};
        const std::uint8_t* start_iter
          {(is_data) ? request.data_begin : request.stdin_begin};
        const std::uint8_t* end_iter 
          {(is_data) ? request.data_end : request.stdin_end};
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
          (request.data_begin == request.data_end)))
      {
        if(!DataAndStdinWriter(fcgi_si::FcgiType::kFCGI_DATA))
        {
          break;
        }
      }

      if(!((role == fcgi_si::FCGI_AUTHORIZER) && 
           (request.stdin_begin == request.stdin_end)))
      {
        if(!DataAndStdinWriter(fcgi_si::FcgiType::kFCGI_STDIN))
        {
          break;
        }
      }

      if((request.params_map_ptr != nullptr) &&
         !(request.params_map_ptr->empty()))
      {
        ParamsMap::const_iterator start_iter {request.params_map_ptr->begin()};
        ParamsMap::const_iterator end_iter   {request.params_map_ptr->end()};
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
      FailedWrite(connection_iter, saved_errno, nothing_written, false,
        "write");
      return fcgi_si::RequestIdentifier {};
    }
    // Insert a new RequestData instance to pending_request_map_.
    pending_request_map_.insert({
      {connection, new_id},
      {request, {}, false, {}, false}
    });
  }
  catch(...)
  {
    // The first write happens to be use operations which cannot throw.
    // Accordingly, if this catch block is reached, then some data will have
    // been written.
    try // noexcept-equivalent block.
    {
      CloseConnection(connection);
      micro_event_queue_.push_back(std::unique_ptr<ServerEvent>
        {new ConnectionClosure {connection}});
    }
    catch(...)
    {
      std::terminate();
    }
    throw;
  }
  return fcgi_si::RequestIdentifier {connection, new_id};
}

std::map<fcgi_si::RequestIdentifier, 
  TestFcgiClientInterface::RequestData>::iterator
TestFcgiClientInterface::UpdateOnHeaderCompletion(
  std::map<int, ConnectionState>::iterator connection_iter,
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_iter)
{
  int descriptor {connection_iter->first};
  ConnectionState* state_ptr {&(connection_iter->second)};
  std::map<fcgi_si::RequestIdentifier, RequestData>::iterator pending_end
    {pending_request_map_.end()};

  // Extract the header information.
  std::uint8_t protocol_version {state_ptr->record_state.
    header[fcgi_si::kHeaderVersionIndex]};
  fcgi_si::FcgiType record_type {static_cast<fcgi_si::FcgiType>(
    state_ptr->record_state.header[fcgi_si::kHeaderTypeIndex])};
  std::uint16_t fcgi_id {state_ptr->record_state.
    header[fcgi_si::kHeaderRequestIDB1Index]};
  fcgi_id <<= 8U;
  fcgi_id += state_ptr->record_state.
    header[fcgi_si::kHeaderRequestIDB0Index];
  std::uint16_t expected_content {state_ptr->record_state.
    header[fcgi_si::kHeaderContentLengthB1Index]};
  expected_content <<= 8U;
  expected_content += state_ptr->record_state.
    header[fcgi_si::kHeaderContentLengthB0Index];
  std::uint8_t expected_padding {state_ptr->record_state.
    header[fcgi_si::kHeaderPaddingLengthIndex]};

  auto PendingIterCheckAndUpdate = 
  [&pending_iter, &descriptor, &fcgi_id, &pending_end, this]()->void
  {
    fcgi_si::RequestIdentifier id {descriptor, fcgi_id};
    if((pending_iter == pending_end) || (pending_iter->first != id))
    {
      // It is acceptable for find to return end as a spurious record may
      // have been sent.
      pending_iter = pending_request_map_.find(id);
    }
  };
  
  // Validate the record.
  bool error_detected {false};
  if(protocol_version != 1U)
  {
    error_detected = true;
  }
  else
  {
    switch(record_type) {
      case fcgi_si::FcgiType::kFCGI_END_REQUEST : {
        PendingIterCheckAndUpdate();
        // Among other questions, does a request exist for this end record?
        // Note that a pending request is moved to the completed map upon its
        // completion. It will then not be present in the pending map.
        if((pending_iter == pending_end)                 ||
            (!(pending_iter->second.stderr_completed) ||
             !(pending_iter->second.stdout_completed))   ||
            (expected_content != 8U))
        {
          error_detected = true;
        }
        break;
      }
      case fcgi_si::FcgiType::kFCGI_STDOUT : {
        PendingIterCheckAndUpdate();
        if((pending_iter == pending_end)                 ||
           (pending_iter->second.stdout_completed))
        {
          error_detected = true;
        }
        break;
      }
      case fcgi_si::FcgiType::kFCGI_STDERR : {
        PendingIterCheckAndUpdate();
        if((pending_iter == pending_end)                 ||
           (pending_iter->second.stderr_completed))
        {
          error_detected = true;
        }
        break;
      }
      case fcgi_si::FcgiType::kFCGI_GET_VALUES_RESULT : {
        if((fcgi_id != 0U)                               ||
           !(state_ptr->management_queue.size())         ||
           (fcgi_si::FcgiType::kFCGI_GET_VALUES != state_ptr->
              management_queue.front().type))
        {
          error_detected = true;
        }
        break;
      }
      case fcgi_si::FcgiType::kFCGI_UNKNOWN_TYPE : {
        if((fcgi_id != 0U)                              ||
           (expected_content != 8U)                     ||
           !(state_ptr->management_queue.size())        ||
           (fcgi_si::FcgiType::kFCGI_GET_VALUES == state_ptr->
              management_queue.front().type))
        {
          error_detected = true;
        }
        break;
      }
      default : {
        // This case implies that a record type was received
        // which was not among the set of valid types for FastCGI
        // clients.
        error_detected = true;
      }
    }
  }
  // Update the RecordState instance with the extracted information and the
  // validation status.
  state_ptr->record_state.type                   = record_type;
  state_ptr->record_state.fcgi_id                = fcgi_id;
  state_ptr->record_state.content_bytes_expected = expected_content;
  state_ptr->record_state.padding_bytes_expected = expected_padding;
  if(error_detected)
  {
    state_ptr->record_state.invalidated = true;
  }
  // pending_iter cannot have been invalidated. It may have been updated.
  // If the record is valid at this point and record_type is one of
  // FCGI_END_REQUEST, FCGI_STDERR, or FCGI_STDOUT, then pending_iter must
  // refer to the appropriate pending request. This is a precondition of
  // ProcessCompleteRecord.
  return pending_iter;
}

} // namespace fcgi_si_test
