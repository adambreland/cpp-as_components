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

#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/record_status.h"
#include "include/request_data.h"
#include "include/request_identifier.h"
#include "include/utility.h"

// Class implementation notes:
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
//       6) Implicitly destroy the write mutexes (such as by erasing nodes of
//          write_mutex_map_).
//       7) Update interface state so that any requests which still exist for
//          a connection whose write mutex was destroyed will not try to
//          acquire the destroyed write mutex.
//       8) Release interface_state_mutex_.
//    d) File descriptor invalidation for an active connection by calling close
//       on the descriptor may only occur:
//       Either 1) Under the protection of the interface mutex.
//       Or     2) After the connection_closed_by_interface_ flag has
//                 been set under the protection of the interface mutex.
//       Requests treat mutex validity and file descriptor validity as 
//       equivalent. These properties are currently signaled through the 
//       connection_closed_by_interface_ flag of the RequestData object of the
//       request.
//
// 2) State checks after mutex acquisition:
//    a) Whenever interface_state_mutex_ is obtained with the intention of
//       reading shared interface state, bad_interface_state_detected_ must be 
//       checked. If this flag was set, some part of the shared interface state
//       is corrupt. An exception should be thrown. The interface should be 
//       destroyed.
//    b) Whenever a write mutex is obtained with the intention of writing data
//       to the connection protected by the mutex, the boolean value 
//       associated with the write mutex must be checked. If true, the
//       connection is corrupted. The write cannot proceed.
//
// 3) Invariants on state:
//    a) The sets dummy_descriptor_set_ and application_closure_request_set_
//       should be disjoint before and after the connection cleanup process of
//       AcceptRequests.
//    b) The interface destructor should always be able to safely destroy the
//       interface by:
//       1) Closing the connections in either of write_mutex_map_ or
//          record_status_map_.
//       2) Closing the connections in dummy_descriptor_set_.
//       Any action which would prevent safe destruction must result in
//       program termination.
//    c) If a connection is corrupted from a write which wrote some but not all
//       of its data, the boolean value associated with the write mutex of the
//       connection must be set under the protection of the mutex.
namespace fcgi_si {

// Initialize static class data members.
std::mutex FCGIServerInterface::interface_state_mutex_ {};
unsigned long FCGIServerInterface::interface_identifier_ {0U};
unsigned long FCGIServerInterface::previous_interface_identifier_ {0U};

FCGIServerInterface::
FCGIServerInterface(int listening_descriptor, int max_connections,
  int max_requests, std::int32_t app_status_on_abort)
: listening_descriptor_ {listening_descriptor},
  app_status_on_abort_ {app_status_on_abort},
  maximum_connection_count_ {max_connections},
  maximum_request_count_per_connection_ {max_requests},
  socket_domain_ {}
{
  // Checks that the arguments are within the domain.
  std::string error_message {};
  bool construction_argument_error {false};
  if(max_connections <= 0)
  {
    construction_argument_error = true;
    error_message = "A value less than or equal to zero was given for the "
       "maximum number of transport connections. This value must be greater "
       "than or equal to one.";
  }
  if(max_requests <= 0)
  {
    if(construction_argument_error)
      error_message += '\n';
    else
      construction_argument_error = true;

    error_message += "A value less than or equal to zero was given for the "
      "maximum number of concurrent requests. This value must be greater "
      "than or equal to one.";
  }
  if(construction_argument_error)
    throw std::invalid_argument {error_message};

  // Ensure that the supplied listening socket is non-blocking. This property
  // is assumed in the design of the AcceptRequests loop.
  int flags = fcntl(listening_descriptor_, F_GETFL);
  if(flags == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "fcntl with F_GETFL"};
  }
  flags |= O_NONBLOCK;
  if(fcntl(listening_descriptor_, F_SETFL, flags) == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "fcntl with F_SETFL"};
  }

  // Check socket options.
  // 1) Determine the socket domain. Internet domains may have a list of
  //    authorized IP addresses bound to "FCGI_WEB_SERVER_ADDRS".
  // 2) Check that the socket is a stream socket.
  // 3) Check that the socket is listening.
  int getsockopt_int_buffer {};
  socklen_t getsockopt_int_buffer_size {sizeof(int)};
  int getsockopt_return {};

  while(((getsockopt_return = getsockopt(listening_descriptor_,
    SOL_SOCKET, SO_DOMAIN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1) && (errno == EINTR))
  {
    getsockopt_int_buffer_size = sizeof(int);
  }
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "getsockopt with SO_DOMAIN"};
  }
  int socket_domain {getsockopt_int_buffer};
  socket_domain_ = socket_domain;

  getsockopt_int_buffer_size = sizeof(int);
  while(((getsockopt_return = getsockopt(listening_descriptor_,
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1) && (errno == EINTR))
  {
    getsockopt_int_buffer_size = sizeof(int);
  }
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "getsockopt with SO_TYPE"};
  }
  if(getsockopt_int_buffer != SOCK_STREAM)
    throw std::runtime_error {"The socket used for construction "
      "of an FCGIServerInterface object was not a stream socket."};

  getsockopt_int_buffer_size = sizeof(int);
  while(((getsockopt_return = getsockopt(listening_descriptor_,
    SOL_SOCKET, SO_ACCEPTCONN, &getsockopt_int_buffer,
      &getsockopt_int_buffer_size)) == -1) && (errno == EINTR))
  {
    getsockopt_int_buffer_size = sizeof(int);
  }
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "getsockopt with SO_ACCEPTCONN"};
  }
  if(getsockopt_int_buffer != 1) // The value 1 indicates listening status.
    throw std::runtime_error {"The socket used for construction "
      "of an FCGIServerInterface object\nwas not a listening socket."};

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
      for(/*no-op*/; token_it != end; ++token_it)
      {
        int inet_pton_return {};
        if((inet_pton_return = inet_pton(socket_domain,
          (token_it->str()).data(), inet_address_subaddress_ptr)) == 1)
        {
          if(!inet_ntop(socket_domain, inet_address_subaddress_ptr,
            normalized_address, INET6_ADDRSTRLEN))
          {
            std::error_code ec {errno, std::system_category()};
            throw std::system_error {ec, "inet_ntop"};
          }
          valid_ip_address_set_.insert(normalized_address);
        }
        else if(inet_pton_return == -1)
        {
          std::error_code ec {errno, std::system_category()};
          throw std::system_error {ec, "inet_pton"};
        }
      }

      if(!valid_ip_address_set_.size())
        throw std::runtime_error {"No authorized IP addresses "
          "were found during construction of an FCGIServerInterface object."};
    }
  }

  // Ensure singleton status and update interface_identifier_ to a valid value.

  // ACQUIRE interface_state_mutex_.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};

  if(interface_identifier_)
    throw std::runtime_error {"Construction of an FCGIServerInterface object "
      "occurred when another object was present."};

  // Prevent interface_identifier_ == 0 when a valid interface is present in
  // the unlikely event of integer overflow.
  if(previous_interface_identifier_ < std::numeric_limits<unsigned long>::max())
    previous_interface_identifier_ += 1U;
  else
    previous_interface_identifier_ = 1U;

  interface_identifier_ = previous_interface_identifier_;

  // Create the self-pipe.
  int pipe_fd_array[2] = {};
  if(pipe(pipe_fd_array) < 0) 
  {
    interface_identifier_ = 0U;
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "pipe"};
  }
  self_pipe_read_descriptor_  = pipe_fd_array[0];
  self_pipe_write_descriptor_ = pipe_fd_array[1];
  for(int i {0}; i < 2; ++i) {
    int f_getfl_return {fcntl(pipe_fd_array[i], F_GETFL)};
    f_getfl_return |= O_NONBLOCK;
    int f_setfl_return {fcntl(pipe_fd_array[i], F_SETFL, f_getfl_return)};
    if((f_getfl_return == -1) || (f_setfl_return == -1)) 
    {
      interface_identifier_ = 0U;
      close(self_pipe_read_descriptor_);
      close(self_pipe_write_descriptor_);
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "fcntl"};
    }
  }
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
    
    close(self_pipe_read_descriptor_);
    close(self_pipe_write_descriptor_);

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
  // A local RAII class for the socket descriptor returned from a call to
  // accept.
  class UniqueDescriptor {
   public:

    int get_descriptor() noexcept
    {
      return descriptor_;
    }

    int release_descriptor() noexcept
    {
      int released_descriptor {descriptor_};
      descriptor_ = -1;
      return released_descriptor;
    }

    UniqueDescriptor(int descriptor) noexcept
    : descriptor_ {descriptor}
    {}

    UniqueDescriptor(const UniqueDescriptor&) = delete;
    UniqueDescriptor& operator=(const UniqueDescriptor&) = delete;

    UniqueDescriptor(UniqueDescriptor&& file_manager) noexcept
    {
      descriptor_ = file_manager.descriptor_;
      file_manager.descriptor_ = -1;
    }

    UniqueDescriptor& operator=(UniqueDescriptor&& file_manager) noexcept
    {
      if(descriptor_ != -1)
        close(descriptor_);
      descriptor_ = file_manager.descriptor_;
      file_manager.descriptor_ = -1;
      return *this;
    }

    ~UniqueDescriptor() noexcept
    {
      if(descriptor_ != -1)
        close(descriptor_);
    }

   private:
    int descriptor_ {-1};
  };

  struct sockaddr_storage new_connection_address;
  struct sockaddr* address_ptr
    {static_cast<struct sockaddr*>(static_cast<void*>(&new_connection_address))};
  socklen_t new_connection_address_length = sizeof(struct sockaddr_storage);
  int accept_return {};

  while((accept_return = accept(listening_descriptor_, address_ptr, 
    &new_connection_address_length)) == -1 && (errno == EINTR || 
    errno == ECONNABORTED))
  {
    new_connection_address_length = sizeof(struct sockaddr_storage);
  }
  if(accept_return == -1)
  {
    if(errno == EWOULDBLOCK || errno == EAGAIN)
      return -1;
    else
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "accept"};
    }
  }
  // With so many circumstances that may require file closure to prevent a
  // leak, use an RAII class until it is known that the file should not be
  // closed on function exit. An error from close is ignored.
  UniqueDescriptor managed_descriptor {accept_return};

  int getsockopt_int_buffer {};
  socklen_t getsockopt_int_buffer_size {sizeof(int)};
  int getsockopt_return {};

  while((getsockopt_return = getsockopt(managed_descriptor.get_descriptor(),
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 && errno == EINTR)
  {
    getsockopt_int_buffer_size = sizeof(int);
  }
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "getsockopt with SO_TYPE"};
  }
  int new_socket_type {getsockopt_int_buffer};

  // Check if the interface is overloaded, the maximum connection count was
  // met, or the socket is of an incorrect type. Reject by closing if so.
  if(application_overload_                                 || 
    (record_status_map_.size() >= 
     static_cast<unsigned int>(maximum_connection_count_)) ||
    (new_socket_type != SOCK_STREAM))
  {
    return 0;
  }

  // Reset the buffer length value-result variable.
  getsockopt_int_buffer_size = sizeof(int);
  while(((getsockopt_return = getsockopt(managed_descriptor.get_descriptor(),
    SOL_SOCKET, SO_DOMAIN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1) && (errno == EINTR))
  {
    getsockopt_int_buffer_size = sizeof(int);
  }
  if(getsockopt_return == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "getsockopt with SO_DOMAIN"};
  }
  int new_socket_domain {getsockopt_int_buffer};

  // Perform address validation against the list of authorized addresses
  // if applicable. A non-empty set implies an internet domain.
  std::string new_address {};
  bool valid_address {true};
  if(valid_ip_address_set_.size())
  {
    char address_array[INET6_ADDRSTRLEN];
    void* addr_ptr {nullptr};

    // Handle both IPv4 and IPv6.
    if(new_socket_domain == AF_INET)
    {
      struct sockaddr_in* inet_addr_ptr {static_cast<struct sockaddr_in*>(
        static_cast<void*>(&new_connection_address))};
      addr_ptr = &(inet_addr_ptr->sin_addr);
    }
    else if(new_socket_domain == AF_INET6)
    {
      struct sockaddr_in6* inet_addr_ptr {static_cast<struct sockaddr_in6*>(
        static_cast<void*>(&new_connection_address))};
      addr_ptr = &(inet_addr_ptr->sin6_addr);
    }
    
    if(addr_ptr)
    {     
      if(!inet_ntop(new_socket_domain, addr_ptr, address_array, 
        INET6_ADDRSTRLEN))
      {
        std::error_code ec {errno, std::system_category()};
        throw std::system_error(ec, "inet_ntop");
      }
      new_address = address_array;
      valid_address = (valid_ip_address_set_.find(new_address) 
        != valid_ip_address_set_.end());
    }
    else
      valid_address = false;
  }

  // Validate the new connected socket against domain and address.
  if(!((new_socket_domain == socket_domain_) && valid_address))
    return 0;

  // Make the accepted connected sockets non-blocking.
  int flags {};
  while(((flags = fcntl(managed_descriptor.get_descriptor(), F_GETFL)) == -1)
    && (errno == EINTR))
    continue;
  if(flags == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "fcntl with F_GETFL"};
  }

  flags |= O_NONBLOCK;

  while(((flags = fcntl(managed_descriptor.get_descriptor(), F_SETFL, flags)) 
    == -1) && (errno == EINTR))
    continue;
  if(flags == -1)
  {
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "fcntl with F_SETFL"};
  }

  // NON-LOCAL STATE modification block start.
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
      managed_descriptor.get_descriptor(), 
      RecordStatus {managed_descriptor.get_descriptor(), this});

    std::unique_ptr<std::mutex> new_mutex_manager {new std::mutex {}};
    write_mutex_map_insert_return = write_mutex_map_.insert(
      {managed_descriptor.get_descriptor(), 
      std::pair<std::unique_ptr<std::mutex>, bool> 
        {std::move(new_mutex_manager), false}});

    request_count_map_emplace_return = request_count_map_.emplace(
        managed_descriptor.get_descriptor(), 0);

    if(!(record_status_map_emplace_return.second
        && write_mutex_map_insert_return.second
        && request_count_map_emplace_return.second))
      throw std::logic_error {"Socket descriptor emplacement "
        "failed due to duplication."};
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
    }
    catch(...)
    {
      // Termination is necessary as a double closure may occur on the
      // descriptor. It will be called once on on exit and may be called
      // in the desctructor of the FCGIServerInterface object.
      std::terminate();
    }
    throw;
  }
  // NON-LOCAL STATE modification block end.

  return managed_descriptor.release_descriptor();
} // RELEASE interface_state_mutex_.

