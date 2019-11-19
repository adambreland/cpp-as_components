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
  // (The file descriptor value 0 must correspond to the listening
  // socket per the FastCGI specification.)
  int flags = fcntl(0, F_GETFL);
  if(flags == -1) throw // TODO
  flags |= O_NONBLOCK;
  if(fcntl(0, F_SETFL, flags) == -1) throw // TODO

  // Access environment variables and check for valid IP addresses.
  if(const char* ip_addresses_ptr = std::getenv("FCGI_WEB_SERVER_ADDRS"))
    // TODO: Create or find a function to call to parse textual,
    // comma-delimited IP addresses.
}

std::vector<FCGIRequest> fcgi_synchronous_interface::FCGIApplicationInterface::
AcceptRequests()
{
  // Result to return.
  std::vector<FCGIRequest> requests {};

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
     + __FILE__ + ":\n"
     + "Line " + std::to_string(__LINE__));

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

  // Return value to be modified during processing.
  std::vector<RequestIdentifier> request_identifiers {};

  // Start reading from the connection until it blocks (no more data),
  // it is found to be disconnected, or an unrecoverable error occurs.
  // Each loop iteration acquires and then releases interface_state_mutex_
  // if an unrecoverable error does not occur.
  while(true)
  {
    // Read from socket.
    uint8_t read_buffer[kBufferSize];
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

        // Acquire interface_state_mutex_ to access and modify state.
        std::lock_guard<std::mutex> interface_state_lock
          {interface_state_mutex_};
        return ClosedConnectionFoundDuringAcceptRequests(connection);
      }

      if((errno != EAGAIN) && (errno != EWOULDBLOCK))
      // Unrecoverable error.
      throw; // TODO add exception content and pass errno value
    }

    // Process bytes received (if any). Needed as blocking errors return
    // zero bytes.
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
          if((int remaining_header {8 - record_status.bytes_received})
             <= number_bytes_remaining)
          {
            for(char i {record_status.bytes_received}; i < 8; i++)
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
          int content_byte_difference {8 + content_bytes_expected
            - record_status.bytes_received};
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
                std::lock_guard<std::mutex> interface_state_lock {interface_state_mutex_};
                auto request_map_iter {request_map_.find(request_id)};
                switch(record_status.type) {
                  case fcgi_synchronous_interface::FCGIType::kFCGI_PARAMS : {
                    request_map_iter->second.AppendToPARAMS(
                      &read_buffer[number_bytes_processed], number_to_write)
                    break;
                  }
                  case fcgi_synchronous_interface::FCGIType::kFCGI_STDIN : {
                    request_map_iter->second.AppendToSTDIN(
                      &read_buffer[number_bytes_processed], number_to_write)
                    break;
                  }
                  case fcgi_synchronous_interface::FCGIType::kFCGI_DATA : {
                    request_map_iter->second.AppendToDATA(
                      &read_buffer[number_bytes_processed], number_to_write)
                    break;
                  }
                }
              }
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
            {ProcessCompleteRecord(connection)};
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
  uint32_t temp_length {*content_ptr & 0x7f}; // mask out leading 1;
  uint32_t length {temp_length << 24};
  content_ptr++;
  temp_length = *content_ptr;
  length += temp_length << 16;
  content_ptr++;
  temp_length = *content_ptr;
  length += temp_length << 8;
  content_ptr++;
  temp_length = *content_ptr;
  length += temp_length;

  return length;
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

void fcgi_synchronous_interface::FCGIApplicationInterface::
SendGetValueResult(int connection, const RecordStatus& record_status)
{
  // TODO Consider removing length equality check.
  int record_content_length = record_status.content_bytes_expected_;
  int content_string_length = record_status.content_buffer_.size();
  if(record_content_length != content_string_length)
    throw std::runtime_error(
      "GetValueResult() encountered a mismatch between the expected\n"
      "content size and the actual size: \n"
     + __FILE__ + ":\n" + "Line " + std::to_string(__LINE__));


  uint8_t* content_ptr = record_status.content_buffer_.data();

  std::vector<std::pair<std::basic_string<uint8_t>, std::basic_string<uint8_t>>>
  name_list = ProcessBinaryNameValuePairs(record_content_length, content_ptr);




}

