// Unix system call headers.
extern "C" {
  // General headers.
  #include <unistd.h>
  #include <sys/types.h>    // General. Also included for portability for
                            // <sys/socket.h>.
  #include <fcntl.h>        // For file status flag manipulation.
  #include <syslog.h>       // For central logging.
  // Socket headers.
  #include <sys/socket.h>
  #include <netinet/in.h>   // Defines constants for use with inet_ntop().
  // I/O multiplexing headers.
  #include <sys/time.h>     // For portability for <sys/select.h>.
  #include <sys/select.h>
}

// C standard library headers present in the C++ standard library.
#include <cerrno>
#include <cstdlib>          // For std::getenv() and uint8_t etc.
// C++ standard library headers.
#include <stdexcept>        // For the standard exception classes.
#include <mutex>

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
  // Check that the role is supported.
  if(role_ != FCGI_RESPONDER)
    throw std::runtime_error {
      "An FCGIApplicationInterface object could not be constructed\n"
      "as the provided role is not supported.\n"
      "Provided role: " + std::to_string(role_) + '\n'};

  // Ensure that the listening socket is non_blocking.
  int flags = fcntl(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO, F_GETFL);
  if(flags == -1)
  {
    throw std::runtime_error {
      "An error from a call to fcntl() with F_GETFL could not be handled.\n"
      "Errno had a value of:\n"
      + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
      + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};
  }
  flags |= O_NONBLOCK;
  if(fcntl(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
     F_SETFL, flags) == -1)
  {
    throw std::runtime_error {
      "An error from a call to fcntl() with F_SETFL could not be handled.\n"
      "Errno had a value of:\n"
      + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
      + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};
  }

  // Determine the socket domain. Internet domains may have a list of
  // authorized IP addresses bound to FCGI_WEB_SERVER_ADDRS.
  struct sockaddr_storage passive_socket_address;
  socklen_t address_size {sizeof(struct sockaddr_storage)};
  int getsockname_return {};
  while((getsockname_return = getsockname(FCGI_LISTENSOCK_FILENO,
    (struct sockaddr*)&passive_socket_address,
    &address_size)) == -1 && errno == EINTR){}
  if(getsockname_return == -1)
  {
    throw std::runtime_error {
      "An error from a call to getsockname() could not be handled.\n"
      "Errno had a value of:\n"
      + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
      + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};
  }
  sa_family_t socket_domain {passive_socket_address.ss_family};

  // For internet domains, check for IP addresses which the parent process
  // deemed authorized. If FCGI_WEB_SERVER_ADDRS is unbound or bound to an
  // empty value, any address is accepted. If no valid addresses are found
  // after processing a list, an error is thrown. Otherwise, the list of
  // authorized addresses is stored in the FCGIApplicationInterface object.
  if(!(socket_domain == AF_INET || socket_domain == AF_INET6))
    return;

  if(const char* ip_addresses_ptr = std::getenv("FCGI_WEB_SERVER_ADDRS"))
  {

  }
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
    number_for_select = (map_reverse_iter->first) + 1; // Use highest fd + 1.
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
    throw std::runtime_error {
      "An error from a call to select() could not be handled.\n"
      "Errno had a value of:\n"
      + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
      + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};

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

inline std::map<int, RecordStatus>::size_type
fcgi_synchronous_interface::FCGIApplicationInterface::
connection_count() const
{
  return record_status_map_.size();
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
      NonblockingSocketRead(connection, read_buffer, kBufferSize);

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

      if((errno != EAGAIN) && (errno != EWOULDBLOCK))
      // Unrecoverable error.
      throw std::runtime_error {
        "NonblockingSocketRead() encountered an error from a call to\n"
        "read() which could not be handled. Errno had a value of: \n"
        + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
        + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};
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
  size_t number_written = NonblockingPollingSocketWrite(connection,
    result.data(), result.size());
  if(number_written < result.size())
    if(errno == EPIPE)
      return false;
    else throw std::runtime_error {
      "NonblockingPollingSocketWrite() encountered an error from a call to\n"
      "write() which could not be handled. Errno had a value of: \n"
      + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
      + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};
  return true;
}

