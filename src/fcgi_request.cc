#include <sys/select.h>
#include <sys/time.h>           // For portable use of select.
#include <sys/types.h>          // For ssize_t and portable use of select.
#include <sys/uio.h>
#include <unistd.h>             // For portable use of select among others.

#include <cerrno>
#include <cstdint>

#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "external/error_handling/include/error_handling.h"
#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_data.h"
#include "include/request_identifier.h"
#include "include/utility.h"

// Numerical assumptions:
// 1) std::size_t >= uint16_t
// 2) std::size_t >= decltype(std::distance(begin_iter, end_iter)) for
//    std::vector<uint8_t> iterators.

// Question: Are size_t and std::size_t the same?

fcgi_si::FCGIRequest::FCGIRequest(fcgi_si::RequestIdentifier request_id,
  fcgi_si::FCGIServerInterface* interface_ptr,
  fcgi_si::RequestData* request_data_ptr,
  std::mutex* write_mutex_ptr, std::mutex* interface_state_mutex_ptr)
: interface_ptr_             {interface_ptr},
  request_identifier_        {request_id},
  environment_map_           {std::move(request_data_ptr->environment_map_)},
  request_stdin_content_     {std::move(request_data_ptr->FCGI_STDIN_)},
  request_data_content_      {std::move(request_data_ptr->FCGI_DATA_)},
  role_                      {request_data_ptr->role_},
  close_connection_          {request_data_ptr->close_connection_},
  was_aborted_               {false},
  completed_                 {false},
  write_mutex_ptr_           {write_mutex_ptr},
  interface_state_mutex_ptr_ {interface_state_mutex_ptr}
{
  if(interface_ptr == nullptr || request_data_ptr == nullptr
     || write_mutex_ptr == nullptr || interface_state_mutex_ptr == nullptr)
    throw std::invalid_argument {ERROR_STRING("A pointer with a nullptr value was used to construct an FCGIRequest object.")};

  // Update the status of the RequestData object to reflect its use in the
  // construction of an FCGIRequest which will be exposed to the application.

  // NOTE that this constructor should only be called by an
  // FCGIServerInterface object. It is assumed that synchronization is
  // implicit and that acquiring the shared state mutex is not necessary.
  request_data_ptr->request_status_ = fcgi_si::RequestStatus::kRequestAssigned;
}

fcgi_si::FCGIRequest::FCGIRequest(FCGIRequest&& request)
: interface_ptr_             {request.interface_ptr_},
  request_identifier_        {request.request_identifier_},
  environment_map_           {std::move(request.environment_map_)},
  request_stdin_content_     {std::move(request.request_stdin_content_)},
  request_data_content_      {std::move(request.request_data_content_)},
  role_                      {request.role_},
  close_connection_          {request.close_connection_},
  was_aborted_               {request.was_aborted_},
  completed_                 {request.completed_},
  write_mutex_ptr_           {request.write_mutex_ptr_},
  interface_state_mutex_ptr_ {request.interface_state_mutex_ptr_}
{
  request.interface_ptr_ = nullptr;
  request.request_identifier_ = fcgi_si::RequestIdentifier {};
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  request.close_connection_ = false;
  request.completed_ = true;
  request.write_mutex_ptr_ = nullptr;
  request.interface_state_mutex_ptr_ = nullptr;
}

fcgi_si::FCGIRequest& fcgi_si::FCGIRequest::operator=(FCGIRequest&& request)
{
  if(this != &request)
  {
    interface_ptr_ = request.interface_ptr_;
    request_identifier_ = request.request_identifier_;
    environment_map_ = std::move(request.environment_map_);
    request_stdin_content_ = std::move(request.request_stdin_content_);
    request_data_content_ = std::move(request.request_data_content_);
    role_ = request.role_;
    close_connection_ = request.close_connection_;
    was_aborted_ = request.was_aborted_;
    completed_ = request.completed_;
    write_mutex_ptr_ = request.write_mutex_ptr_;
    interface_state_mutex_ptr_ = request.interface_state_mutex_ptr_;

    request.interface_ptr_ = nullptr;
    request.request_identifier_ = fcgi_si::RequestIdentifier {};
    request.environment_map_.clear();
    request.request_stdin_content_.clear();
    request.request_data_content_.clear();
    // request.role_ is unchanged.
    request.close_connection_ = false;
    request.was_aborted_ = false;
    request.completed_ = true;
    request.write_mutex_ptr_ = nullptr;
    request.interface_state_mutex_ptr_ = nullptr;
  }
  return *this;
}