std::vector<FCGIRequest> FCGIServerInterface::AcceptRequests()
{
  auto InterfaceCheck = [this]()->void
  {
    if(bad_interface_state_detected_)
      throw std::runtime_error {"The interface was found to be "
        "corrupt in a call to "
        "fcgi_si::FCGIServerInterface::AcceptRequests."};
  };
  
  // Check for previously-created requests that could not be returned because
  // of an error.
  if(request_buffer_on_throw_.size())
    return std::move(request_buffer_on_throw_);

  // CLEANUP CONNECTIONS
  {
    // Start of interface_state_lock handling block.

    // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock 
      {FCGIServerInterface::interface_state_mutex_};
    InterfaceCheck();

    // Remove dummy descriptors if possible.
    //
    // Exception safety: 
    // Removal of a descriptor from dummy_descriptor_set_ and calling close on
    // that descriptor must be transactional. If performance of these actions
    // was not a transactional step, the following scenario is possible:  
    // 1) The descriptor is released for use by calling close but is not 
    //    removed from dummy_descriptor_set_.
    // 2) The descriptor is allocated for use by the application.
    // 3) When the destructor of the interface executes, the descriptor, which
    //    is now in use by the application, is spuriously closed as the 
    //    descriptor remained in dummy_descriptor_set_.
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

          if(!request_count_map_.erase(connection_to_be_closed))
            throw std::logic_error {"An expected connection was not present "
              "in request_count_map_ during connection cleanup in a call "
              "to fcgi_si::FCGIServerInterface::AcceptRequests."};

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

    // Clear the self-pipe. It may have been written to to wake up the
    // interface while it was blocked waiting for incoming connections
    // or data.
    int read_return {};
    constexpr int bl {32};
    std::uint8_t read_buffer[bl];
    while((read_return = read(self_pipe_read_descriptor_, read_buffer, bl)) > 0)
      continue;
    if(read_return == 0) 
    {
      bad_interface_state_detected_ = true;
      throw std::logic_error {"The self-pipe of the interface was found "
        "to be closed for reading."};
    }
    if(errno != EAGAIN) 
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "read"};
    }

    // Close connection descriptors for which closure was requested.
    // Update interface state to allow FCGIRequest objects to inspect for
    // connection closure. 
    //
    // Note that dummy_descriptor_set_ is disjoint from 
    // application_closure_request_set_. This is necessary as the presence of a
    // descriptor in both categories of descriptors may result in double
    // closure.
    //
    // TODO Formally prove this property and include proof in supporting
    // documentation.
    try
    {
      for(int connection : application_closure_request_set_)
      {
        bool connection_removed {RemoveConnection(connection)};
        if(!connection_removed)
          throw std::logic_error {"A connection could not be removed because "
            "a write mutex was erroneously held."};
      }
    }
    catch(...)
    {
      bad_interface_state_detected_ = true;
      throw;
    }
  } // RELEASE interface_state_mutex_;

  // DESCRIPTOR MONITORING

  // TODO Determine a better I/O multiplexing system call or library function.
  // For example, some glibc implementations have an upper limit of 1023 for
  // file descriptor values due to the size of FD_SET (which is just an integer
  // type used as a bitset).

  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(listening_descriptor_, &read_set);
  FD_SET(self_pipe_read_descriptor_, &read_set);
  int number_for_select 
    {std::max<int>(listening_descriptor_, self_pipe_read_descriptor_) + 1};
  // Reverse to access highest fd immediately.
  auto map_reverse_iter = record_status_map_.rbegin();
  if(map_reverse_iter != record_status_map_.rend())
  {
    number_for_select = std::max<int>(number_for_select, 
      (map_reverse_iter->first) + 1); 
    for(/*no-op*/; map_reverse_iter != record_status_map_.rend();
      ++map_reverse_iter)
      FD_SET(map_reverse_iter->first, &read_set);
  }

  int select_return {};
  while((select_return =
    select(number_for_select, &read_set, nullptr, nullptr, nullptr))
    == -1 && (errno == EINTR || errno == EAGAIN))
    continue;
  if(select_return == -1)
  {
    // TODO Are there any situations that could cause select to return EBADF
    // from a call with only a non-null read set other than one of the file
    // descriptors not being open?
    if(errno == EBADF)
    {
      try
      {
        // ACQUIRE interface_state_mutex_
        std::unique_lock<std::mutex> interface_state_lock
          {FCGIServerInterface::interface_state_mutex_};
        bad_interface_state_detected_ = true;
      } // RELEASE interface_state_mutex_
      catch(...)
      {
        std::terminate();
      }
    } 
    std::error_code ec {errno, std::system_category()};
    throw std::system_error {ec, "select"};
  }

  // Check if the interface was corrupted while it blocked on select.
  { // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock 
      {FCGIServerInterface::interface_state_mutex_};
    InterfaceCheck();
  } // RELEASE interface_state_mutex_.

  std::vector<FCGIRequest> requests {};

  // This list length variable is assigned to at the end of each iteration of
  // the below for loop. It allows the following case to be detected: 1) some
  // FCGIRequest objects were added to requests in a loop iteration 2) a throw
  // occurred in that iteration. In this case, length_at_loop and 
  // requests.size() will differ.
  std::vector<FCGIRequest>::size_type length_at_loop {0U};

  // This variable allows the number of connected sockets read in the below
  // loop to be tracked so that a comparison of select_return and it determines
  // if select found that peers were waiting for socket connection requests
  // to be accepted.
  int connections_read {0};

  // This variable 1) serves as the value of the current file descriptor
  // where that information is needed in function calls in the loop below and 
  // 2) allows, in the case that a throw occurred during a loop iteration, the
  // value of the file descriptor during the iteration to be in scope in the
  // catch block below.
  int current_connection {};
  try
  {
    for(auto it = record_status_map_.begin();
        (it != record_status_map_.end()) && (connections_read < select_return);
        ++it)
    {
      current_connection = it->first;
      // Call ReadRecords and construct FCGIRequest objects for any application
      // requests which are complete and ready to be passed to the application.
      if(FD_ISSET(current_connection, &read_set))
      {
        connections_read++;
        std::vector<RequestIdentifier> request_identifiers
          {it->second.ReadRecords()};
        if(request_identifiers.size())
        {
          // ACQUIRE interface_state_mutex_.
          std::unique_lock<std::mutex> unique_interface_state_lock
            {FCGIServerInterface::interface_state_mutex_};
          InterfaceCheck();

          std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator 
            write_mutex_map_iter {write_mutex_map_.find(current_connection)};
          if(write_mutex_map_iter == write_mutex_map_.end())
          {
            bad_interface_state_detected_ = true;
            throw std::logic_error {"An expected write mutex and flag pair "
              "was not present in write_mutex_map_ in a call to "
              "fcgi_si::FCGIServerInterface::AcceptRequests."};
          }
          std::mutex* write_mutex_ptr 
            {write_mutex_map_iter->second.first.get()};
          bool* write_mutex_bad_state_ptr
            {&(write_mutex_map_iter->second.second)};

          // For each request_id, find the associated RequestData object, extract
          // a pointer to it, and create an FCGIRequest object from it.
          for(RequestIdentifier request_id : request_identifiers)
          {
            std::map<RequestIdentifier, RequestData>::iterator request_data_iter
              {request_map_.find(request_id)};
            if(request_data_iter == request_map_.end())
            {
              bad_interface_state_detected_ = true;
              throw std::logic_error {"An expected request "
                "was not present in request_map_ in a call to "
                "fcgi_si::FCGIServerInterface::AcceptRequests."};
            }
            RequestData* request_data_ptr {&(request_data_iter->second)};
            
            // This is a rare instance where an FCGIRequest may be destroyed
            // within the scope of implementation code. The destructor of
            // FCGIRequest objects tries to acquire interface_state_mutex_.
            // See the catch block immediately below.
            FCGIRequest request {request_id,
              FCGIServerInterface::interface_identifier_, this, 
              request_data_ptr, write_mutex_ptr, write_mutex_bad_state_ptr, 
              self_pipe_write_descriptor_};
            try
            {
              requests.push_back(std::move(request));
            }
            catch(...)
            {
              // Conditionally RELEASE interface_state_mutex_ so that
              // deadlock will not occur when the destructor of request
              // executes.
              unique_interface_state_lock.unlock();
              throw;
            }
          }
          length_at_loop = requests.size();
        } // RELEASE interface_state_mutex_.
      }
    }
    // Accept new connections if some are present.
    if(connections_read < select_return)
      while(AcceptConnection() != -1)
        continue;
  }
  catch(...)
  {
    if(requests.size() == 0)
      throw;

    std::unique_lock<std::mutex> unique_interface_state_lock
        {FCGIServerInterface::interface_state_mutex_, std::defer_lock};
    try
    {
      // We need to check if there is a point to try to preserve the request
      // objects which were created.
      //
      // ACQUIRE interface_state_mutex_
      unique_interface_state_lock.lock();
    }
    catch(...)
    {
      std::terminate();
    }

    if(!bad_interface_state_detected_) // We can save the requests.
    {
      try
      {
        for(std::vector<FCGIRequest>::size_type i {0}; i < length_at_loop; ++i)
          request_buffer_on_throw_.push_back(std::move(requests[i]));
      }
      catch(...)
      {
        bad_interface_state_detected_ = true;
        throw;
      }
      
      if(requests.size() > length_at_loop)
      {
        try
        {
          application_closure_request_set_.insert(current_connection);
        }
        catch(...)
        {
          std::terminate();
        }
      }
    }
    throw;
  } // RELEASE interface_state_mutex_.

  return requests;
}

