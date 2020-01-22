// Unix system call headers.
  // General headers.
#include <unistd.h>
#include <sys/types.h>    // General. Also included for portability for
                          // <sys/socket.h>.
#include <fcntl.h>        // For file status flag manipulation.
#include <syslog.h>       // For central logging.
  // Socket headers.
#include <sys/socket.h>
#include <netinet/in.h>   // Defines constants for use with inet_ntop().
#include <arpa/inet.h>    // For inet_pton() and inet_ntop().
  // I/O multiplexing headers.
#include <sys/time.h>     // For portability for <sys/select.h>.
#include <sys/select.h>

// C standard library headers in the C++ standard library.
#include <cerrno>
#include <cstdint>          // For std::uint8_t and others.
#include <cstdlib>          // For std::getenv(), std::size_t, and EXIT_FAILURE.
// C++ standard library headers.
#include <stdexcept>        // For the standard exception classes.
#include <mutex>
#include <regex>
#include <utility>
#include <memory>

#include "external/error_handling/include/error_handling.h"
#include "external/linux_scw/include/linux_scw.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/pair_processing.h"
#include "include/protocol_constants.h"
#include "include/record_status.h"
#include "include/request_data.h"
#include "include/request_identifier.h"
#include "include/utility.h"

// Public member functions

fcgi_si::FCGIServerInterface::
FCGIServerInterface(uint32_t max_connections, uint32_t max_requests,
  uint16_t role, int32_t app_status_on_abort = EXIT_FAILURE)