bool fcgi_si::FCGIRequest::AbortStatus()
{
  if(completed_ || was_aborted_)
    return was_aborted_;

  // ACQUIRE interface_state_mutex_ to determine current abort state.
  std::lock_guard<std::mutex> interface_state_lock {*interface_state_mutex_ptr_};
  auto request_map_iter = interface_ptr_->request_map_.find(request_identifier_);
  // Include check for absence of request to prevent undefined method call.
  // This check should always pass.
  if(request_map_iter != interface_ptr_->request_map_.end())
    if(request_map_iter->second.get_abort())
      return was_aborted_ = true;
  // TODO Remove check for request_data_ptr invalidation when there is
  // confidence that invalidation cannot occur.

  return was_aborted_; // i.e. return false.
} // RELEASE interface_state_mutex_

void fcgi_si::FCGIRequest::Complete(int32_t app_status)
{
  constexpr char seq_num {4}; // Three headers and an 8-byte body. 3+1=4
  uint8_t header_and_end_content[seq_num][fcgi_si::FCGI_HEADER_LEN];

  fcgi_si::PopulateHeader(&header_and_end_content[0][0],
    fcgi_si::FCGIType::kFCGI_STDOUT,
    request_identifier_.FCGI_id(), 0, 0);
  fcgi_si::PopulateHeader(&header_and_end_content[1][0],
    fcgi_si::FCGIType::kFCGI_STDERR,
    request_identifier_.FCGI_id(), 0, 0);
  fcgi_si::PopulateHeader(&header_and_end_content[2][0],
    fcgi_si::FCGIType::kFCGI_END_REQUEST,
    request_identifier_.FCGI_id(), fcgi_si::FCGI_HEADER_LEN, 0);

  // Fill end request content.
  for(char i {0}; i < seq_num ; i++)
    header_and_end_content[3][i] = static_cast<uint8_t>(
      app_status >> (24 - (8*i)));
  header_and_end_content[3][4] = fcgi_si::FCGI_REQUEST_COMPLETE;
  for(char i {5}; i < fcgi_si::FCGI_HEADER_LEN; i++)
    header_and_end_content[3][i] = 0;

  // Fill iovec structures
  struct iovec iovec_array[seq_num];
  for(char i {0}; i < seq_num; i++)
  {
    iovec_array[i].iov_base = &header_and_end_content[i][0];
    iovec_array[i].iov_len  = fcgi_si::FCGI_HEADER_LEN;
  }

  // Update FCGIRequest state.
  completed_ = true;

  // ACQUIRE *interface_state_mutex_ptr_ to allow interface request_map_
  // update and to prevent race conditions between the client server and
  // the interface.
  std::lock_guard<std::mutex> interface_state_lock {*interface_state_mutex_ptr_};

  // Implicitly ACQUIRE and RELEASE *write_mutex_ptr_.
  WriteHelper(request_identifier_.descriptor(), iovec_array, seq_num,
    seq_num*fcgi_si::FCGI_HEADER_LEN, true);

  // Update interface state.
  interface_ptr_->RemoveRequest(request_identifier_);
  if(close_connection_)
    interface_ptr_->application_closure_request_set_.insert(
      request_identifier_.descriptor());
} // RELEASE *interface_state_mutex_ptr_.

void fcgi_si::FCGIRequest::CompleteAfterDiscoveredClosedConnection()
{
  if(!completed_)
  {
    interface_ptr_->RemoveRequest(request_identifier_);
    completed_   = true;
    was_aborted_ = true;
    if(close_connection_)
      interface_ptr_->application_closure_request_set_.insert(
        request_identifier_.descriptor());
  }
}

