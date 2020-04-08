#include <arpa/inet.h>    // For inet_pton() and inet_ntop().
#include <fcntl.h>
#include <netinet/in.h>   // Defines constants for use with inet_ntop().
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>     // For portability for <sys/select.h>.
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>          // For std::uint8_t and others.
#include <cstdlib>          // For std::getenv(), std::size_t, and EXIT_FAILURE.
#include <iterator>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "external/error_handling/include/error_handling.h"
#include "external/linux_scw/include/linux_scw.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/record_status.h"
#include "include/request_data.h"
#include "include/request_identifier.h"
#include "include/utility.h"

// Implementation notes:
// 1) Mutex acquisition patterns and related actions:
//    a) With no other mutexes held, the interface may either:
//        1) Acquire and then release a write mutex.
//        2) Acquire interface_state_mutex_, acquire a write mutex, and then
//           release these in the opposite order of acquisition.
//    b) In particular, the pattern "has write mutex, wants interface mutex" is
//       forbidden as it may lead to deadlock.
//    c) If write mutexes should be destroyed, the following pattern must be 
//       followed:
//       1) No mutexes are held.
//       2) Acquire interface_state_mutex_.
//       3) Acquire a write mutex.
//       4) Release the write mutex which was just acquired. Holding the
//          interface mutex ensures that the write mutex will not be acquired
//          by a request as requests follow the discipline of only acquiring a
//          write mutex while holding the interface mutex.
//       5) Acquire and release other write mutexes if needed.
//       6) Implicitly destroy the write mutexes.
//       7) Update interface state so that any requests which still exist for
//          a connection whose write mutex was destroyed will not try to
//          acquire the destroyed write mutex.
//    d) File descriptor invalidation through calling close on the descriptor
//       may only occur under the protection of the interface mutex. Requests
//       treat mutex validity and file descriptor validity as equivalent.
//       These properties are currently signaled through the 
//       connection_closed_by_interface_ flag of the RequestData object of the
//       request.
// 2) State checks after mutex acquisition:
//    a) Whenever interface_state_mutex_ is obtained with the intention of
//       using interface state for an action other than interface destruction, 
//       bad_interface_state_detected_ must be checked. If true, some part of
//       the shared interface state is corrupt. An exception should be thrown.
//       The interface should be destroyed.
//    b) Whenever a write mutex is obtained with the intention of writing data
//       to the connection protected by the interface, the boolean value 
//       associated with the write mutex must be checked. If true, the
//       connection is corrupted.
// 2) Invariants on state:
//    a) The sets dummy_descriptor_set_ and 
//       (application_closure_request_set_ U connections_to_close_set_)
//       should be disjoint before and after connection cleanup.
//    b) The interface destructor should always be able to safely destroy the
//       interface by:
//       1) Closing the connections in either of write_mutex_map_ or
//          record_status_map_.
//       2) Closing the connections in dummy_descriptor_set_.
//       Any action which would prevent safe destruction must result in
//       program termination.
namespace fcgi_si {

// Initialize static class data members.
std::mutex FCGIServerInterface::interface_state_mutex_ {};
unsigned long FCGIServerInterface::interface_identifier_ {0};
unsigned long FCGIServerInterface::previous_interface_identifier_ {0};

FCGIServerInterface::
FCGIServerInterface(int max_connections, int max_requests,
  std::uint16_t role, std::int32_t app_status_on_abort)
: app_status_on_abort_ {app_status_on_abort},
  maximum_connection_count_ {max_connections},
  maximum_request_count_per_connection_ {max_requests},
  role_ {role},
  socket_domain_ {}
{
  // Checks that the arguments are within the domain.
  std::string error_message {};
  bool construction_argument_error {false};
  // Checks that the role is supported.
  if(role_ != FCGI_RESPONDER)
  {
    construction_argument_error = true;
    error_message += "An FCGIServerInterface object could not be constructed\n"
                     "as the provided role is not supported.\nProvided role: ";
    error_message += std::to_string(role_);
  }
  if(max_connections == 0)
  {
    if(construction_argument_error)
      error_message += '\n';
    else
      construction_argument_error = true;

    error_message += "A value of zero was given for the maximum number of "
      "transport connections.\nThis value must be greater than or equal to one.";
  }
  if(max_requests == 0)
  {
    if(construction_argument_error)
      error_message += '\n';
    else
      construction_argument_error = true;

    error_message += "A value of zero was given for the maximum number of "
      "concurrent requests.\nThis value must be greater than or equal to one.";
  }
  if(construction_argument_error)
    throw std::invalid_argument {ERROR_STRING(error_message)};

  // Ensure that the supplied listening socket is non-blocking. This property
  // is assumed in the design of the AcceptRequests() loop.
  int flags = fcntl(FCGI_LISTENSOCK_FILENO, F_GETFL);
  if(flags == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_GETFL")};
  flags |= O_NONBLOCK;
  if(fcntl(FCGI_LISTENSOCK_FILENO,
     F_SETFL, flags) == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_SETFL")};

  // Check socket options.
  // 1) Determine the socket domain. Internet domains may have a list of
  //    authorized IP addresses bound to "FCGI_WEB_SERVER_ADDRS".
  // 2) Check that the socket is a stream socket.
  // 3) Check that the socket is listening.
  int getsockopt_int_buffer {};
  socklen_t getsockopt_int_buffer_size {sizeof(int)};
  int getsockopt_return {};

  while(((getsockopt_return = getsockopt(FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_DOMAIN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1) && (errno == EINTR)){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_DOMAIN")};
  int socket_domain {getsockopt_int_buffer};
  socket_domain_ = socket_domain;

  getsockopt_int_buffer_size = sizeof(int);
  while(((getsockopt_return = getsockopt(FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1) && (errno == EINTR)){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_TYPE")};
  if(getsockopt_int_buffer != SOCK_STREAM)
    throw std::runtime_error {ERROR_STRING("The socket used for construction "
      "of an FCGIServerInterface object\nwas not a stream socket.")};

  getsockopt_int_buffer_size = sizeof(int);
  while(((getsockopt_return = getsockopt(FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_ACCEPTCONN, &getsockopt_int_buffer,
      &getsockopt_int_buffer_size)) == -1) && (errno == EINTR))
    continue;
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING(
      "getsockopt with SO_ACCEPTCONN")};
  if(getsockopt_int_buffer != 1) // The value 1 indicates listening status.
    throw std::runtime_error {ERROR_STRING("The socket used for construction "
      "of an FCGIServerInterface object\nwas not a listening socket.")};

  // For internet domains, check for IP addresses which the parent process
  // deemed authorized. If FCGI_WEB_SERVER_ADDRS is unbound or bound to an
  // empty value, any address is authorized. If no valid addresses are found
  // after processing a list, an error is thrown. Otherwise, a list of
  // well-formed addresses which have been converted to a normalized
  // presentation format is stored in the FCGIServerInterface object.
  if(socket_domain == AF_INET || socket_domain == AF_INET6)
  {
    const char* ip_address_list_ptr = std::getenv("FCGI_WEB_SERVER_ADDRS");
    std::string ip_address_list {(ip_address_list_ptr) ?
      std::string(ip_address_list_ptr) : ""};
    if(ip_address_list.size() != 0U) // A non-empty address list was bound.
    {
      // Declare appropriate variables to use with inet_pton() and inet_ntop().
      // These structs are internal to struct sockaddr_in and
      // struct sockaddr_in6.
      struct in_addr ipv4_internal_address;
      struct in6_addr ipv6_internal_address;
      void* inet_address_subaddress_ptr {nullptr};
      if(socket_domain == AF_INET)
        inet_address_subaddress_ptr = &(ipv4_internal_address);
      else
        inet_address_subaddress_ptr = &(ipv6_internal_address);
      // Allocate enough space for a maximal normalized address string.
      char normalized_address[INET6_ADDRSTRLEN];

      // Construct a tokenizer to split the string into address tokens.
      // The -1 option selects non-matching substrings and, hence, tokens.
      std::regex comma_tokenizer {","};
      std::sregex_token_iterator token_it {ip_address_list.begin(),
        ip_address_list.end(), comma_tokenizer, -1};
      std::sregex_token_iterator end {};

      // Iterate over tokens and add the normalized textual representation of
      // every well-formed address to the set of authorized addresses. The
      // call sequence inet_pton, inet_ntop normalizes the textual
      // representation of the address.
      for(; token_it != end; ++token_it)
      {
        int inet_pton_return {};
        if((inet_pton_return = inet_pton(socket_domain,
          (token_it->str()).data(), inet_address_subaddress_ptr)) == 1)
        {
          if(!inet_ntop(socket_domain, inet_address_subaddress_ptr,
            normalized_address, INET6_ADDRSTRLEN))
            throw std::runtime_error {ERRNO_ERROR_STRING("inet_ntop")};
          valid_ip_address_set_.insert(normalized_address);
        }
        else if(inet_pton_return == -1)
          throw std::runtime_error {ERRNO_ERROR_STRING("inet_pton")};
      }

      if(!valid_ip_address_set_.size())
        throw std::logic_error {ERROR_STRING("No authorized IP addresses "
          "were found during construction of an FCGIServerInterface object.")};
    }
  }

  // Ensure singleton status and update interface_identifier_ to a valid value.
  // ACQUIRE interface_state_mutex_.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  if(interface_identifier_)
    throw std::logic_error {"Construction of an FCGIServerInterface object "
      "occurred when another object was present."};
  // Prevent interface_identifier_ == 0 when a valid interface is present in
  // the unlikely event of integer overflow.
  if(previous_interface_identifier_ < std::numeric_limits<unsigned long>::max())
    previous_interface_identifier_ += 1U;
  else
    previous_interface_identifier_ = 1U;
  interface_identifier_ = previous_interface_identifier_;
} // RELEASE interface_state_mutex_.

FCGIServerInterface::~FCGIServerInterface()
{
  // Any exception results in program termination.
  try
  {
    for(auto dds_iter {dummy_descriptor_set_.begin()}; 
      dds_iter != dummy_descriptor_set_.end(); ++dds_iter)
      close(*dds_iter);

    // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock
      {FCGIServerInterface::interface_state_mutex_};

    // ACQUIRE and RELEASE each write mutex. The usage discipline followed by
    // FCGIRequest objects for write mutexes ensures that no write mutex will
    // be held when the loop completes until the interface mutex is released.
    // Close all file descriptors for active sockets.
    for(auto write_mutex_iter {write_mutex_map_.begin()};
        write_mutex_iter != write_mutex_map_.end(); ++write_mutex_iter)
    {
      write_mutex_iter->second.first->lock();
      write_mutex_iter->second.first->unlock();
      close(write_mutex_iter->first);
    }

    // Kill interface.
    FCGIServerInterface::interface_identifier_ = 0U;
  } // RELEASE interface_state_mutex_.
  catch(...)
  {
    std::terminate();
  }
}

int FCGIServerInterface::AcceptConnection()
{
  struct sockaddr_storage new_connection_address;
  socklen_t new_connection_address_length = sizeof(struct sockaddr_storage);
  int accept_return {};
  while((accept_return = accept(FCGI_LISTENSOCK_FILENO,
    (struct sockaddr*)&new_connection_address, &new_connection_address_length))
    == -1 && errno == EINTR)
  {
    new_connection_address_length = sizeof(struct sockaddr_storage);
  }
  if(accept_return == -1)
  {
    if(errno == EWOULDBLOCK || errno == EAGAIN)
      return -1;
    else if(errno == ECONNABORTED)
      return 0;
    else
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error(ec, "accept");
    }
  }
  // The call to accept() returned a file descriptor for a new connected socket.
  int new_socket_descriptor {accept_return};

  // Check that the connected socket has the same domain as the listening
  // socket and that it is a stream socket.
  int getsockopt_int_buffer {};
  socklen_t getsockopt_int_buffer_size {sizeof(int)};
  int getsockopt_return {};

  while((getsockopt_return = getsockopt(new_socket_descriptor,
    SOL_SOCKET, SO_DOMAIN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 && errno == EINTR)
    continue;
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error(ec, "getsockopt with SO_DOMAIN");
  }
  int new_socket_domain {getsockopt_int_buffer};

  getsockopt_int_buffer_size = sizeof(int);
  while((getsockopt_return = getsockopt(new_socket_descriptor,
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 && errno == EINTR)
    continue;
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error(ec, "getsockopt with SO_TYPE");
  }
  int new_socket_type {getsockopt_int_buffer};

  // Perform address validation against the list of authorized addresses
  // if applicable. A non-empty set implies an internet domain.
  std::string new_address {};
  if(valid_ip_address_set_.size())
  {
    char address_array[INET6_ADDRSTRLEN];
    void* addr_ptr {(new_socket_domain == AF_INET) ?
      (void*)&(((struct sockaddr_in*)&new_connection_address)->sin_addr)
      : (void*)&(((struct sockaddr_in6*)&new_connection_address)->sin6_addr)};
    if(!inet_ntop(new_socket_domain, addr_ptr, address_array, INET6_ADDRSTRLEN))
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error(ec, "inet_ntop");
    }
    new_address = address_array;
  }

  // Validate the new connected socket against the gathered information.
  if(new_socket_domain != socket_domain_ || new_socket_type != SOCK_STREAM
     || (valid_ip_address_set_.size() > 0U
         && (valid_ip_address_set_.find(new_address) ==
             valid_ip_address_set_.end())))
  {
    try {linux_scw::CloseWithErrorCheck(new_socket_descriptor);}
    catch(std::runtime_error& e)
    {
      std::string error_message {e.what()};
      error_message += ERROR_STRING("A call to CloseWithErrorCheck threw an "
        "error which could not be handled.");
      throw std::runtime_error {error_message};
    }
    return 0;
  }

  // Make the accepted connected sockets non-blocking.
  int flags {};
  while((flags = fcntl(new_socket_descriptor, F_GETFL)) == -1
    && errno == EINTR){}
  if(flags == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_GETFL")};

  flags |= O_NONBLOCK;

  while((flags = fcntl(new_socket_descriptor, F_SETFL, flags)) == -1
    && errno == EINTR){}
  if(flags == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_SETFL")};

  // NON-LOCAL STATE modification block start
  // Updates state to reflect the new connection. Tries to update and undoes
  // any changes if an exception is caught. (Strong exception guarantee.)
  std::pair<std::map<int, RecordStatus>::iterator, bool>
    record_status_map_emplace_return {{}, {false}};
  std::pair<
    std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator,
    bool
  >
    write_mutex_map_insert_return {{}, {false}};
  std::pair<std::map<int, int>::iterator, bool>
    request_count_map_emplace_return {{}, {false}};
  // ACQUIRE interface_state_mutex_.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  try
  {
    record_status_map_emplace_return = record_status_map_.emplace(
      new_socket_descriptor, RecordStatus {new_socket_descriptor, this});

    std::unique_ptr<std::mutex> new_mutex_manager {new std::mutex {}};
    write_mutex_map_insert_return = write_mutex_map_.insert(
      {new_socket_descriptor, std::pair<std::unique_ptr<std::mutex>, bool> 
        {std::move(new_mutex_manager), false}});

    request_count_map_emplace_return = request_count_map_.emplace(
        new_socket_descriptor, 0);

    if(!(record_status_map_emplace_return.second
         && write_mutex_map_insert_return.second
         && request_count_map_emplace_return.second))
      throw std::logic_error {ERROR_STRING("Socket descriptor emplacement "
        "failed due to duplication.")};
  }
  catch(...)
  {
    // Ensure that the original state of FCGIServerInterface is restored before
    // rethrowing. The flag can only be true if insertion occurred. If so, the
    // iterator is valid and the socket descriptor must be removed from the map.
    try
    {
      if(record_status_map_emplace_return.second)
        record_status_map_.erase(record_status_map_emplace_return.first);
      if(write_mutex_map_insert_return.second)
        write_mutex_map_.erase(write_mutex_map_insert_return.first);
      if(request_count_map_emplace_return.second)
        request_count_map_.erase(request_count_map_emplace_return.first);
      if(close(new_socket_descriptor) == -1)
        if(errno != EINTR)
        {
          std::error_code ec {errno, std::system_category()};
          throw std::system_error {ec, "close"};
        }
    }
    catch(...)
    {
      std::terminate();
    }
    throw;
  }
  // NON-LOCAL STATE modification block end

  return new_socket_descriptor;
} // RELEASE interface_state_mutex_.

std::vector<FCGIRequest> FCGIServerInterface::AcceptRequests()
{
  std::vector<FCGIRequest> requests {};
  
  // CLEANUP
  { 
    // Start of interface_state_lock handling block.

    // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock 
      {FCGIServerInterface::interface_state_mutex_};

    if(bad_interface_state_detected_)
      throw std::logic_error {"The interface was found to be corrupt."};

    // Remove dummy descriptors if possible.
    //
    // Exception safety: 
    // Removal of a descriptor from dummy_descriptor_set_ and calling close on
    // that descriptor must be transactional. If performance of these actions
    // was not a transactional step, the following scenario is possible:  
    // 1) The descriptor is released for use but not removed from  
    //    dummy_descriptor_set_.
    // 2) The descriptor is allocated for use by the application.
    // 3) When the destructor of the interface executes, the descriptor is 
    //    spuriously closed as the descriptor remained in dummy_descriptor_set_.
    for(auto dds_iter {dummy_descriptor_set_.begin()};
        dds_iter != dummy_descriptor_set_.end(); /*no-op*/)
    {
      std::map<RequestIdentifier, RequestData>::iterator request_map_iter
        {request_map_.lower_bound(RequestIdentifier {*dds_iter, 0U})};

      // The absence of requests allows closure of the descriptor.
      // Remember that RequestIdentifier is lexically ordered and that a
      // request with an FCGI_id of zero is never added to request_map_.
      if(request_map_iter == request_map_.end() 
          || request_map_iter->first.descriptor() > *dds_iter)
      {
        try
        {
          int connection_to_be_closed {*dds_iter};
          std::set<int>::iterator safe_erasure_iterator {dds_iter};
          ++dds_iter;
          // Erase first to prevent closure without removal from
          // dummy_descriptor_set_ and potential double closure.
          //
          // Assume that erase either doesn't throw or meets the strong
          // exception guarantee. In either case a descriptor won't be leaked.
          dummy_descriptor_set_.erase(safe_erasure_iterator);
          int close_return {close(connection_to_be_closed)};
          if(close_return == -1 && errno != EINTR)
          {
            std::error_code ec {errno, std::system_category()};
            throw std::system_error {ec, "close"};
          }   
        }
        catch(...)
        {
          bad_interface_state_detected_ = true;
          throw;
        }
      }
      else // Leave the descriptor until all requests have been removed.
        ++dds_iter;
    }

    // Close connection descriptors for which closure was requested.
    // Update interface state to allow FCGIRequest objects to inspect for
    // connection closure. 
    //
    // Note that dummy_descriptor_set_ is disjoint from the union of
    // connections_to_close_set_ and application_closure_request_set_. This
    // is necessary as the presence of a descriptor in both categories of
    // descriptors may result in double closure.
    ConnectionClosureProcessing(&connections_to_close_set_,
      connections_to_close_set_.begin(), connections_to_close_set_.end(),
      &application_closure_request_set_, application_closure_request_set_.begin(),
      application_closure_request_set_.end());
  } // RELEASE interface_state_mutex_;

  // DESCRIPTOR MONITORING

  // TODO Determine a better I/O multiplexing system call or library function.
  // For example, some glibc implementations have an upper limit of 1023 for
  // file descriptor values due to the size of FD_SET (which is just an integer
  // type used as a bitset).
  //
  // Construct a descriptor set to wait on for select.
  fd_set read_set;
  FD_ZERO(&read_set);
  // Add listening socket to read_set for new connections.
  FD_SET(FCGI_LISTENSOCK_FILENO, &read_set);
  // 0 + 1 = 1 Set for proper select call if no connected sockets are present.
  int number_for_select {1};
  // Reverse to access highest fd immediately.
  auto map_reverse_iter = record_status_map_.rbegin();
  if(map_reverse_iter != record_status_map_.rend())
  {
    number_for_select = (map_reverse_iter->first) + 1; // Use (highest fd) + 1.
    for(/*no-op*/; map_reverse_iter != record_status_map_.rend();
      ++map_reverse_iter)
      FD_SET(map_reverse_iter->first, &read_set);
  }

  // Wait for data to be available or new connections to be available.
  int select_return {};
  while((select_return =
    select(number_for_select, &read_set, nullptr, nullptr, nullptr))
    == -1 && (errno == EINTR || errno == EAGAIN))
    continue;
  if(select_return == -1) // Unrecoverable error from select().
  {
    // TODO Are there any situations that could cause select to return EBADF
    // from a call with only a non-null read set other than one of the file
    // descriptors not being open?
    if(errno == EBADF)
    {
      // ACQUIRE interface_state_mutex_
      std::unique_lock<std::mutex> unique_interface_state_lock
        {FCGIServerInterface::interface_state_mutex_, std::defer_lock};
      try
      {
        unique_interface_state_lock.lock();
      }
      catch(...)
      {
        std::terminate();
      }
      bad_interface_state_detected_ = true;
    } // RELEASE interface_state_mutex_
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "select"};
  }
  // Check connected sockets (as held in record_status_map_) before the
  // listening socket.
  int connections_read {0};
  for(auto it = record_status_map_.begin();
      (it != record_status_map_.end()) && (connections_read < select_return);
      ++it)
  {
    // Extract file descriptor and check if it is ready.
    int fd {it->first};
    if(FD_ISSET(fd, &read_set))
    {
      connections_read++;
      std::vector<RequestIdentifier> request_identifiers
        {it->second.ReadRecords()};
      if(request_identifiers.size())
      {
        // ACQUIRE interface_state_mutex_.
        std::lock_guard<std::mutex> interface_state_lock
          {FCGIServerInterface::interface_state_mutex_};

        std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator 
          write_mutex_map_iter {write_mutex_map_.find(fd)};
        std::mutex* write_mutex_ptr 
          {write_mutex_map_iter->second.first.get()};
        bool* write_mutex_bad_state_ptr
          {&(write_mutex_map_iter->second.second)};
        // For each request_id, find the associated RequestData object, extract
        // a pointer to it, and create an FCGIRequest object from it.
        for(RequestIdentifier request_id : request_identifiers)
        {
          RequestData* request_data_ptr
            {&(request_map_.find(request_id)->second)};
          FCGIRequest request {request_id,
            FCGIServerInterface::interface_identifier_, this, request_data_ptr,
            write_mutex_ptr, write_mutex_bad_state_ptr};
          requests.push_back(std::move(request));
        }
      } // RELEASE interface_state_mutex_.
    }
  }
  // Accept new connections if some are present.
  if(connections_read < select_return)
    while(AcceptConnection() != -1)
      continue;

  return requests;
}

void FCGIServerInterface::AddRequest(RequestIdentifier request_id, 
  std::uint16_t role, bool close_connection)
{
  try
  {
    std::map<int, int>::iterator request_count_iter 
      {request_count_map_.find(request_id.descriptor())};
    if(request_count_iter == request_count_map_.end())
    {
      throw std::logic_error {"request_count_map_ did not possess an "
        "expected file descriptor key."};
    }

    std::map<RequestIdentifier, RequestData>::iterator request_map_iter
      {request_map_.find(request_id)};
    if(request_map_iter != request_map_.end())
    {
      throw std::logic_error {"An attempt was made to add an element to "
        "request_map_ with a key which was already present."};
    }

    request_map_[request_id] = RequestData(role, close_connection);
    request_count_iter->second++;
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
}

// Synchronization:
// 1) interface_state_mutex_ must be held prior to a call.
void FCGIServerInterface::RemoveConnection(int connection)
{
  // Care must be taken to prevent descriptor leaks or double closures.

  // A lambda which checks for the presence of the connection in and attempts
  // to erase the connection from record_status_map_ and write_mutex_map_.
  // Terminates the program if erasure doesn't or can't occur.
  auto EraseConnectionOrTerminate = [this](int connection)->void
  {
    try
    {
      std::map<int, RecordStatus>::iterator record_iter 
        {record_status_map_.find(connection)};
      std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator
        write_iter {write_mutex_map_.find(connection)}; 
      if(record_iter == record_status_map_.end() 
        || write_iter == write_mutex_map_.end())
        throw std::logic_error {"An expected connection was not present in "
          "record_status_map_ or write_mutex_map_"};

      record_status_map_.erase(record_iter);
      write_mutex_map_.erase(write_iter);
    }
    catch(...)
    {
      std::terminate();
    }
  };

  try
  {
    bool assigned_requests {RequestCleanupDuringConnectionClosure(connection)};
    // ACQUIRE and RELEASE the write mutex of the connection to ensure that a 
    // request does not hold it while the connection is being erased.
    std::unique_lock<std::mutex> write_lock
      {*(write_mutex_map_.at(connection).first)};
    write_lock.unlock();
    // Close the connection in one of two ways.
    if(assigned_requests)
    {
      // Go through the process to make the descriptor a dummy.
      // Implicitly and atomically call close(connection).
      //
      // TODO Should a way to check for errors on the implicit closure of
      // connection be implemented?
      int dup2_return {};
      while((dup2_return = dup2(FCGI_LISTENSOCK_FILENO, connection)) == -1)
      {
        if(errno == EINTR || errno == EBUSY)
          continue;
        else
        {
          std::error_code ec {errno, std::system_category()};
          throw std::system_error {ec, "dup2"};
        }
      }
      // Order as given. If insertion throws, erasure never occurs and the
      // descriptor is not leaked.
      dummy_descriptor_set_.insert(connection);
      EraseConnectionOrTerminate(connection);
    }
    else
    {
      // Order as given. If erasure is not ordered before the call of
      // close(connection), it is possible that erasure does not occur and
      // close(connection) will be called twice.
      EraseConnectionOrTerminate(connection);
      int close_return {close(connection)};
      if(close_return == -1 && errno != EINTR)
      {
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "close"};
      }
    }
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
}

void FCGIServerInterface::
RemoveRequestHelper(std::map<RequestIdentifier, RequestData>::iterator iter)
{
  try
  {
    if(iter == request_map_.end())
      throw std::logic_error {"A request to erase an item of request_map_ "
      "was made on a missing key."};

    // Use a pointer instead of a non-constant reference to conditionally 
    // modify the request count associated with request_id.descriptor().
    int* request_count_ptr {&request_count_map_.at(
      iter->first.descriptor())};
    if(*request_count_ptr == 0)
      throw std::logic_error {"request_count_map_ would have obtained "
        "a negative count."};
    *request_count_ptr -= 1;

    request_map_.erase(iter);
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
}

// Synchronization:
// 1) interface_state_mutex_ must be held prior to a call.
bool FCGIServerInterface::RequestCleanupDuringConnectionClosure(int connection)
{
  try
  {
    bool assigned_requests_present {false};
    for(auto request_map_iter =
          request_map_.lower_bound(RequestIdentifier {connection, 0U});
        !(request_map_iter == request_map_.end()
          || request_map_iter->first.descriptor() > connection);
        /*no-op*/)
    {
      if(request_map_iter->second.get_status() ==
        RequestStatus::kRequestAssigned)
      {
        request_map_iter->second.set_connection_closed_by_interface();
        assigned_requests_present = true;
        ++request_map_iter;
      }
      else
      {
        // Safely erase the request.
        std::map<RequestIdentifier, RequestData>::iterator request_map_erase_iter 
          {request_map_iter};
        ++request_map_iter;
        RemoveRequest(request_map_erase_iter);
      }
    }
    return assigned_requests_present;
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
}

bool FCGIServerInterface::
SendFCGIEndRequest(int connection, RequestIdentifier request_id,
  uint8_t protocol_status, int32_t app_status)
{
  std::vector<uint8_t> result(16, 0U); // Allocate space for two bytes.

  // Encode app_status.
  uint8_t app_status_byte_array[4];
  uint32_t unsigned_app_status {static_cast<uint32_t>(app_status)};
  for(int i {0}; i < 4; i++)
  {
    app_status_byte_array[i] = unsigned_app_status >> (24 - (8*i));
  }

  // Set header.
  PopulateHeader(result.data(), FCGIType::kFCGI_END_REQUEST,
    request_id.FCGI_id(), FCGI_HEADER_LEN, 0);
  // Set body.
  for(int i {0}; i < 4; i++)
  {
    result[(i + 1) + kHeaderReservedByteIndex] =
      app_status_byte_array[i];
  }
  result[5 + kHeaderReservedByteIndex]   =
    protocol_status;
  // Remaining bytes were set to zero during string initialization.

  return SendRecord(connection, result.data(), result.size());
}

bool FCGIServerInterface::
SendFCGIUnknownType(int connection, FCGIType type)
{
  std::vector<uint8_t> result(16, 0U); // Allocate space for two bytes.

  // Set header.
  PopulateHeader(result.data(), FCGIType::kFCGI_UNKNOWN_TYPE,
    FCGI_NULL_REQUEST_ID, FCGI_HEADER_LEN, 0U);
  // Set body. (Only the first byte in the body is used.)
  result[1 + kHeaderReservedByteIndex]   =
    static_cast<uint8_t>(type);
  // Remaining bytes were set to zero during string initialization.

  return SendRecord(connection, result.data(), result.size());
}

bool FCGIServerInterface::
SendGetValuesResult(int connection, const std::uint8_t* buffer_ptr, 
  std::size_t count)
{
  using byte_seq_pair = std::pair<std::vector<std::uint8_t>, 
    std::vector<std::uint8_t>>;
  // If count is zero or if the sequence of bytes given by the range
  // [buffer_ptr, buffer_ptr + count) contains an error, the vector returned
  // by ProcessBinaryNameValuePairs is empty. In either case, an empty
  // FCGI_GET_VALUES_RESULT record will be sent to the client. If the client
  // included requests, the absence of those variables in the response will 
  // correctly indicate that the request was not understood (as, in this case,
  // an error will have been present).
  std::vector<byte_seq_pair> get_value_pairs
    {ExtractBinaryNameValuePairs(buffer_ptr, count)};
  
  std::vector<byte_seq_pair> result_pairs {};

  // Constructs result pairs. Disregards any name that is not understood and
  // omit duplicates. The map is used to track which FCGI_GET_VALUES
  // requests are understood (three are specified in the standard) and which
  // requests have already occurred. Once a request type is seen, it is
  // removed from the map. Processing stops once all types have been seen or
  // the list of understood FCGI_GET_VALUES requests is exhausted.

  // The integer values in the key-value pairs are used for the switch only. 
  std::map<std::vector<std::uint8_t>, int> get_values_result_request_map
      {{FCGI_MAX_CONNS, 0}, {FCGI_MAX_REQS, 1}, {FCGI_MPXS_CONNS, 2}};
  std::vector<uint8_t> local_result {};
  for(std::vector<byte_seq_pair>::iterator pairs_iter {get_value_pairs.begin()};
    (pairs_iter != get_value_pairs.end()) && get_values_result_request_map.size(); 
     ++pairs_iter)
  {
    std::map<std::vector<std::uint8_t>, int>::iterator vpm_iter 
      {get_values_result_request_map.find(pairs_iter->first)};
    if(vpm_iter != get_values_result_request_map.end())
    {
      local_result.clear();
      switch(vpm_iter->second)
      {
        case 0:
          local_result = 
            ToUnsignedCharacterVector(maximum_connection_count_);
          break;
        case 1:
          local_result = 
            ToUnsignedCharacterVector(maximum_request_count_per_connection_);
          break;
        case 2:
          local_result.push_back((maximum_request_count_per_connection_ > 1) ?
            static_cast<std::uint8_t>('1') : static_cast<std::uint8_t>('0'));
          break;
      }
      result_pairs.emplace_back(std::move(pairs_iter->first), 
        std::move(local_result));
      get_values_result_request_map.erase(vpm_iter);
    }
  }
  // Process result pairs to generate the response string.

  // Allocate space for header.
  std::vector<std::uint8_t> result(FCGI_HEADER_LEN, 0U);

  // Since only known names are accepted, assume that the lengths of
  // the names and values can fit in either 7 or 31 bits, i.e. 1 or 4 bytes.
  // (Currently only 1 byte is needed to encode lengths.)
  for(auto pair_iter = result_pairs.begin(); pair_iter != result_pairs.end();
    ++pair_iter)
  {
    // Encode name length.
    std::int_fast32_t item_size(pair_iter->first.size());
    (item_size <= kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      EncodeFourByteLength(item_size, std::back_inserter(result));
    // Encode value length.
    item_size = pair_iter->second.size();
    (item_size <= kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      EncodeFourByteLength(item_size, std::back_inserter(result));
    // Append character bytes of name and value.
    result.insert(result.end(), pair_iter->first.begin(),
      pair_iter->first.end());
    result.insert(result.end(), pair_iter->second.begin(),
      pair_iter->second.end());
  }

  // Prepare to write the response.
  // Note that it is not currently possible to exceed the limit for the
  // content size of a singe record (2^16 - 1 bytes).
  // Pad the record to a multiple of FCGI_HEADER_LEN.
  std::int_fast32_t header_and_content_length(result.size());
  std::int_fast32_t content_length {header_and_content_length
    - FCGI_HEADER_LEN};
    // A safe narrowing conversion.
  std::int_fast32_t remainder(header_and_content_length % FCGI_HEADER_LEN);
  std::int_fast32_t pad_length {};
  if(remainder != 0)
    pad_length = FCGI_HEADER_LEN - remainder;
  else
    pad_length = 0;
  result.insert(result.end(), pad_length, 0);
  PopulateHeader(result.data(),
    FCGIType::kFCGI_GET_VALUES_RESULT, FCGI_NULL_REQUEST_ID,
    content_length, pad_length);

  return SendRecord(connection, result.data(), result.size());
}

bool FCGIServerInterface::
SendRecord(int connection, const uint8_t* buffer_ptr, std::size_t count)
{
  std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator
    mutex_map_iter {write_mutex_map_.find(connection)};
  // Logic error check.
  if(mutex_map_iter == write_mutex_map_.end())
  {
    std::unique_lock<std::mutex> unique_interface_state_lock
      {FCGIServerInterface::interface_state_mutex_, std::defer_lock};
    try
    {
      // ACQUIRE interface_state_mutex_.
      unique_interface_state_lock.lock();
    }
    catch(...)
    {
      std::terminate();
    }
    bad_interface_state_detected_ = true;
    throw std::logic_error {"An expected connection was missing from "
      "write mutex_map_."};
  } // RELEASE interface_state_mutex_.

  // ACQUIRE the write mutex for the connection.
  std::unique_lock<std::mutex> write_lock
    {*(mutex_map_iter->second.first)};
  
  // Check if the connection is corrupt.
  // Part of the discipline for connection use is adding the connection
  // descriptor to one of the closure sets. It need not be done upon corruption
  // discovery.
  if(mutex_map_iter->second.second)
    return false;

  // TODO Have writes on a connection which would be performed by the interface
  // object be performed instead by a worker thread. It expedient but
  // inappropriate to have the interface thread block on a write.
  // 
  // Send record.
  std::size_t number_written {socket_functions::WriteOnSelect(connection, 
    buffer_ptr, count)};
  
  // Check for errors which prevented a full write.
  if(number_written < count)
  {
    // The application must handle SIGPIPE.
    if(errno == EPIPE)
    {
      try
      {
        connections_to_close_set_.insert(connection);
      }
      catch(...)
      {
        // RELEASE the write mutex for the connection as the interface mutex
        // will be acquired and the pattern "wants interface mutex, owns write
        // mutex" is forbidden.
        write_lock.unlock();
        std::unique_lock<std::mutex> unique_interface_state_lock
          {FCGIServerInterface::interface_state_mutex_, std::defer_lock};
        try
        {
          // ACQUIRE interface_state_mutex_.
          unique_interface_state_lock.lock();
        }
        catch(...)
        {
          std::terminate();
        }
        bad_interface_state_detected_ = true;
        throw;
      } // RELEASE interface_state_mutex_.
      return false;
    }
    else 
    {
      if(number_written != 0U)
      {
        // The connection to the client was just corrupted.
        mutex_map_iter->second.second = true;
        try
        {
          connections_to_close_set_.insert(connection);
        }
        catch(...)
        {
          // RELEASE the write mutex for the connection as the interface mutex
          // will be acquired and the pattern "wants interface mutex, owns write
          // mutex" is forbidden.
          write_lock.unlock();

          std::unique_lock<std::mutex> unique_interface_state_lock
            {FCGIServerInterface::interface_state_mutex_, std::defer_lock};
          try
          {
            // ACQUIRE interface_state_mutex_.
            unique_interface_state_lock.lock();
          }
          catch(...)
          {
            std::terminate();
          }
          bad_interface_state_detected_ = true;
          throw;
        } // RELEASE interface_state_mutex_.
      }
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "write from a call to "
        "::socket_functions::WriteOnSelect."};
    }
  }
  return true;
} // RELEASE the write mutex for the connection.

} // namespace fcgi_si