// Synchronization:
// 1) interface_state_mutex_ must be held prior to a call.
void FCGIServerInterface::AddRequest(RequestIdentifier request_id, 
  std::uint16_t role, bool close_connection)
{
  std::map<int, int>::iterator request_count_iter {};
  std::map<RequestIdentifier, RequestData>::iterator request_map_iter {};

  try
  { 
    request_count_iter = request_count_map_.find(request_id.descriptor());
    if(request_count_iter == request_count_map_.end())
    {
      throw std::logic_error {"request_count_map_ did not possess an "
        "expected file descriptor key."};
    }

    request_map_iter = request_map_.find(request_id);
    if(request_map_iter != request_map_.end())
    {
      throw std::logic_error {"An attempt was made to add an element to "
        "request_map_ with a key which was already present."};
    }
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
  
  request_count_iter->second++;
  try
  { 
    // Insertion has no effect on a throw.
    request_map_[request_id] = RequestData(role, close_connection);
  }
  catch(...)
  {
    request_count_iter->second--;
    throw;
  }
}

bool FCGIServerInterface::interface_status() const
{
  // ACQUIRE interface_state_mutex_.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  return !bad_interface_state_detected_;
} // RELEASE interface_state_mutex_.

// Synchronization:
// 1) interface_state_mutex_ must be held prior to a call.
bool FCGIServerInterface::RemoveConnection(int connection)
{
  // Care must be taken to prevent descriptor leaks or double closures.

  // A lambda which checks for the presence of the connection in and attempts
  // to erase the connection from record_status_map_ and write_mutex_map_.
  // Terminates the program if erasure doesn't or can't occur.
  auto EraseConnectionOrTerminate = [this](int connection, 
    bool erase_request_count)->void
  {
    try
    {
      std::map<int, RecordStatus>::iterator record_iter 
        {record_status_map_.find(connection)};
      std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator
        write_iter {write_mutex_map_.find(connection)}; 
      std::map<int, int>::iterator request_count_iter {};
      if(erase_request_count)
        request_count_iter = request_count_map_.find(connection);

      if(record_iter == record_status_map_.end() 
         || write_iter == write_mutex_map_.end()
         || (erase_request_count && 
             (request_count_iter == request_count_map_.end())))
        throw std::logic_error {"An expected connection was not present in "
          "at least one of record_status_map_, write_mutex_map_, and "
          "request_count_map_ in a call to "
          "fcgi_si::FCGIServerInterface::RemoveConnection."};

      record_status_map_.erase(record_iter);
      write_mutex_map_.erase(write_iter);
      if(erase_request_count)
        request_count_map_.erase(request_count_iter);
    }
    catch(...)
    {
      std::terminate();
    }
  };

  try
  {
    std::unique_lock<std::mutex> unique_write_lock 
      {*(write_mutex_map_.at(connection).first), std::defer_lock};
    // Attempt to ACQUIRE the write mutex of the connection. If acquired, 
    // RELEASE. This process ensures that a request does not hold the write
    // lock while the connection is being erased. This in ensured as the
    // interface mutex is held over the entire process.
    bool locked {unique_write_lock.try_lock()};
    if(!locked)
      return false;
    unique_write_lock.unlock();

    bool assigned_requests {RequestCleanupDuringConnectionClosure(connection)};
    // Close the connection in one of two ways.
    if(assigned_requests)
    {
      // Go through the process to make the descriptor a dummy.
      // Implicitly and atomically call close(connection).
      //
      // TODO Should a way to check for errors on the implicit closure of
      // connection be implemented?
      int dup2_return {};
      while((dup2_return = dup2(listening_descriptor_, connection)) == -1)
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
      EraseConnectionOrTerminate(connection, false);
    }
    else // No requests for the connection.
    {
      // Synchronization note:
      // Order as given. If erasure is not ordered before the call of
      // close(connection), it is possible that erasure does not occur and
      // close(connection) will be called twice.
      EraseConnectionOrTerminate(connection, true);
      int close_return {close(connection)};
      if(close_return == -1 && errno != EINTR)
      {
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "close"};
      }
    }
    return true;
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
  for(int i {0}; i < 4; i++)
  {
    app_status_byte_array[i] = app_status >> (24 - (8*i));
  }

  // Set header.
  PopulateHeader(result.data(), FCGIType::kFCGI_END_REQUEST,
    request_id.FCGI_id(), FCGI_HEADER_LEN, 0);
  // Set body.
  for(int i {0}; i < 4; i++)
  {
    result[(i + 1) + kHeaderReservedByteIndex] = app_status_byte_array[i];
  }
  result[5 + kHeaderReservedByteIndex]   = protocol_status;
  // Remaining bytes were set to zero during initialization.

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
  result[1 + kHeaderReservedByteIndex] = static_cast<uint8_t>(type);
  // Remaining bytes were set to zero during initialization.

  return SendRecord(connection, result.data(), result.size());
}

bool FCGIServerInterface::
SendGetValuesResult(int connection, const std::uint8_t* buffer_ptr, 
  std::int_fast32_t count)
{
  using byte_seq_pair = std::pair<std::vector<std::uint8_t>, 
    std::vector<std::uint8_t>>;

  // If count is zero or if the sequence of bytes given by the range
  // [buffer_ptr, buffer_ptr + count) contains a FastCGI name-value pair format
  // error, the vector returned by ExtractBinaryNameValuePairs is empty. In 
  // either case, an empty FCGI_GET_VALUES_RESULT record will be sent to the 
  // client. If the client included requests, the absence of those variables 
  // in the response will correctly indicate that the request was not 
  // understood (as, in this case, an error will have been present).
  std::vector<byte_seq_pair> get_value_pairs
    {ExtractBinaryNameValuePairs(buffer_ptr, count)};

  // The following for loop constructs result pairs as a list with elements
  // of type byte_seq_pair. This process disregards any name that is not 
  // understood and omits duplicates. The map is used to track which
  // FCGI_GET_VALUES requests are understood (three are specified in the 
  // standard) and which requests have already occurred. Once a request type is
  // seen, it is removed from the map. Processing stops once all requests have 
  // been seen or the list of understood FCGI_GET_VALUES requests is exhausted.

  std::vector<byte_seq_pair> result_pairs {};

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

// Implementation note:
// The write mutex is acquired if the interface must schedule the connection
// which is asociated with the write mutex for closure. This is done to allow
// destruction of the write mutex without a prior, potentially-blocking call to
// acquire the write mutex. It is planned that writes by the interface will
// eventually be made by a separate thread on behalf of the interface.
// Non-blocking write mutex destruction combined with interface writes being
// made by a separate thread would allow the interface thread to never block on
// write mutex acquisition during normal operation.
bool FCGIServerInterface::
SendRecord(int connection, const std::uint8_t* buffer_ptr, 
  std::int_fast32_t count)
{
  if(count < 0)
    throw std::logic_error {"A negative value was provided for count."};

  std::unique_lock<std::mutex> unique_interface_state_lock
    {FCGIServerInterface::interface_state_mutex_, std::defer_lock};

  std::map<int, std::pair<std::unique_ptr<std::mutex>, bool>>::iterator
    mutex_map_iter {write_mutex_map_.find(connection)};
  // Defensive check on write mutex existence for connection.
  if(mutex_map_iter == write_mutex_map_.end())
  {
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
      "write_mutex_map_."};
  } // RELEASE interface_state_mutex_ on throw.

  // ACQUIRE the write mutex for the connection.
  std::unique_lock<std::mutex> unique_write_lock
    {*(mutex_map_iter->second.first)};
  
  // Check if the connection is corrupt.
  if(mutex_map_iter->second.second)
    // Insertion to application_closure_request_set_ is not necessary. Part of
    // the discipline for writing to a connection is adding the descriptor to
    // the closure set in the event of corruption.
    return false;

  // TODO Have writes on a connection which would be performed by the interface
  // object be performed instead by a worker thread. It is expedient but
  // inappropriate to have the interface thread block on a write.
  // 
  // Send record.
  struct timeval timeout {write_block_timeout, 0};
  std::size_t number_written {socket_functions::WriteOnSelect(connection, 
    buffer_ptr, count, &timeout)};
  
  // Check for errors which prevented a full write.
  if(number_written < static_cast<std::size_t>(count))
  {
    // Indicate that the connection is corrupt if it is still open and some 
    // data was written.
    if(number_written != 0U)
      mutex_map_iter->second.second = true;
    // RELEASE the write mutex for the connection (as the pattern "has write
    // mutex, wants interface mutex" is forbidden).
    unique_write_lock.unlock();

    // Add the connection to the closure set.
    try
    {
      // ACQUIRE interface_state_mutex_.
      unique_interface_state_lock.lock();
    }
    catch(...)
    {
      std::terminate();
    }
    try
    {
      // ACQUIRE the write lock. This prevents a request thread from holding
      // the write lock once the connection has been added to the closure set
      // and the current thread releases the interface mutex.
      unique_write_lock.lock();
      application_closure_request_set_.insert(connection);
      // RELEASE the write lock.
      unique_write_lock.unlock();
    }
    catch(...)
    {
      bad_interface_state_detected_ = true;
      throw;
    }

    if((errno == EPIPE) || (errno == 0))
      return false;
    else // Any other error is considered exceptional.
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "An error from a call to "
        "socket_functions::WriteOnSelect."};
    } 
  }
  return true;
} // RELEASE the write mutex for the connection.

} // namespace fcgi_si