RequestIdentifier fcgi_synchronous_interface::FCGIApplicationInterface::
ProcessCompleteRecord(RecordStatus* record_status_ptr)
{
  // Retrieve record status for the connection.
  RequestIdentifier result {};
  RecordStatus& record_status {*record_status_ptr};
  // Extract type
  FCGIType type {record_status.content_buffer_[1]};
  // Extract FCGI request ID
  uint16_t FCGI_request_ID_1 = record_status.content_buffer_[2];
  uint16_t FCGI_request_ID_0 = record_status.content_buffer_[3];
  int FCGI_request_ID = (FCGI_request_ID_1 << 8) + FCGI_request_ID_0;
  // Check if it is a management record.
  if(FCGI_request_ID == 0)
  {
    if(type == FCGIType::kFCGI_GET_VALUES)
      SendGetValueResult(connection, record_status);
    else // Unknown type,
      SendFCGIUnknownType(connection, type);
    return result;
  }
  // Else, request_ID > 0 => application request record.
  // Check for the allowed application record types and act accordingly.
  // The allowed types are:
  // 1) kFCGI_BEGIN_REQUEST
  // 2) kFCGI_ABORT_REQUEST
  // 3) kFCGI_STDIN
  // 4) kFCGI_DATA
  // 5) kFCGI_PARAMS
  fcgi_synchronous_interface::RequestIdentifier request_ID {connection, request_ID};
  switch(type)
  {
    // Record types indicates a new request.
    case FCGIType::kFCGI_BEGIN_REQUEST: {
      if(request_map_.find(request_ID) == request_map_.end())
        request_map_[request_ID] = fcgi_synchronous_interface::RequestData();
      break;
    }
    // An abort record.
    case FCGIType::kFCGI_ABORT_REQUEST: {
      // Does the abort apply to a request?
      if((auto request_data_it = request_map_.find(request_ID))
        != request_map_.end())
      {
        if(!(request_data_it->second.abort))
        // The request has not already been aborted.
        {
          // Has the request already been assigned?
          if(request_data_it->second.request_status
            == RequestStatus::kRequestAssigned)
          {
            request_data_it->second.abort = true;
            result = request_ID; // The code which processes RequestIdentifiers will
                                 // create an abort FCGIRequest object for the application.
          }
          else // It hasn't been assigned. We can erase the request.
          {
            fcgi_synchronous_interface::FCGIRequest abort_request {request_ID, true};
            abort_request.AbortRespond(-1);
            request_map_.erase(request_data_it);
          }
        }
      }
      break;
    }
    // A stdin stream record.
    case FCGIType::kFCGI_STDIN: {
      // Does the record apply to a request?
      if((auto request_data_it = request_map_.find(request_ID)) != request_map_.end())
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
              result = request_ID;
            }
          }
        }
      }
      break;
    }
    // A data stream record.
    case FCGIType::kFCGI_DATA: {
      if((auto request_data_it = request_map_.find(request_ID)) != request_map_.end())
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
              result = request_ID;
            }
          }
        }
      }
      break;
    }
    // An environment variable (FCGI_PARAMS) stream record.
    case FCGIType::kFCGI_PARAMS: {

    }
  }
  // Clear the record status for the next record.
  record_status = RecordStatus();
  return result; // Default (null) RequestIdentifier if not assinged to.
}

std::vector<RequestIdentifier>
fcgi_synchronous_interface::FCGIApplicationInterface::
ClosedConnectionFoundDuringAcceptRequests(int connection)
{
  std::vector<RequestIdentifier> request_identifiers {};

  auto request_map_iter = request_map_.lower_bound(RequestIdentifier {connection, 0});

}