bool fcgi_synchronous_interface::FCGIApplicationInterface::
SendGetValueResult(int connection, const RecordStatus& record_status)
{
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
    get_value_pairs
    {ProcessBinaryNameValuePairs(record_status.local_record_content_buffer.size(),
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
      EncodeFourByteLength(four_byte_mask | item_size, &result);
    // Encode value temp_length
    item_size = pair_iter->second.size();
    (item_size <= fcgi_synchronous_interface::kNameValuePairSingleByteLength) ?
      result.push_back(item_size) :
      EncodeFourByteLength(four_byte_mask | item_size, &result);
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

// TODO Refactor with new cases for implicit calls to accept.
ssize_t fcgi_synchronous_interface::FCGIApplicationInterface::
Accept()
{
  sockaddr new_connection_address_struct;
  socklen_t new_connection_address_length;
  int new_connection_descriptor = accept(0, &new_connection_address_struct, &new_connection_address_length);
  if(new_connection_descriptor == -1)
    // TODO Errno check on socket errors.
    return -1;
  if(valid_ip_address_list_.size())
  {
    std::string address_string {} // = TODO Address-to-string conversion function.
    if (valid_ip_address_set_.find(address_string) == valid_ip_address_set_.end())
    {
      // Invalid connection; reject.
      new_connection_descriptor.close();
      // TODO write the address of the invalid connection to a log.
      return -1;
    }
  }
  // Make accepted connections non-blocking.
  int flags = fcntl(new_connection_descriptor, F_GETFL);
  if(flags == -1);
    // TODO: error reporting procedure?
  flags |= O_NONBLOCK;
  if(fcntl(new_connection_descriptor, F_SETFL, flags) == -1);
    // TODO: error reporting procedure? See above.
  return new_connection_descriptor;
}

size_t fcgi_synchronous_interface::FCGIApplicationInterface::
NonblockingPollingSocketWrite(int fd, const uint8_t* buffer_ptr, size_t count)
{
  size_t number_remaining {count};
  ssize_t number_returned {};

  while(number_remaining > 0)
  {
    number_returned = write(fd, static_cast<void*>(buffer_ptr), number_remaining);
    if(number_returned == -1)
    {
      if(errno == EINTR || errno = EAGAIN || errno = EWOULDBLOCK)
        number_returned = 0;
      else break; // Error value that doesn't permit re-calling write().
    }
    number_remaining -= number_returned;
    buffer_ptr += number_returned;
  }
  return count - number_remaining;
}

size_t fcgi_synchronous_interface::FCGIApplicationInterface::
NonblockingSocketRead(int fd, uint8_t* buffer_ptr, size_t count)
{
  size_t number_remaining {count};
  ssize_t number_returned {};

  while(number_remaining > 0)
  {
    number_returned = read(fd, static_cast<void*>(buffer_ptr), number_remaining);
    if(number_returned == 0)
    {
      errno = 0;
      break;
    }
    else if(number_returned == -1)
    {
      if(errno == EINTR) number_returned = 0;
      else break;
    }
    number_remaining -= number_returned;
    buffer_ptr += number_returned;
  }
  return count - number_remaining;
}

uint32_t fcgi_synchronous_interface::FCGIApplicationInterface::
ExtractFourByteLength(const uint8_t* content_ptr) const
{
  uint32_t length {*content_ptr & 0x7f}; // mask out leading 1;
  // Perform three shifts by 8 bits to extract all four bytes.
  for(char i {0}; i < 3; i++)
  {
    length <<= 8;
    content_ptr++;
    length += *content_ptr;
  }
  return length;
}

void EncodeFourByteLength(uint32_t length, std::basic_string<uint8_t>* string_ptr)
{
  for(char i {0}; i < 4; i++)
  {
    string_ptr->push_back(length >> (24 - (8*i)));
  }
}

std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
fcgi_synchronous_interface::FCGIApplicationInterface::
ProcessBinaryNameValuePairs(int content_length, const uint8_t* content_ptr)
{
  int bytes_processed {0};
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
  result {};
  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
  error_result {};

  while(bytes_processed < content_length)
  {
    uint32_t name_length;
    uint32_t value_length;
    bool name_length_bit = *content_ptr >> 7;
    bool value_length_bit;

    // Extract name length.
    if(name_length_bit)
    {
      if((bytes_processed + 3) > content_length)
        return error_result; // Not enough information to continue.
      name_length = ExtractFourByteLength(content_ptr);
      bytes_processed += 4;
      content_ptr += 4;
    }
    else
    {
      name_length = *content_ptr;
      bytes_processed += 1;
      content_ptr += 1;
    }

    // Extract value length.
    if((bytes_processed + 1) > content_length)
      return error_result;
    value_length_bit = *content_ptr >> 7;
    if(value_length_bit)
    {
      if((bytes_processed + 3) > content_length)
        return error_result; // Not enough information to continue.
      value_length = ExtractFourByteLength(content_ptr);
      bytes_processed += 4;
      content_ptr += 4;
    }
    else
    {
      value_length = *content_ptr;
      bytes_processed += 1;
      content_ptr += 1;
    }

    // Extract name and value as byte strings.
    if((bytes_processed + name_length + value_length) > content_length)
      return error_result; // Not enough information to continue.
    std::basic_string<uint8_t> name {content_ptr, name_length};
    content_ptr += name_length;
    std::basic_string<uint8_t> value {content_ptr, value_length};
    content_ptr += value_length;
    bytes_processed += (name_length + value_length);
    result.emplace_back(std::move(name), std::move(value));
  } // End while (no more pairs to process).

  return result;
}

void RemoveConnectionFromSharedState(int connection)
{
  write_mutex_map_.erase(connection);
  application_closure_request_set_.erase(connection);
  request_count_map_.erase(connection);

  // Error checking on close() is currently dependent on how
  // Linux implements close(). For most errors, Linux guarantees
  // that the descriptor is closed.
  if(close(connection) == -1)
  {
    if(errno != EINTR)
      throw std::runtime_error {
        "RemoveConnectionFromSharedState() encountered an error from a call to\n"
        "close() which could not be handled. Errno had a value of: \n"
        + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
        + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n'};
  }
}

void
fcgi_synchronous_interface::FCGIApplicationInterface::
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