bool fcgi_si::FCGIRequest::
PartitionByteSequence(const std::vector<uint8_t>& ref,
  std::vector<uint8_t>::const_iterator begin_iter,
  std::vector<uint8_t>::const_iterator end_iter, fcgi_si::FCGIType type)
{
  if(completed_)
    return false;
  if(begin_iter == end_iter)
    return true;

  // message_length may be undefined for extremely large vectors as the
  // difference_type of std::vector may not be able to store the length
  // of the vector (or the length of an interval of it).
  auto message_length = std::distance(begin_iter, end_iter);
  auto message_offset = std::distance(ref.begin(), begin_iter);
  const uint8_t* message_ptr = ref.data() + message_offset;

  // Determine the number of full records and the length of a partial record
  // if present. Determine the padding length for the partial record.
  decltype(message_length) full_record_count
    {message_length / fcgi_si::kMaxRecordContentByteLength};
  uint16_t partial_record_length
    {static_cast<uint16_t>(message_length % fcgi_si::kMaxRecordContentByteLength)};
  bool partial_record_count {partial_record_length > 0};
  uint8_t padding_count {(partial_record_length % fcgi_si::FCGI_HEADER_LEN) ?
    static_cast<uint8_t>(fcgi_si::FCGI_HEADER_LEN -
      (partial_record_length % fcgi_si::FCGI_HEADER_LEN)) :
    static_cast<uint8_t>(0)};

  uint8_t padding[fcgi_si::FCGI_HEADER_LEN] = {}; // Initialize to all zeroes.

  // Populate header for the partial record.
  uint8_t header[fcgi_si::FCGI_HEADER_LEN];
  fcgi_si::PopulateHeader(header, type, request_identifier_.FCGI_id(),
    partial_record_length, padding_count);

  // Initialize the iovec structures.
  // Ensure that an overflow does not occur when we initialize iovec_count.
  // Note that the limit placed on the number of buffers by writev is
  // (usually) much less than std::numeric_limits<int>::max(). Ensuring that
  // this limit is not surpassed is required for successful use of
  // FCGIRequest::Write. A throw will occur if writev returns an invalid size
  // error.

  // Determine the number of buffers (which will be the number of iovec structs).
  // Double the number of records as each record has a header buffer.
  // Include the padding buffer.
  decltype(message_length) iovec_count_iter_distance_units
    {(2*(partial_record_count + full_record_count))+(padding_count > 0)};
  if(iovec_count_iter_distance_units > std::numeric_limits<int>::max())
    throw std::logic_error {ERROR_STRING("PartitionByteSequence received a byte sequence length which resulted\nin more records than the maximum value of int.")};
  int iovec_count {static_cast<int>(iovec_count_iter_distance_units)};
  std::unique_ptr<struct iovec[]> iovec_array_ptr {new struct iovec[iovec_count]};
  // Initialize iovec strcutre for the partial record if there is one.
  int i {0}; // struct iovect structure index in iovec_array on the heap.
  if(partial_record_count)
  {
    // Header
    (iovec_array_ptr.get()+i)->iov_base = static_cast<void*>(header);
    (iovec_array_ptr.get()+i)->iov_len  = fcgi_si::FCGI_HEADER_LEN;
    i++;
    // Content
    (iovec_array_ptr.get()+i)->iov_base = (void*)message_ptr; // Need to remove const.
    (iovec_array_ptr.get()+i)->iov_len  = partial_record_length;
    i++;
    // Update the content pointer.
    message_ptr += partial_record_length;
    // Conditionally include padding.
    if(padding_count)
    {
      (iovec_array_ptr.get()+i)->iov_base = padding;
      (iovec_array_ptr.get()+i)->iov_len  = padding_count;
      i++;
    }
  }
  if(i < iovec_count)
  {
    // Initialize all other iovec structures for full records.
    // Update header.
    header[fcgi_si::kHeaderContentLengthB1Index] =
      0xff; // A full record has a content length value with a bit sequence of 16 ones.
    header[fcgi_si::kHeaderContentLengthB0Index] =
      0xff;
    header[fcgi_si::kHeaderPaddingLengthIndex]   =
      0;
    for(/*no-op*/; i < iovec_count; /*no-op*/)
    {
      // Header
      (iovec_array_ptr.get()+i)->iov_base = static_cast<void*>(header);
      (iovec_array_ptr.get()+i)->iov_len = fcgi_si::FCGI_HEADER_LEN;
      i++;
      // Content
      (iovec_array_ptr.get()+i)->iov_base = (void*)message_ptr; // Need to remove const.
      (iovec_array_ptr.get()+i)->iov_len = fcgi_si::kMaxRecordContentByteLength;
      i++;
      // Update the content pointer.
      message_ptr += fcgi_si::kMaxRecordContentByteLength;
    }
  }
  decltype(message_length) total_write_length
    {message_length + padding_count
    + (fcgi_si::FCGI_HEADER_LEN * (full_record_count + partial_record_count))};

  return WriteHelper(request_identifier_.descriptor(), iovec_array_ptr.get(),
    iovec_count, total_write_length, false);
}