: maximum_connection_count_ {max_connections},
  maximum_request_count_per_connection_ {max_requests},
  role_ {role},
  app_status_on_abort_ {app_status_on_abort}
{
  // Check that the arguments are within the domain.
  std::string error_message {};
  bool construction_argument_error {false};
  // Check that the role is supported.
  if(role_ != FCGI_RESPONDER)
  {
    construction_argument_error = true;
    error_message += "An FCGIServerInterface object could not be constructed\n";
    error_message += "as the provided role is not supported.\n";
    error_message += "Provided role: ";
    error_message += std::to_string(role_);
  }
  if(max_connections == 0)
  {
    if(construction_argument_error)
      error_message += '\n';
    else
      construction_argument_error = true;
    error_message += "A value of zero was given for the maximum number of transport connections.\n";
    error_message += "This value must be greater than or equal to one.";
  }
  if(max_requests == 0)
  {
    if(construction_argument_error)
      error_message += '\n';
    else
      construction_argument_error = true;
    error_message += "A value of zero was given for the maximum number of concurrent requests.\n";
    error_message += "This value must be greater than or equal to one.";
  }
  if(construction_argument_error)
    throw std::invalid_argument {ERROR_STRING(error_message)};

  // Ensure that the supplied listening socket is non-blocking. This property
  // is assumed in the design of the AcceptRequests() loop.
  int flags = fcntl(fcgi_si::FCGI_LISTENSOCK_FILENO, F_GETFL);
  if(flags == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_GETFL")};
  flags |= O_NONBLOCK;
  if(fcntl(fcgi_si::FCGI_LISTENSOCK_FILENO,
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

  while((getsockopt_return = getsockopt(fcgi_si::FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_DOMAIN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 && errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_DOMAIN")};
  int socket_domain {getsockopt_int_buffer};
  socket_domain_ = socket_domain;

  getsockopt_int_buffer_size = sizeof(int);
  while((getsockopt_return = getsockopt(fcgi_si::FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 & errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_TYPE")};
  if(getsockopt_int_buffer != SOCK_STREAM)
    throw std::runtime_error {ERROR_STRING("The socket used for construction of an FCGIServerInterface object\nwas not a stream socket.")};

  getsockopt_int_buffer_size = sizeof(int);
  while((getsockopt_return = getsockopt(fcgi_si::FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_ACCEPTCONN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 & errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_ACCEPTCONN")};
  if(getsockopt_int_buffer != 1) // The value 1 is used to indicate listening status.
    throw std::runtime_error {ERROR_STRING("The socket used for construction of an FCGIServerInterface object\nwas not a listening socket.")};

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
    if(ip_address_list.size() != 0) // A non-empty address list was bound.
    {
      // Declare appropriate variables to use with inet_pton() and inet_ntop().
      // These structs are internal to struct sockaddr_in and struct sockaddr_in6.
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
      // every well-formed address to the set of authorized addresses.
      for(; token_it != end; ++token_it)
      {
        int inet_pton_return {};
        if((inet_pton_return = inet_pton(socket_domain, (token_it->str()).data(),
          inet_address_subaddress_ptr)) == 1)
        {
          if(!inet_ntop(socket_domain, inet_address_subaddress_ptr,
            normalized_address, INET6_ADDRSTRLEN))
            throw std::runtime_error {ERRNO_ERROR_STRING("inet_ntop")};
          valid_ip_address_set_.insert(normalized_address);
        }
        else if(inet_pton_return == -1)
          throw std::runtime_error {ERRNO_ERROR_STRING("inet_pton")};
      }
    } // End non-empty environment variable value check.
  } // End internet domain check.
}

int fcgi_si::FCGIServerInterface::
AcceptConnection()
{
  struct sockaddr_storage new_connection_address;
  socklen_t new_connection_address_length = sizeof(struct sockaddr_storage);
  int accept_return {};
  while((accept_return = accept(fcgi_si::FCGI_LISTENSOCK_FILENO,
    (struct sockaddr*)&new_connection_address, &new_connection_address_length)) == -1
    && errno == EINTR)
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
      throw std::runtime_error(ERRNO_ERROR_STRING("accept"));
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
    == -1 && errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_DOMAIN")};
  int new_socket_domain {getsockopt_int_buffer};

  getsockopt_int_buffer_size = sizeof(int);
  while((getsockopt_return = getsockopt(new_socket_descriptor,
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 && errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_DOMAIN")};
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
      throw std::runtime_error {ERRNO_ERROR_STRING("inet_ntop")};
    new_address = address_array;
  }

  // Validate the new connected socket against the gathered information.
  if(new_socket_domain != socket_domain_ || new_socket_type != SOCK_STREAM
     || (valid_ip_address_set_.size() > 0
         && (valid_ip_address_set_.find(new_address) == valid_ip_address_set_.end())))
  {
    try {linux_scw::CloseWithErrorCheck(new_socket_descriptor);}
    catch(std::runtime_error& e)
    {
      std::string error_message {e.what()};
      error_message += ERROR_STRING("A call to CloseWithErrorCheck threw an error which could not be handled.");
      throw std::runtime_error {error_message};
    }
    return 0;
  }

  // Make the accepted connected sockets non-blocking.
  int flags {};
  while((flags = fcntl(new_socket_descriptor, F_GETFL)) == -1
    && errno == EINTR){}
  if(flags == -1);
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_GETFL")};

  flags |= O_NONBLOCK;

  while((flags = fcntl(new_socket_descriptor, F_SETFL)) == -1
    && errno == EINTR){}
  if(flags == -1);
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_SETFL")};

  // Update interface state to reflect the new connection.
  record_status_map_[new_socket_descriptor] = RecordStatus {this};
  std::unique_ptr<std::mutex> new_mutex_manager {new std::mutex};
  write_mutex_map_[new_socket_descriptor] = std::move(new_mutex_manager);
  request_count_map_[new_socket_descriptor] = 0;

  return new_socket_descriptor;
}

std::vector<fcgi_si::FCGIRequest>
fcgi_si::FCGIServerInterface::
AcceptRequests()
{
  // Result to return.
  std::vector<fcgi_si::FCGIRequest> requests {};

  // Process connections which were found to have been closed by the peer
  // and connections which were requested to be closed through FCGIRequest
  // objects. Start with connections_found_closed_set_ as we can remove
  // connections which can be closed in this set from
  // application_closure_request_set_.
  std::vector<int> connection_skip_set {};
  {
    // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
    for(auto closed_conn_iter {connections_found_closed_set_.begin()};
        closed_conn_iter != connections_found_closed_set_.end();
        /*no-op*/)
    {
      // Check if there are no requests and, hence, no assigned requests.
      // Unassigned requests were removed when the connection was added to the
      // set and cannot be generated after that addition. This is because the
      // connection was removed from record_status_map_.
      if(request_count_map_[*closed_conn_iter] == 0)
      {
        // application_closure_request_set_ is handled by the call.
        RemoveConnectionFromSharedState(*closed_conn_iter);
        // Safely remove the connection from connections_found_closed_set_.
        auto closed_conn_iter_erase = closed_conn_iter;
        ++closed_conn_iter;
        connections_found_closed_set_.erase(closed_conn_iter_erase);
      }
      else // Requests are present. There is no point to search again.
      {
        connection_skip_set.push_back(*closed_conn_iter);
        ++closed_conn_iter;
      }
    }
    auto connection_skip_set_iter {connection_skip_set.begin()};
    for(auto close_request_conn_iter {application_closure_request_set_.begin()};
        close_request_conn_iter != application_closure_request_set_.end();
        /*no-op*/)
    {
      // Maintain skip set discipline and skip connections which have requests.
      if(connection_skip_set_iter != connection_skip_set.end())
      {
        if(*close_request_conn_iter > *connection_skip_set_iter)
          ++connection_skip_set_iter;
        else if(*close_request_conn_iter == *connection_skip_set_iter)
        {
          ++connection_skip_set_iter;
          ++close_request_conn_iter;
          continue;
        }
      }
      // Check for requests which have been exposed to the application.
      // Requests which have not been received in full are deleted.
      bool assigned_req {UnassignedRequestCleanup(*close_request_conn_iter)};
      if(!assigned_req)
      {
        // Safely remove the connection from application_closure_request_set_.
        auto close_request_conn_erase_iter {close_request_conn_iter};
        ++close_request_conn_iter;
        record_status_map_.erase(*close_request_conn_erase_iter);
        // Connection removed from application_closure_request_set_ here.
        RemoveConnectionFromSharedState(*close_request_conn_erase_iter);
      }
    }
  } // RELEASE interface_state_mutex_.

  // Construct descriptor set to wait on for select.
  fd_set read_set;
  FD_ZERO(&read_set);
  // Add listening socket to read_set for new connections.
  FD_SET(fcgi_si::FCGI_LISTENSOCK_FILENO, &read_set);
  // 0 + 1 = 1 Set for proper select call if no connected sockets are present.
  int number_for_select {1};
  // Reverse to access highest fd immediately.
  auto map_reverse_iter = record_status_map_.rbegin();
  if(map_reverse_iter != record_status_map_.rend())
  {
    number_for_select = (map_reverse_iter->first) + 1; // Use (highest fd) + 1.
    for(; map_reverse_iter != record_status_map_.rend(); ++map_reverse_iter)
      FD_SET(map_reverse_iter->first, &read_set);
  }

  // Wait for data to be available or new connections to be available.
  int select_return {};
  while((select_return =
    select(number_for_select, &read_set, nullptr, nullptr, nullptr))
    == -1 && (errno == EINTR || errno == EAGAIN)){}
  if(select_return == -1) // Unrecoverable error from select().
    throw std::runtime_error {ERRNO_ERROR_STRING("select")};
  // Check connected sockets (as held in record_status_map_) before the
  // listening socket.
  int connections_read {0};
  std::mutex* interface_state_mutex_ptr {&interface_state_mutex_};
  for(auto it = record_status_map_.begin();
      (it != record_status_map_.end()) && (connections_read < select_return);
      ++it)
  {
    // Extract file descriptor and check if it is ready.
    int fd {it->first};
    if(FD_ISSET(fd, &read_set))
    {
      connections_read++;
      std::vector<fcgi_si::RequestIdentifier> request_identifiers
        {it->second.Read(fd)};
      if(request_identifiers.size())
      {
        std::mutex* write_mutex_ptr {write_mutex_map_.find(fd)->second.get()};
        // ACQUIRE interface_state_mutex_.
        std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
        // For each request_id, find the associated RequestData object, extract
        // a pointer to it, and create an FCGIRequest object from it.
        for(RequestIdentifier request_id : request_identifiers)
        {
          fcgi_si::RequestData* request_data_ptr {nullptr};
          request_data_ptr = &(request_map_.find(request_id)->second);
          fcgi_si::FCGIRequest request {request_id, this, request_data_ptr,
            write_mutex_ptr, interface_state_mutex_ptr};
          requests.push_back(std::move(request));
        } // RELEASE interface_state_mutex_.
      }
    }
  }
  // Accept new connections if some are present.
  if(connections_read < select_return)
    while(AcceptConnection() != -1){}

  return requests;
}

void fcgi_si::FCGIServerInterface::
ClosedConnectionFoundDuringAcceptRequests(int connection)
{
  // Remove the connection from the record_status_map_ so that it is not
  // included in a call to select().
  record_status_map_.erase(connection);

  // Iterate over requests: delete unassigned requests and check for
  // assigned requests.
  // ACQUIRE interface_state_mutex_ to access and modify shared state.
  std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
  bool active_requests_present {UnassignedRequestCleanup(connection)};
  // Check if the connection can be closed or assigned requests require
  // closure to be delayed.
  if(!active_requests_present)
    RemoveConnectionFromSharedState(connection);
  else
    connections_found_closed_set_.insert(connection);
} // RELEASE interface_state_mutex_.

fcgi_si::RequestIdentifier fcgi_si::FCGIServerInterface::
ProcessCompleteRecord(int connection, RecordStatus* record_status_ptr)
{
  fcgi_si::RequestIdentifier result {};
  fcgi_si::RecordStatus& record_status {*record_status_ptr};

  // Check if it is a management record.
  if(record_status.get_request_id().FCGI_id() == 0)
  {
    if(record_status.get_type() == fcgi_si::FCGIType::kFCGI_GET_VALUES)
      SendGetValuesResult(connection, record_status);
    else // Unknown type,
      SendFCGIUnknownType(connection, record_status.get_type());
  }
  // Check if the record is valid. Ignore record if it is not.
  else if(record_status.get_invalid_status())
  {}
  else // The record must be a valid application record. Process it.
  {
    fcgi_si::RequestIdentifier request_id {record_status.get_request_id()};

    switch(record_status.get_type())
    {
      case fcgi_si::FCGIType::kFCGI_BEGIN_REQUEST: {
        // Extract role
        uint16_t role {record_status.get_local_content()[
          fcgi_si::kBeginRequestRoleB1Index]};
        role <<= 8;
        role += record_status.get_local_content()[
          fcgi_si::kBeginRequestRoleB0Index];

        // Check for rejection based on role, maximum request count,
        // and application-set overload.
        if(role =! role_)
          SendFCGIEndRequest(connection, request_id,
            fcgi_si::FCGI_UNKNOWN_ROLE, EXIT_FAILURE);
        else
        {
          bool at_maximum_request_limit {false};
          { // Start lock handling block.
            // ACQUIRE interface_state_mutex_.
            std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
            auto request_count_it = request_count_map_.find(connection);
            at_maximum_request_limit = (request_count_it->second
              == maximum_request_count_per_connection_);
          } // RELEASE interface_state_mutex_.
          if(at_maximum_request_limit)
            (maximum_request_count_per_connection_ == 1) ?
              SendFCGIEndRequest(connection, request_id,
                fcgi_si::FCGI_CANT_MPX_CONN, EXIT_FAILURE) :
              SendFCGIEndRequest(connection, request_id,
                fcgi_si::FCGI_OVERLOADED, EXIT_FAILURE);
          else if(application_overload_)
            SendFCGIEndRequest(connection, request_id,
              fcgi_si::FCGI_OVERLOADED, EXIT_FAILURE);
          else // We can accept the request.
          {
            // Extract close_connection value.
            bool close_connection =
              !(record_status.get_local_content()[
                  fcgi_si::kBeginRequestFlagsIndex]
                & fcgi_si::FCGI_KEEP_CONN);
            // ACQUIRE interface_state_mutex_.
            std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
            AddRequest(request_id, role, close_connection, request_count_it);
          } // RELEASE interface_state_mutex_.
        }
        break;
      }
      case fcgi_si::FCGIType::kFCGI_ABORT_REQUEST: {
        // Has the request already been assigned?
        bool send_end_request {false};
        { // Start lock handling block.
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
          auto request_data_it {request_map_.find(request_id)};

          if(request_data_it->second.get_status()
             == fcgi_si::RequestStatus::kRequestAssigned)
            request_data_it->second.set_abort();
          else // Not assigned. We can erase the request and update state.
          {
            // Check if we should indicate that a request was made by the
            // client web sever to close the connection.
            if(request_data_it->second.get_close_connection())
              application_closure_request_set_.insert(connection);
            RemoveRequest(request_data_it);
            send_end_request = true;
          }
        } // RELEASE interface_state_mutex_.
        if(send_end_request)
          SendFCGIEndRequest(connection, request_id,
            fcgi_si::FCGI_REQUEST_COMPLETE, app_status_on_abort_);
          // Don't bother checking if the connection was closed by the
          // peer by inspecting the return value of the call to
          // SendFCGIEndRequest() as it would be difficult to act on
          // this information in the middle of the call to read.
        break;
      }
      case fcgi_si::FCGIType::kFCGI_PARAMS:
      case fcgi_si::FCGIType::kFCGI_STDIN:
      case fcgi_si::FCGIType::kFCGI_DATA: {
        bool send_end_request {false};
        // Should we complete the stream?
        if(record_status.EmptyRecord())
        {
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
          auto request_data_it {request_map_.find(request_id)};

          (record_status.get_type() == fcgi_si::FCGIType::kFCGI_PARAMS) ?
            request_data_it->second.CompletePARAMS() :
          (record_status.get_type() == fcgi_si::FCGIType::kFCGI_STDIN)  ?
            request_data_it->second.CompleteSTDIN() :
            request_data_it->second.CompleteDATA();

          // Check if the request is complete. If it is, validate the
          // FCGI_PARAMS stream. This also puts the RequestData object into a
          // valid state to be used for construction of an FCGIRequest object.
          if(request_data_it->second.IsRequestComplete())
          {
            if(request_data_it->second.ProcessFCGI_PARAMS())
              result = request_id;
            else // The request has a malformed FCGI_PARAMS stream. Reject.
            {
              // Check if we should indicate that a request was made by the
              // client web sever to close the connection.
              if(request_data_it->second.get_close_connection())
                application_closure_request_set_.insert(connection);
              RemoveRequest(request_data_it);
              send_end_request = true;
            }
          }
        } /* RELEASE interface_state_mutex_. */ /*
        else
          The record had content which was appended to the proper
          stream when the content was received. No action need be taken now. */

        if(send_end_request) // (Because of a malformed FCGI_PARAMS stream.)
          SendFCGIEndRequest(connection, request_id, fcgi_si::FCGI_REQUEST_COMPLETE,
            EXIT_FAILURE);
        break;
      }
    }
  }
  return result; // Default (null) RequestIdentifier if not assinged to.
}

void fcgi_si::FCGIServerInterface::
RemoveConnectionFromSharedState(int connection)
{
  write_mutex_map_.erase(connection);
  application_closure_request_set_.erase(connection);
  request_count_map_.erase(connection);

  try
  {
    linux_scw::CloseWithErrorCheck(connection);
  }
  catch(std::runtime_error& e)
  {
    std::string message {e.what()};
    message += "A call to RemoveConnectionFromSharedState encountered an error which\n could not be handled from a call to CloseWithErrorCheck.";
    throw std::runtime_error {ERROR_STRING(message)};
  }
}

bool fcgi_si::FCGIServerInterface::
SendFCGIEndRequest(int connection, RequestIdentifier request_id,
                   uint8_t protocol_status, int32_t app_status)
{
  std::vector<uint8_t> result(16, 0); // Allocate space for two bytes.

  // Encode the record FCGI request ID from the RequestID object.
  uint8_t request_id_byte_array[2];
  for(char i {0}; i < 2; i++)
  {
    request_id_byte_array[i] = request_id.FCGI_id() >> (8 - (8*i));
  }

  // Encode app_status.
  uint8_t app_status_byte_array[4];
  uint32_t unsigned_app_status {static_cast<uint32_t>(app_status)};
  for(char i {0}; i < 4; i++)
  {
    app_status_byte_array[i] = unsigned_app_status >> (24 - (8*i));
  }

  // Set header.
  fcgi_si::PopulateHeader(result.begin(), fcgi_si::FCGIType::kFCGI_END_REQUEST,
    request_id.FCGI_id(), fcgi_si::FCGI_HEADER_LEN, 0);
  // Set body.
  for(char i {0}; i < 4; i++)
  {
    result[(i + 1) + fcgi_si::kHeaderReservedByteIndex] =
      app_status_byte_array[i];
  }
  result[5 + fcgi_si::kHeaderReservedByteIndex]   =
    protocol_status;
  // Remaining bytes were set to zero during string initialization.

  return SendRecord(connection, result);
}

bool fcgi_si::FCGIServerInterface::
SendFCGIUnknownType(int connection, fcgi_si::FCGIType type)
{
  std::vector<uint8_t> result(16, 0); // Allocate space for two bytes.

  // Set header.
  fcgi_si::PopulateHeader(result.begin(), fcgi_si::FCGIType::kFCGI_UNKNOWN_TYPE,
    fcgi_si::FCGI_NULL_REQUEST_ID, fcgi_si::FCGI_HEADER_LEN, 0);
  // Set body. (Only the first byte in the body is used.)
  result[1 + fcgi_si::kHeaderReservedByteIndex]   =
    static_cast<uint8_t>(type);
  // Remaining bytes were set to zero during string initialization.

  return SendRecord(connection, result);
}

bool fcgi_si::FCGIServerInterface::
SendGetValuesResult(int connection, const RecordStatus& record_status)
{
  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
    get_value_pairs {fcgi_si::
      ProcessBinaryNameValuePairs(record_status.get_local_content().size(),
      record_status.get_local_content().data())};

  std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>>
    result_pairs {};

  // Construct result pairs disregarding any name that is not understood.
  // TODO Mark each of the three configuration parameters as included or not
  // and do not include duplicates in the output. This ensures that an over-
  // flow does not occur.
  for(auto iter {get_value_pairs.begin()};
      iter != get_value_pairs.end(); ++iter)
  {
    if(iter->first == fcgi_si::FCGI_MAX_CONNS)
    {
      result_pairs.emplace_back(iter->first,
        fcgi_si::uint32_tToUnsignedCharacterVector(maximum_connection_count_));
    }
    else if(iter->first == fcgi_si::FCGI_MAX_REQS)
    {
      result_pairs.emplace_back(iter->first,
        fcgi_si::uint32_tToUnsignedCharacterVector(maximum_request_count_per_connection_));
    }
    else if(iter->first == fcgi_si::FCGI_MPXS_CONNS)
    {
      std::vector<uint8_t> v {(maximum_request_count_per_connection_ > 1) ?
        static_cast<uint8_t>('1') : static_cast<uint8_t>('0')};
      result_pairs.emplace_back(iter->first, v);
    }
  }

  // Process result pairs to generate the response string.

  // Allocate space for header.
  std::vector<uint8_t> result(fcgi_si::FCGI_HEADER_LEN, 0);

  // Since only known names are accepted, assume that the lengths of
  // the names and values can fit in either 7 or 31 bits.
  for(auto pair_iter = result_pairs.begin(); pair_iter != result_pairs.end();
    ++pair_iter)
  {
    // Mask for encoding four byte lengths.
    uint32_t four_byte_mask(1LU << 31);
    // Encode name length;
    uint32_t item_size(pair_iter->first.size());
    (item_size <= fcgi_si::kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      fcgi_si::EncodeFourByteLength(four_byte_mask | item_size,
        &result);
    // Encode value temp_length
    item_size = pair_iter->second.size();
    (item_size <= fcgi_si::kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      fcgi_si::EncodeFourByteLength(four_byte_mask | item_size,
        &result);
    // Append character bytes of name and value.
    result.insert(result.end(), pair_iter->first.begin(), pair_iter->first.end());
    result.insert(result.end(), pair_iter->second.begin(), pair_iter->second.end());
  }

  // Prepare to write the response.

  // Check that the content length can be encoded in the header.
  uint64_t header_and_content_length(result.size());
  uint64_t content_length {header_and_content_length
    - fcgi_si::FCGI_HEADER_LEN};
  if(content_length > kMaxRecordContentByteLength)
    throw; // TODO what?

  // Pad the record to a multiple of FCGI_HEADER_LEN.
  uint8_t pad_length {static_cast<uint8_t>(fcgi_si::FCGI_HEADER_LEN
    - (header_and_content_length % fcgi_si::FCGI_HEADER_LEN))};
  result.insert(result.end(), pad_length, 0);

  fcgi_si::PopulateHeader(result.begin(), fcgi_si::FCGIType::kFCGI_GET_VALUES_RESULT,
    fcgi_si::FCGI_NULL_REQUEST_ID, content_length, pad_length);

  return SendRecord(connection, result);
}

bool fcgi_si::FCGIServerInterface::
SendRecord(int connection, const std::vector<uint8_t>& result)
{
  // ACQUIRE the write mutex for the connection.
  std::lock_guard<std::mutex> write_lock {*write_mutex_map_[connection]};

  // Send record.
  size_t number_written = socket_functions::NonblockingPollingSocketWrite(connection,
    result.data(), result.size());
  if(number_written < result.size())
    if(errno == EPIPE)
      return false;
    else throw std::runtime_error
      {ERRNO_ERROR_STRING("write from a call to NonblockingPollingSocketWrite")};
  return true;
} // RELEASE the write mutex for the connection.

bool fcgi_si::FCGIServerInterface::
UnassignedRequestCleanup(int connection)
{
  bool active_requests_present {false};
  for(auto request_map_iter =
        request_map_.lower_bound(RequestIdentifier {connection, 0});
      !(request_map_iter == request_map_.end()
        || request_map_iter->first.descriptor() > connection);
      /*no-op*/)
  {
    if(request_map_iter->second.get_status() ==
       fcgi_si::RequestStatus::kRequestAssigned)
    {
      active_requests_present = true;
      ++request_map_iter;
    }
    else
    {
      // Safely erase the request.
      auto request_map_erase_iter {request_map_iter};
      ++request_map_iter;
      RemoveRequest(request_map_erase_iter);
    }
  }
  return active_requests_present;
}
