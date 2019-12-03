extern "C" {
  #include <sys/types.h>    // General
  #include <unistd.h>       // For file status flag manipulation,
                            // socket writing.
  #include <fcntl.h>        // For file status flag manipulation.
  #include <sys/socket.h>   // To accept requests.
  #include <sys/time.h>     // For select().
  #include <sys/select.h>
  #include <syslog.h>       // For central logging.
  #include <errno.h>        // To check system call error values.
}

#include <cstdlib>          // For std::getenv().
#include <stdexcept>
#include <mutex>            // For std::lock_guard

fcgi_synchronous_interface::FCGIApplicationInterface::
FCGIApplicationInterface(int max_connections, int max_requests);
: maximum_connection_count_ {max_connections},
  maximum_request_count_per_connection_ {max_requests}
{
  // Ensure that the listening socket is non_blocking.
  int flags = fcntl(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO, F_GETFL);
  if(flags == -1)
  {
    throw; // TODO
  }
  flags |= O_NONBLOCK;
  if(fcntl(fcgi_synchronous_interface::FCGI_LISTENSOCK_FILENO,
     F_SETFL, flags) == -1)
  {
    throw; // TODO
  }
  // Access environment variables and check for valid IP addresses.
  if(const char* ip_addresses_ptr = std::getenv("FCGI_WEB_SERVER_ADDRS"))
    // TODO: Create or find a function to call to parse textual,
    // comma-delimited IP addresses.

  // TODO add error checking against values for max_connections
  // and max_requests
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
    throw std::runtime_error(
       "Call to select() failed with errno value: \n"
     + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
     + __FILE__ + "\n"
     + "Line: " + std::to_string(__LINE__) + '\n');

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
      throw std::runtime_error(
         "A call to read() in NonblockingSocketRead() failed with errno value: \n"
       + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
       + __FILE__ + "\n"
       + "Line: " + std::to_string(__LINE__) + '\n');
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
    else throw std::runtime_error(
        "NonblockingPollingSocketWrite() encountered an error from a call to\n"
        "write() which could not be handled. Errno had a value of: \n"
      + std::to_string(errno) + '\n' + std::strerror(errno) + '\n'
      + __FILE__ + "\n" + "Line: " + std::to_string(__LINE__) + '\n');
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
  // Extract type
  FCGIType type
    {record_status.header[fcgi_synchronous_interface::kHeaderTypeIndex]};
  // Extract FCGI request ID
  uint16_t FCGI_request_id
    {record_status.header[fcgi_synchronous_interface::kHeaderRequestIDB1Index]};
  FCGI_request_id <<= 8; // shift by one byte
  FCGI_request_id +=
    record_status.header[fcgi_synchronous_interface::kHeaderRequestIDB0Index];
  // Check if it is a management record.
  if(FCGI_request_id == 0)
  {
    if(type == fcgi_synchronous_interface::FCGIType::kFCGI_GET_VALUES)
      SendGetValueResult(connection, &record_status);
    else // Unknown type,
      SendFCGIUnknownType(connection, type);
    return result;
  }

  // Else, FCGI_request_id > 0 => application request record.
  // Check for the allowed application record types and act accordingly.
  // The allowed types are:
  // 1) kFCGI_BEGIN_REQUEST
  // 2) kFCGI_ABORT_REQUEST
  // 3) kFCGI_PARAMS
  // 4) kFCGI_DATA
  // 5) kFCGI_STDIN
  fcgi_synchronous_interface::RequestIdentifier request_id
    {connection, FCGI_request_id};
  // Obtain interface_state_mutex_ to access state.
  std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
  auto request_data_it {request_map_.find(request_id)};
  switch(type)
  {
    case FCGIType::kFCGI_BEGIN_REQUEST: {
      if(request_data_it == request_map_.end())
      {
        // Extract role
        uint6_t role {record_status.local_record_content_buffer[
          fcgi_synchronous_interface::kBeginRequestRoleB1Index]};
        role <<= 8;
        role += local_record_content_buffer[
          fcgi_synchronous_interface::kBeginRequestRoleB0Index];

        // Check for rejection based on role, maximum request count,
        // and application-set overload.
        if(role =! role_)
          SendFCGIEndRequest(connection, fcgi_synchronous_interface::FCGI_UNKNOWN_ROLE), -1);
        else if((auto request_count_it {request_count_map_.find(connection)},
                 request_count_it.second == maximum_connection_count_))
          (maximum_connection_count_ == 1) ?
            SendFCGIEndRequest(connection, fcgi_synchronous_interface::FCGI_CANT_MPX_CONN, -1) :
            SendFCGIEndRequest(connection, fcgi_synchronous_interface::FCGI_OVERLOADED, -1);
        else if(application_overload_)
          SendFCGIEndRequest(connection, fcgi_synchronous_interface::FCGI_OVERLOADED, -1);
        else // We can accept the request.
        {
          // Extract close_connection value.
          bool close_connection =
            !(record_status.local_record_content_buffer[
                fcgi_synchronous_interface::kBeginRequestFlagsIndex]
              & fcgi_synchronous_interface::FCGI_KEEP_CONN);

          request_map_[request_id] = fcgi_synchronous_interface::
            RequestData(role, close_connection);
          request_count_it->second += 1;
        }
      }
      break;
    }
    case FCGIType::kFCGI_ABORT_REQUEST: {
      // Does the abort apply to a request?
      if((auto request_data_it = request_map_.find(request_id))
        != request_map_.end())
      {
        if(!(request_data_it->second.get_abort()))
        // The request has not already been aborted.
        {
          // Has the request already been assigned?
          if(request_data_it->second.get_status()
            == fcgi_synchronous_interface::RequestStatus::kRequestAssigned)
          {
            request_data_it->second.set_abort();
          }
          else // It hasn't been assigned. We can erase the request.
          {
            // TODO figure out the best way to have the interface send a
            // response.
            request_map_.erase(request_data_it);
          }
        }
      }
      break;
    }
    case FCGIType::kFCGI_PARAMS: {

    }
    case FCGIType::kFCGI_STDIN: {
      // Does the record apply to a request?
      if((auto request_data_it = request_map_.find(request_id)) != request_map_.end())
      {
        if(!(request_data_it->second.FCGI_STDIN_complete)) // Not complete, process data.
        {
          // Extract content length
          uint16_t content_length_1 = record_status.content_buffer_[4];
          uint16_t content_length_0 = record_status.content_buffer_[5];
          int content_length = (content_length_1 << 8) + content_length_0;
          if(content_length)
          {
            // Append data to RequestData stdin buffer
            // TODO
          }
          else // content_length == 0 (and stdin is now complete)
          {
            request_data_it->second.FCGI_STDIN_complete = true;
            if(request_data_it->second.FCGI_DATA_complete && equest_data_it->second.FCGI_PARAMS_complete)
            {
              request_data_it->second.request_status = fcgi_synchronous_interface::RequestStatus::kRequestAssigned;
              result = request_id;
            }
          }
        }
      }
      break;
    }
    case FCGIType::kFCGI_DATA: {
      if((auto request_data_it = request_map_.find(request_id)) != request_map_.end())
      {
        if(!(request_data_it->second.FCGI_DATA_complete))
        {
          // Extract content length
          uint16_t content_length_1 = record_status.content_buffer_[4];
          uint16_t content_length_0 = record_status.content_buffer_[5];
          int content_length = (content_length_1 << 8) + content_length_0;
          if(content_length)
          {
            // Append data to RequestData stdin buffer

          }
          else // content_length == 0 (and stdin is complete)
          {
            request_data_it->second.FCGI_DATA_complete = true;
            if(request_data_it->second.FCGI_STDIN_complete && request_data_it->second.FCGI_PARAMS_complete)
            {
              request_data_it->second.request_status = fcgi_synchronous_interface::RequestStatus::kRequestAssigned;
              result = request_id;
            }
          }
        }
      }
      break;
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
  close(connection);
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
        || request_map_iter->first.descriptor() > connection));
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
