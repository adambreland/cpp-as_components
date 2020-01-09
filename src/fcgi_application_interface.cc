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
#include <cstdlib>          // For std::getenv() and std::size_t.
// C++ standard library headers.
#include <stdexcept>        // For the standard exception classes.
#include <mutex>
#include <regex>
#include <utility>
#include <memory>

#include "external/error_handling/include/error_handling.h"
#include "external/linux_scw/include/linux_scw.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/data_types.h"
#include "include/fcgi_application_interface.h"
#include "include/fcgi_request.h"

// Public member functions

fcgi_synchronous_interface::FCGIApplicationInterface::
FCGIApplicationInterface(uint32_t max_connections, uint32_t max_requests,
  uint16_t role);
: maximum_connection_count_ {max_connections},
  maximum_request_count_per_connection_ {max_requests},
  role_ {role}
{
  // Check that the arguments are within the domain.
  std::string error_message {};
  bool construction_argument_error {false};
  // Check that the role is supported.
  if(role_ != FCGI_RESPONDER)
  {
    construction_argument_error = true;
    error_message += "An FCGIApplicationInterface object could not be constructed\n";
    error_message += "as the provided role is not supported.\n";
    error_message += "Provided role: "};
    error_message += std::to_string(role_);
  }
  if(max_connections_ == 0)
  {
    if(construction_argument_error)
      error_message += '\n';
    else
      construction_argument_error = true;
    error_message += "A value of zero was given for the maximum number of transport connections.\n";
    error_message += "This value must be greater than or equal to one.";
  }
  if(max_requests_ == 0)
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
  int flags = fcntl(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO, F_GETFL);
  if(flags == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_GETFL")};
  flags |= O_NONBLOCK;
  if(fcntl(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
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

  while((getsockopt_return = getsockopt(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_DOMAIN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 && errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_DOMAIN")};
  sa_family_t socket_domain {getsockopt_int_buffer};
  socket_domain_ = socket_domain;

  getsockopt_int_buffer_size = sizeof(int);
  while((getsockopt_return = getsockopt(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_TYPE, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 & errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_TYPE")};
  if(getsockopt_int_buffer != SOCK_STREAM)
    throw std::runtime_error {ERROR_STRING("The socket used for construction of an FCGIApplicationInterface object\nwas not a stream socket.")};

  getsockopt_int_buffer_size = sizeof(int);
  while((getsockopt_return = getsockopt(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
    SOL_SOCKET, SO_ACCEPTCONN, &getsockopt_int_buffer, &getsockopt_int_buffer_size))
    == -1 & errno == EINTR){}
  if(getsockopt_return == -1)
    throw std::runtime_error {ERRNO_ERROR_STRING("getsockopt with SO_ACCEPTCONN")};
  if(getsockopt_int_buffer != 1) // The value 1 is used to indicate listening status.
    throw std::runtime_error {ERROR_STRING("The socket used for construction of an FCGIApplicationInterface object\nwas not a listening socket.")};

  // For internet domains, check for IP addresses which the parent process
  // deemed authorized. If FCGI_WEB_SERVER_ADDRS is unbound or bound to an
  // empty value, any address is authorized. If no valid addresses are found
  // after processing a list, an error is thrown. Otherwise, a list of
  // well-formed addresses which have been converted to a normalized
  // presentation format is stored in the FCGIApplicationInterface object.
  if(socket_domain == AF_INET || socket_domain == AF_INET6)
  {
    const char* ip_address_list_ptr = std::getenv("FCGI_WEB_SERVER_ADDRS")
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

inline bool fcgi_synchronous_interface::FCGIApplicationInterface::
get_overload() const
{
  return application_overload_;
}

inline bool fcgi_synchronous_interface::FCGIApplicationInterface::
set_overload()
{
  application_overload_ = true;
}

inline int fcgi_synchronous_interface::FCGIApplicationInterface::
connection_count() const
{
  return record_status_map_.size();
}

std::vector<fcgi_synchronous_interface::FCGIRequest>
fcgi_synchronous_interface::FCGIApplicationInterface::
AcceptRequests()
{
  // Result to return.
  std::vector<fcgi_synchronous_interface::FCGIRequest> requests {};

  // Construct read descriptor set to wait on.
  fd_set read_set;
  FD_ZERO(&read_set);
  // Add listening socket to read_set for new connections.
  FD_SET(0, &read_set);
  // 0 + 1 = 1 Set for proper select call if no connected sockets are present.
  int number_for_select {1};
  if((auto map_reverse_iter {record_status_map_.rbegin()}) !=
     record_status_map_.rend()) // Reverse to access highest fd immediately.
  {
    number_for_select = (map_reverse_iter->first) + 1; // Use (highest fd) + 1.
    for(; map_reverse_iter != record_status_map_.rend(); ++map_reverse_iter)
      FD_SET(map_reverse_iter->first, &read_set);
  }

  // Wait for data to be read or new connections and process them.
  // select() error check loop.
  int select_return {};
  while((select_return =
    select(number_for_select, &read_set, nullptr, nullptr, nullptr))
    == -1 && (errno == EINTR || errno == EAGAIN)){}
  if(select_return == -1) // Unrecoverable error from select().
    throw std::runtime_error {ERRNO_ERROR_STRING("select")};

  // Start reading data from ready connections.
  // Check connected sockets (as held in record_status_map_) before the
  // listening socket.
  int connections_read {0};
  for(auto it = record_status_map_.begin();
      (it != record_status_map_.end()) && (connections_read < select_return);
      ++it)
  {
    // Extract file descriptor and check if it is ready.
    if((int fd = it->first, FD_ISSET(fd, &read_set)))
    {
      connections_read++;
      std::vector<RequestIdentifier> request_identifiers = Read(fd);
      // TODO Process requests
    }
  }
  // Accept new connections if some are present.
  if(connections_read < select_return)
  {
    // Accept();
  }
}



// Helper functions



std::vector<RequestIdentifier>
fcgi_synchronous_interface::FCGIApplicationInterface::
Read(int connection)
{
  // Number of bytes read at a time from connected sockets.
  constexpr int kBufferSize {512};
  uint8_t read_buffer[kBufferSize];

  // Return value to be modified during processing.
  std::vector<RequestIdentifier> request_identifiers {};

  // Start reading from the connection until it blocks (no more data),
  // it is found to be disconnected, or an unrecoverable error occurs.
  while(true)
  {
    // Read from socket.
    int number_bytes_processed = 0;
    int number_bytes_received =
      socket_functions::NonblockingSocketRead(connection, read_buffer, kBufferSize);

    // Check for a disconnected socket or an unrecoverable error.
    if(number_bytes_received < kBufferSize)
    {
      if(errno == 0)
      {
        // Connection was closed. Discard any read data and update interface
        // state.
        ClosedConnectionFoundDuringAcceptRequests(connection);
        return {};
      }

      if((errno != EAGAIN) && (errno != EWOULDBLOCK)) // Unrecoverable error.
        throw std::runtime_error
          {ERRNO_ERROR_STRING("read from a call to NonblockingSocketRead")};
    }

    // Process bytes received (if any). This check is needed as blocking
    // errors may return zero bytes if nothing was read.
    if(number_bytes_received > 0)
    {
      RecordStatus& record_status = record_status_map_[connection];
      while(number_bytes_processed < number_bytes_received)
      {
        int number_bytes_remaining = number_bytes_received
          - number_bytes_processed;
        // Is the header complete?
        if(!(record_status.IsHeaderComplete()))
        {
          if((int remaining_header {fcgi_synchronous_interface::FCGI_HEADER_LEN
            - record_status.bytes_received}) <= number_bytes_remaining)
          {
            for(char i {record_status.bytes_received};
                i < fcgi_synchronous_interface::FCGI_HEADER_LEN; i++)
            {
              header[i] = read_buffer[number_bytes_processed];
              number_bytes_processed++;
            }
            record_status.bytes_received += remaining_header;

            // Update record_status as header has been completed.
            // Part of this update is conditionally setting the rejected flag.
            record_status.UpdateAfterHeaderCompletion(connection);
          }
          else // Need more than we have to complete header.
          {
            for(char i {record_status.bytes_received};
              number_bytes_remaining > 0; (i++, number_bytes_remaining--))
            {
              header[i] = read_buffer[number_bytes_processed];
              number_bytes_processed++;
            }
            record_status.bytes_received += remaining_header;
          }
        }
        // Header is complete, but the record is not.
        // Either the content is complete or it isn't.
        else
        {
          int content_byte_difference {fcgi_synchronous_interface::FCGI_HEADER_LEN
            + content_bytes_expected - record_status.bytes_received};
          int remaining_content {(content_byte_difference > 0) ?
            content_byte_difference : 0};
          int number_to_write {};

          if(remaining_content > 0) // Content incomplete.
          {
            number_to_write = (remaining_content <= number_bytes_remaining) ?
              remaining_content : number_bytes_remaining;
            // Follow discipline for local while and usage of RecordStatus objects.
            record_status.bytes_received += number_to_write;
            number_bytes_processed += number_to_write;
            // Determine what we should do with the bytes based on rejection
            // and type. Every record is rejected if it is not one of the
            // six types below. Accordingly, we only need to check for those
            // types.
            if(!record_status.invalid_record)
            {
              if(record_status.request_id.second == 0
                 || record_status.type ==
                    fcgi_synchronous_interface::FCGIType::kFCGI_BEGIN_REQUEST
                 || record_status.type ==
                    fcgi_synchronous_interface::FCGIType::kFCGI_ABORT_REQUEST)
              // Append to local buffer.
              {
                record_status.local_record_content_buffer.
                  append(&read_buffer[number_bytes_processed], number_to_write);
              }
              else // Append to non-local buffer.
              {
                // Acquire interface_state_mutex_ to locate append location.
                // The key request_id must be present as the record is valid
                // and it is not a begin request record.
                std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
                auto request_map_iter {request_map_.find(request_id)};
                switch(record_status.type) {
                  case fcgi_synchronous_interface::FCGIType::kFCGI_PARAMS : {
                    request_map_iter->second.AppendToPARAMS(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case fcgi_synchronous_interface::FCGIType::kFCGI_STDIN : {
                    request_map_iter->second.AppendToSTDIN(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                  case fcgi_synchronous_interface::FCGIType::kFCGI_DATA : {
                    request_map_iter->second.AppendToDATA(
                      &read_buffer[number_bytes_processed], number_to_write);
                    break;
                  }
                }
              } // interface_state_mutex_ released here.
            }
          }
          else // Padding incomplete.
          {
            int remaining_padding {content_byte_difference
              + record_status.padding_bytes_expected};
            number_to_write = (remaining_padding <= number_bytes_remaining) ?
              remaining_padding : number_bytes_remaining;
            record_status.bytes_received += number_to_write;
            number_bytes_processed += number_to_write;
          }
        }
        // Potentially completed a record.
        if(record_status.IsRecordComplete())
        {
          fcgi_synchronous_interface::RequestIdentifier request_id
            {ProcessCompleteRecord(connection, &record_status)};
          if(request_id) request_identifiers.push_back(request_id);
        }
        // Loop to check if more received bytes need to be processed.
      } // On exit, looped through all received data as partitioned by record
        // segments.
    }
    // Check if an additional read should be made on the socket. A short count
    // can only mean that a call to read() blocked as EOF and other errors
    // were handled above.
    if(bytes_received < kBufferSize)
    break;
  } // On exit, end while loop which keeps reading from the socket.

  return request_identifiers;
}

bool SendRecord(int connection, const std::basic_string<uint8_t>& result)
{
  // Obtain the write mutex for the connection.
  std::lock_guard<std::mutex> write_lock {write_mutex_map_[connection]};

  // Send record.
  size_t number_written = socket_functions::NonblockingPollingSocketWrite(connection,
    result.data(), result.size());
  if(number_written < result.size())
    if(errno == EPIPE)
      return false;
    else throw std::runtime_error
      {ERRNO_ERROR_STRING("write from a call to NonblockingPollingSocketWrite")};
  return true;
}

bool fcgi_synchronous_interface::FCGIApplicationInterface::
SendGetValueResult(int connection, const RecordStatus& record_status)
{
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
    get_value_pairs {fcgi_synchronous_interface::
      ProcessBinaryNameValuePairs(record_status.local_record_content_buffer.size(),
      record_status.local_record_content_buffer.data())};

  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
    result_pairs {};

  // Construct result pairs disregarding any name that is not understood.
  for(auto iter {get_value_pairs.begin()};
      iter != get_value_pairs.end(); ++iter)
  {
    if(iter->first == fcgi_synchronous_interface::FCGI_MAX_CONNS)
    {
      result_pairs.emplace_back(iter->first,
        std::to_string(maximum_connection_count_));
    }
    else if(iter->first == fcgi_synchronous_interface::FCGI_MAX_REQS)
    {
      result_pairs.emplace_back(iter->first,
        std::to_string(maximum_request_count_per_connection_));
    }
    else if(iter->first == fcgi_synchronous_interface::FCGI_MPXS_CONNS)
    {
      result_pairs.emplace_back(iter->first,
        (maximum_request_count_per_connection_ > 1) ? "1" : "0");
    }
  }

  // Process result pairs to generate the response string.

  // Allocate space for header.
  std::basic_string<uint8_t> result(fcgi_synchronous_interface::FCGI_HEADER_LEN, 0);

  // Since only known names are accepted, assume that the lengths of
  // the names and values can fit in either 7 or 31 bits.
  for(auto pair_iter {result_pairs.begin()}; iter != result_pairs.end(); ++iter)
  {
    // Mask for encoding four byte lengths.
    uint32_t four_byte_mask {1 << 31};
    // Encode name length;
    uint32_t item_size(pair_iter->first.size());
    (item_size <= fcgi_synchronous_interface::kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      fcgi_synchronous_interface::EncodeFourByteLength(four_byte_mask | item_size,
        &result);
    // Encode value temp_length
    item_size = pair_iter->second.size();
    (item_size <= fcgi_synchronous_interface::kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      fcgi_synchronous_interface::EncodeFourByteLength(four_byte_mask | item_size,
        &result);
    // Append character bytes of name and value.
    result.append(pair_iter->first);
    result.append(pair_iter->second);
  }

  // Prepare to write the response.

  // Check that the content length can be encoded in the header.
  uint64_t header_and_content_length(result.size());
  uint64_t content_length {header_and_content_length
    - fcgi_synchronous_interface::FCGI_HEADER_LEN};
  if(content_length > kMaxRecordContentByteLength)
    throw; // TODO what?

  // Pad the record to a multiple of FCGI_HEADER_LEN.
  uint8_t pad_length {fcgi_synchronous_interface::FCGI_HEADER_LEN
    - (header_and_content_length % fcgi_synchronous_interface::FCGI_HEADER_LEN)};
  result.append(pad_length, 0);

  // Update header:
  result[fcgi_synchronous_interface::kHeaderVersionIndex]            =
    fcgi_synchronous_interface::FCGI_VERSION_1;
  result[fcgi_synchronous_interface::kHeaderTypeIndex]               =
    fcgi_synchronous_interface::FCGIType::kFCGI_GET_VALUES_RESULT;
  result[fcgi_synchronous_interface::kHeaderRequestIDB1Index]        =
    fcgi_synchronous_interface::FCGI_NULL_REQUEST_ID;
  result[fcgi_synchronous_interface::kHeaderRequestIDB0Index]        =
    fcgi_synchronous_interface::FCGI_NULL_REQUEST_ID;
  result[fcgi_synchronous_interface::kHeaderContentLengthB1Index]    =
    content_length >> 8;
  result[fcgi_synchronous_interface::kHeaderContentLengthB0Index]    =
    content_length;
  result[fcgi_synchronous_interface::kHeaderPaddingLengthIndex]      =
    pad_length;
  result[fcgi_synchronous_interface::kHeaderReservedByteIndex]       =
    0;

  return SendRecord(connection, result);
}

bool fcgi_synchronous_interface::FCGIApplicationInterface::
SendFCGIUnknownType(int connection, fcgi_synchronous_interface::FCGIType type)
{
  std::basic_string<uint8_t> result(16, 0) // Allocate space for two bytes.

  // Set header.
  result[fcgi_synchronous_interface::kHeaderVersionIndex]            =
    fcgi_synchronous_interface::FCGI_VERSION_1;
  result[fcgi_synchronous_interface::kHeaderTypeIndex]               =
    fcgi_synchronous_interface::FCGIType::kFCGI_UNKNOWN_TYPE;
  result[fcgi_synchronous_interface::kHeaderRequestIDB1Index]        =
    fcgi_synchronous_interface::FCGI_NULL_REQUEST_ID;
  result[fcgi_synchronous_interface::kHeaderRequestIDB0Index]        =
    fcgi_synchronous_interface::FCGI_NULL_REQUEST_ID;
  result[fcgi_synchronous_interface::kHeaderContentLengthB1Index]    =
    0;
  result[fcgi_synchronous_interface::kHeaderContentLengthB0Index]    =
    8; // One byte.
  result[fcgi_synchronous_interface::kHeaderPaddingLengthIndex]      =
    0; // No padding needed.
  result[fcgi_synchronous_interface::kHeaderReservedByteIndex]       =
    0;
  // Set body. (Only the first byte in the body is used.)
  result[1 + fcgi_synchronous_interface::kHeaderReservedByteIndex]   =
    type;
  // Remaining bytes were set to zero during string initialization.

  // Obtain the write mutex for the connection.
  std::lock_guard<std::mutex> write_lock {write_mutex_map_[connection]};

  return SendRecord(connection, result);
}

bool fcgi_synchronous_interface::FCGIApplicationInterface::
SendFCGIEndRequest(int connection, RequestIdentifier request_id,
                   uint8_t protocol_status, int32_t app_status)
{
  std::basic_string<uint8_t> result(16, 0) // Allocate space for two bytes.

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
  result[fcgi_synchronous_interface::kHeaderVersionIndex]            =
    fcgi_synchronous_interface::FCGI_VERSION_1;
  result[fcgi_synchronous_interface::kHeaderTypeIndex]               =
    fcgi_synchronous_interface::FCGIType::kFCGI_END_REQUEST;
  result[fcgi_synchronous_interface::kHeaderRequestIDB1Index]        =
    request_id_byte_array[0];
  result[fcgi_synchronous_interface::kHeaderRequestIDB0Index]        =
    request_id_byte_array[1];
  result[fcgi_synchronous_interface::kHeaderContentLengthB1Index]    =
    0;
  result[fcgi_synchronous_interface::kHeaderContentLengthB0Index]    =
    8; // One byte.
  result[fcgi_synchronous_interface::kHeaderPaddingLengthIndex]      =
    0; // No padding needed.
  result[fcgi_synchronous_interface::kHeaderReservedByteIndex]       =
    0;
  // Set body.
  for(char i {0}; i < 4; i++)
  {
    result[(i + 1) + fcgi_synchronous_interface::kHeaderReservedByteIndex] =
      app_status_byte_array[i]
  }
  result[5 + fcgi_synchronous_interface::kHeaderReservedByteIndex]   =
    protocol_status;
  // Remaining bytes were set to zero during string initialization.

  // Obtain the write mutex for the connection.
  std::lock_guard<std::mutex> write_lock {write_mutex_map_[connection]};

  return SendRecord(connection, result);
}

RequestIdentifier fcgi_synchronous_interface::FCGIApplicationInterface::
ProcessCompleteRecord(int connection, RecordStatus* record_status_ptr)
{
  RequestIdentifier result {};
  RecordStatus& record_status {*record_status_ptr};

  // Check if it is a management record.
  if(record_status.request_id.FCGI_id() == 0)
  {
    if(record_status.type == fcgi_synchronous_interface::FCGIType::kFCGI_GET_VALUES)
      SendGetValueResult(connection, &record_status);
    else // Unknown type,
      SendFCGIUnknownType(connection, record_status.type);
    return result;
  }
  else if(record_status.invalid_record)
  {} // Check if the record is valid. Ignore record if it is not.
  else
  {
    fcgi_synchronous_interface::RequestIdentifier request_id
      {connection, FCGI_request_id};

    // Obtain interface_state_mutex_ to access state.
    std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
    auto request_data_it {request_map_.find(request_id)};
    switch(record_status.type)
    {
      case fcgi_synchronous_interface::FCGIType::kFCGI_BEGIN_REQUEST: {
        // Extract role
        uint6_t role {record_status.local_record_content_buffer[
          fcgi_synchronous_interface::kBeginRequestRoleB1Index]};
        role <<= 8;
        role += local_record_content_buffer[
          fcgi_synchronous_interface::kBeginRequestRoleB0Index];

        // Check for rejection based on role, maximum request count,
        // and application-set overload.
        if(role =! role_)
          SendFCGIEndRequest(connection,
            fcgi_synchronous_interface::FCGI_UNKNOWN_ROLE), -1);
        else if((auto request_count_it {request_count_map_.find(connection)},
                 request_count_it.second == maximum_request_count_per_connection_))
          (maximum_request_count_per_connection_ == 1) ?
            SendFCGIEndRequest(connection, request_id,
              fcgi_synchronous_interface::FCGI_CANT_MPX_CONN, -1) :
            SendFCGIEndRequest(connection, request_id,
              fcgi_synchronous_interface::FCGI_OVERLOADED, -1);
        else if(application_overload_)
          SendFCGIEndRequest(connection, request_id,
            fcgi_synchronous_interface::FCGI_OVERLOADED, -1);
        else // We can accept the request.
        {
          // Extract close_connection value.
          bool close_connection =
            !(record_status.local_record_content_buffer[
                fcgi_synchronous_interface::kBeginRequestFlagsIndex]
              & fcgi_synchronous_interface::FCGI_KEEP_CONN);

          request_map_[request_id] = fcgi_synchronous_interface::
            RequestData(role, close_connection);
          request_count_it->second++;
        }
        break;
      }
      case fcgi_synchronous_interface::FCGIType::kFCGI_ABORT_REQUEST: {
        // Has the request already been assigned?
        if(request_data_it->second.get_status()
          == fcgi_synchronous_interface::RequestStatus::kRequestAssigned)
        {
          request_data_it->second.set_abort();
        }
        else // Not assigned. We can erase the request and update state.
        {
          // Check if we should indicate that a request was made by the
          // client web sever to close the connection.
          if(request_data_it->second.get_close_connection())
            application_closure_request_set_.insert(connection);

          request_map_.erase(request_data_it);
          request_count_map_[connection]--;
          SendFCGIEndRequest(connection, request_id,
            fcgi_synchronous_interface::FCGI_REQUEST_COMPLETE, -1);
          // Don't bother checking if the connection was closed by the
          // peer by inspecting the return value of the call to
          // SendFCGIEndRequest() as it would be difficult to act on
          // this information in the middle of the call to read.
        }
        break;
      }
      case fcgi_synchronous_interface::FCGIType::kFCGI_PARAMS:
      case fcgi_synchronous_interface::FCGIType::kFCGI_STDIN:
      case fcgi_synchronous_interface::FCGIType::kFCGI_DATA: {
        // Should we complete the stream?
        if(record_status.content_bytes_expected == 0)
        {
          (record_status.type == fcgi_synchronous_interface::FCGIType::kFCGI_PARAMS) ?
            request_data_it->second.CompletePARAMS() :
          (record_status.type == fcgi_synchronous_interface::FCGIType::kFCGI_STDIN)  ?
            request_data_it->second.CompleteSTDIN() :
            request_data_it->second.CompleteDATA();

          if(request_data_it->second.IsRequestComplete())
            result = request_id;
        }
        break;
      }
    }
  }
  // Clear the record status for the next record.
  record_status = RecordStatus();
  return result; // Default (null) RequestIdentifier if not assinged to.
} // Release interface_state_mutex_.

ssize_t fcgi_synchronous_interface::FCGIApplicationInterface::
AcceptConnection()
{
  struct sockaddr_storage new_connection_address;
  socklen_t new_connection_address_length = sizeof(struct sockaddr_storage);
  int accept_return {};
  while((accept_return = accept(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
    &new_connection_address, &new_connection_address_length)) == -1
    && errno == EINTR){}
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
      &(((struct sockaddr_in*)&new_connection_address)->in_addr)
      : &(((struct sockaddr_in6*)&new_connection_address)->in6_addr)}
    if(!inet_ntop(new_socket_domain, addr_ptr, address_array, INET6_ADDRSTRLEN))
      throw std::runtime_error {ERRNO_ERROR_STRING("inet_ntop")};
    new_address = address_array;
  }

  // Validate the new connected socket against the gathered information.
  if(new_socket_domain != socket_domain_ || new_socket_type != SOCK_STREAM
     || (valid_ip_address_set_.size() > 0
         && (valid.find(new_address) == valid_ip_address_set_.end())))
  {
    try {linux_scw::CloseWithErrorCheck(new_socket_descriptor)}
    catch(std::runtime_error& e)
    {
      std::string error_message {e.what()};
      error_message += ERROR_STRING("A call to CloseWithErrorCheck threw an error which could not be handled.")
      throw std::runtime_error {error_message};
    }
    return 0;
  }

  // Make the accepted connected sockets non-blocking.
  int flags {};
  while((flags = fcntl(new_connection_descriptor, F_GETFL)) == -1
    && errno == EINTR){}
  if(flags == -1);
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_GETFL")};

  flags |= O_NONBLOCK;

  while((flags = fcntl(new_connection_descriptor, F_SETFL)) == -1
    && errno == EINTR){}
  if(flags == -1);
    throw std::runtime_error {ERRNO_ERROR_STRING("fcntl with F_SETFL")};

  // Update interface state to reflect the new connection.
  record_status_map_[new_socket_descriptor] = RecordStatus {};
  std::unique_pointer<std::mutex> new_mutex_manager {new std::mutex};
  // Acquire interface_state_mutex_ to access shared state.
  std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
  write_mutex_map_[new_socket_descriptor] = std::move(new_mutex_manager);
  request_count_map_[new_socket_descriptor] = 0;

  return new_socket_descriptor;
} // Release interface_state_mutex_.

void fcgi_synchronous_interface::FCGIApplicationInterface::
RemoveConnectionFromSharedState(int connection)
{
  write_mutex_map_.erase(connection);
  application_closure_request_set_.erase(connection);
  request_count_map_.erase(connection);

  try
  {
    linux_system_call_wrappers::CloseWithErrorCheck(connection);
  }
  catch(std::runtime_error& e)
  {
    std::string message {e.what()}
    message += "A call to RemoveConnectionFromSharedState encountered an error which\n could not be handled from a call to CloseWithErrorCheck."
    throw std::runtime_error {ERROR_STRING(message)};
  }
}

void fcgi_synchronous_interface::FCGIApplicationInterface::
ClosedConnectionFoundDuringAcceptRequests(int connection)
{
  // Remove the connection from the record_status_map_ so that it is not
  // included in a call to select().
  record_status_map_.erase(connection);

  // Iterate over requests: delete unassigned requests and check for
  // assigned requests.
  bool active_requests_present {false};
  // Acquire interface_state_mutex_ to access and modify shared state.
  std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
  for(auto request_map_iter =
        request_map_.lower_bound(RequestIdentifier {connection, 0});
      !(request_map_iter == request_map_.end()
        || request_map_iter->first.descriptor() > connection);
      ++request_map_iter)
  {
    if(request_map_iter->second.get_status() ==
       fcgi_synchronous_interface::RequestStatus::kRequestAssigned)
    {
      active_requests_present = true;
    }
    else
    {
      request_map_.erase(request_map_iter);
      request_count_map_[connection]--;
    }
  }
  // Check if the connection can be closed or assigned requests require
  // closure to be delayed.
  (!active_requests_present) ? RemoveConnectionFromSharedState(connection) :
                               connections_found_closed_set_.insert(connection);
}