bool fcgi_si::FCGIRequest::
WriteHelper(int fd, struct iovec* iovec_ptr, int iovec_count,
  std::size_t number_to_write, bool interface_mutex_held)
{
  bool connection_open_at_peer {true};
  std::unique_lock<std::mutex> write_lock {*write_mutex_ptr_, std::defer_lock_t {}};
  bool acquired_mutex {false};
  int select_descriptor_range {fd + 1};
  fd_set write_set {};

  while(number_to_write > 0) // Start write loop.
  {
    // Conditionally ACQUIRE *write_mutex_ptr_.
    if(!write_lock.owns_lock())
    {
      write_lock.lock();
      acquired_mutex = true;
    }
    // *write_mutex_ptr_ is held.
    std::tuple<struct iovec*, int, std::size_t> write_return
      {socket_functions::ScatterGatherSocketWrite(fd, iovec_ptr, iovec_count,
        number_to_write)};
    if(std::get<2>(write_return) == 0)
      number_to_write = 0;
    else // The number written is less than number_to_write. We must check errno.
    {
      // EINTR is handled by ScatterGatherSocketWrite.
      // Handle blocking errors.
      if(errno == EAGAIN || errno == EWOULDBLOCK)
      {
        // Check if nothing was written and nothing was written prior.
        if(std::get<2>(write_return) == number_to_write && acquired_mutex)
          // RELEASE *write_mutex_ptr_. (As no record content has been written.)
          write_lock.unlock();
        else // Some but not all was written.
          std::tie(iovec_ptr, iovec_count, number_to_write) = write_return;
        // Reset flag for mutex acquisition as the loop will iterate.
        acquired_mutex = false;
        // Call select with error handling to wait until a write won't block.
        while(true) // Start select loop.
        {
          // The loop exits only when writing won't block or an error is thrown.
          FD_ZERO(&write_set);
          FD_SET(fd, &write_set);
          if(select(select_descriptor_range, nullptr, &write_set, nullptr,
            nullptr) == -1)
          {
            if(errno != EINTR)
              throw std::runtime_error {ERRNO_ERROR_STRING("select")};
            // else: loop (EINTR is handled)
          }
          else
            break; // Exit select loop.
        }
      } // End blocking error handling.
      // Handle a connection which was closed by the peer.
      else if(errno == EPIPE)
      {
        // Conditionally ACQUIRE interface_state_mutex_
        if(!interface_mutex_held)
        {
          // ACQUIRE interface_state_mutex_.
          std::lock_guard<std::mutex> interface_state_lock
            {*interface_state_mutex_ptr_};
          CompleteAfterDiscoveredClosedConnection();
        } // RELEASE interface_state_mutex_.
        else
          CompleteAfterDiscoveredClosedConnection();
        connection_open_at_peer = false;
        break; // Exit write loop.
      }
      else // Cannot handle the error.
        throw std::runtime_error {ERRNO_ERROR_STRING("write from a call to socket_functions::SocketWrite")};
    } // End handling incomplete writes. Loop.
  } // Exit write loop.
  write_lock.unlock(); // RELEASE *write_mutex_ptr_.
  return connection_open_at_peer;
}
