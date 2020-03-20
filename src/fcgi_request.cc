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
#include <system_error>
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

// Class implementation notes:
// 1) Updating interface state:
//    a) Requests are responsible for removing themselves from their interface.
//       The interface will not remove an item from request_map_ if the
//       associated request has been assigned to the application.
//    b) Removal must occur when the request is no longer relevant to the
//       interface. This occurs when:
//       1) A call to Complete is made on the request.
//       2) Through calls on the request, it is detected that the client closed
//          the request connection.
//       3) Through calls on the request, it is detected that the interface
//          closed the request connection.
//       4) The destructor of a request is called and the request has not
//          yet removed itself from the interface.
//    c) The cases above may be viewed as occurring on the transition of 
//       completed_ from false to true. This should only occur once for a 
//       request and, once it has occurred, the request is no longer relevant
//       its interface.
//    d) Removing a request should be performed by:
//          interface_ptr_->RemoveRequest(request_identifier_);
//       The call to RemoveRequest maintains invariants on interface state.
//    e) When a request is completed and the connection of the request is
//       still open, the request should conditionally add the descriptor of the
//       connection to application_closure_request_set_ as per the value of
//       close_connection_.
// 2) Discipline for mutex acquisition and release:
//    a) Immediately after acquisition of interface_state_mutex_, a request
//       must check if its interface has been destroyed. This is done by
//       comparing the value of FCGIServerInterface::interface_identifier_ to
//       the value of associated_interface_id_.
//    b) Any use of:
//       1) The file descriptor of the connection (such as from
//          request_id.descriptor()) in a method which requires that the
//          file description associated with the descriptor is valid.
//       2) A write mutex.
//       by a request requires the request to check the value of
//       connection_closed_by_interface_ in the RequestData object associated
//       with the request. If the connection was closed, the state above cannot
//       be used.
//    c) Acquisition of a write mutex may only occur when interface_state_mutex_
//       is held.
//       1) FCGIRequest objects are separate from their associated
//          FCGIServerInterface object yet need to access state which belongs to
//          the interface. This means that the interface may be destroyed before
//          one of its associated requests. In particular, write mutexes, which
//          are part of the interface, may be destroyed while they are held by
//          request objects. This is undefined behavior. To prevent this, the
//          destructor of the interface acquires and releases each write mutex
//          under the protection of interface_state_mutex_. If requests only
//          acquire a write mutex under the protection of
//          interface_state_mutex_, no write mutexes will be acquired after
//          the destructor acquires interface_state_mutex_ and before the
//          destructor releases it. If all requests eventually release
//          interface_state_mutex_, then the destructor should acquire and
//          release all write mutexes. Once the destructor releases
//          interface_state_mutex_, requests may acquire it. However, if they
//          check interface identity, they will find that their interface has
//          been destroyed and, as such, that they cannot access interface
//          state. Thus it is ensured that the destructor will not destroy a
//          write mutex while a request holds that write mutex.
//   c) Once a write mutex has been acquired by a request under the protection
//      of interface_state_mutex_, the request may release
//      interface_state_mutex_ to write. Alternatively, the request may
//      defer releasing interface_state_mutex_ until after the write mutex
//      is released.
//   d) A request may never acquire interface_state_mutex_ while a write mutex
//      is held. Doing so may lead to deadlock.


// Implementation notes:
// Note that this constructor should only be called by an
// FCGIServerInterface object.
//
// Synchronization:
// 1) It is assumed that interface_state_mutex_ is held prior to a call.
fcgi_si::FCGIRequest::FCGIRequest(RequestIdentifier request_id,
  unsigned long interface_id,
  FCGIServerInterface* interface_ptr,
  RequestData* request_data_ptr,
  std::mutex* write_mutex_ptr)
: associated_interface_id_   {interface_id},
  interface_ptr_             {interface_ptr},
  request_identifier_        {request_id},
  request_data_ptr_          {request_data_ptr},
  environment_map_           {},
  request_stdin_content_     {},
  request_data_content_      {},
  role_                      {request_data_ptr->role_},
  close_connection_          {request_data_ptr->close_connection_},
  was_aborted_               {false},
  completed_                 {false},
  write_mutex_ptr_           {write_mutex_ptr}
{
  if((interface_ptr == nullptr || request_data_ptr == nullptr
     || write_mutex_ptr == nullptr)
     || (request_data_ptr_->request_status_ == RequestStatus::kRequestAssigned))
  {
    interface_ptr_->bad_interface_state_detected_ = true;
    throw std::logic_error {ERROR_STRING("An FCGIRequest could not be "
      "constructed.")};
  }

  environment_map_       = std::move(request_data_ptr->environment_map_);
  request_stdin_content_ = std::move(request_data_ptr->FCGI_STDIN_);
  request_data_content_  = std::move(request_data_ptr->FCGI_DATA_);

  // Update the status of the RequestData object to reflect its use in the
  // construction of an FCGIRequest which will be exposed to the application.
  request_data_ptr_->request_status_ = fcgi_si::RequestStatus::kRequestAssigned;
}

fcgi_si::FCGIRequest::FCGIRequest(FCGIRequest&& request) noexcept
: associated_interface_id_   {request.associated_interface_id_},
  interface_ptr_             {request.interface_ptr_},
  request_identifier_        {request.request_identifier_},
  request_data_ptr_          {request.request_data_ptr_},
  environment_map_           {std::move(request.environment_map_)},
  request_stdin_content_     {std::move(request.request_stdin_content_)},
  request_data_content_      {std::move(request.request_data_content_)},
  role_                      {request.role_},
  close_connection_          {request.close_connection_},
  was_aborted_               {request.was_aborted_},
  completed_                 {request.completed_},
  write_mutex_ptr_           {request.write_mutex_ptr_}
{
  request.associated_interface_id_ = 0;
  request.interface_ptr_ = nullptr;
  request.request_identifier_ = fcgi_si::RequestIdentifier {};
  request.request_data_ptr_ = nullptr;
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  request.close_connection_ = false;
  request.completed_ = true;
  request.write_mutex_ptr_ = nullptr;
}

fcgi_si::FCGIRequest&
fcgi_si::FCGIRequest::operator=(FCGIRequest&& request) noexcept
{
  if(this != &request)
  {
    associated_interface_id_ = request.associated_interface_id_;
    interface_ptr_ = request.interface_ptr_;
    request_identifier_ = request.request_identifier_;
    request_data_ptr_ = request.request_data_ptr_;
    environment_map_ = std::move(request.environment_map_);
    request_stdin_content_ = std::move(request.request_stdin_content_);
    request_data_content_ = std::move(request.request_data_content_);
    role_ = request.role_;
    close_connection_ = request.close_connection_;
    was_aborted_ = request.was_aborted_;
    completed_ = request.completed_;
    write_mutex_ptr_ = request.write_mutex_ptr_;

    request.associated_interface_id_ = 0;
    request.interface_ptr_ = nullptr;
    request.request_identifier_ = fcgi_si::RequestIdentifier {};
    request.request_data_ptr_ = nullptr;
    request.environment_map_.clear();
    request.request_stdin_content_.clear();
    request.request_data_content_.clear();
    // request.role_ is unchanged.
    request.close_connection_ = false;
    request.was_aborted_ = false;
    request.completed_ = true;
    request.write_mutex_ptr_ = nullptr;
  }
  return *this;
}

// If a request was not completed, the destructor attempts to remove the
// request from the interface.
//
// Exceptions:
// 1) An exception may be thrown by the attempt to acquire
//    interface_state_mutex_. This will result in program termination
//    if the destructor is being called by the exception handling mechanism.
//
// Synchronization:
// 1) Acquires and releases interface_state_mutex_.
//
// Effects:
// 1) If the request was not completed and the destructor exited normally,
//    one of the following applies:
//    a) the request was removed from the interface
//    b) the bad_interface_state_detected_ flag of the interface was set (as per
//    the specification of RemoveRequest).
fcgi_si::FCGIRequest::~FCGIRequest()
{
  if(!completed_)
  {
    // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock
      {FCGIServerInterface::interface_state_mutex_};
    // Check if the interface has not been destroyed.
    if(FCGIServerInterface::interface_identifier_ == associated_interface_id_)
    {
      // Try to remove the request from the interface.
      try 
      {
        interface_ptr_->RemoveRequest(request_identifier_);
        if(close_connection_)
        {
          interface_ptr_->application_closure_request_set_.insert(
          request_identifier_.descriptor());
        }
      }
      catch(...) 
      {
        if(close_connection_)
          interface_ptr_->bad_interface_state_detected_ = true;
      }
    }
  } // RELEASE interface_state_mutex_.
}

// Synchronization:
// 1) Acquires interface_state_mutex_.
bool fcgi_si::FCGIRequest::AbortStatus()
{
  if(completed_ || was_aborted_)
    return was_aborted_;

  // ACQUIRE interface_state_mutex_ to determine current abort state.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  // Check if the interface has been destroyed.
  if(FCGIServerInterface::interface_identifier_ != associated_interface_id_)
  {
    completed_ = true;
    was_aborted_ = true;
    throw std::runtime_error {ERROR_STRING("The FCGIServerInterface associated "
      "with an FCGIRequest object was destroyed before the request.")};
  }
  // Check if the connection has been closed by the interface.
  if(request_data_ptr_->connection_closed_by_interface_)
  {
    completed_   = true;
    was_aborted_ = true;
    interface_ptr_->RemoveRequest(request_identifier_);
    return was_aborted_;
  }

  if(request_data_ptr_->client_set_abort_)
    was_aborted_ = true;

  return was_aborted_;
} // RELEASE interface_state_mutex_

// Race condition discussion:
// If interface_state_mutex_ is not held for the duration of the write, it is
// possible that a race condition may occur. There are two steps to
// consider:
// 1) Removal of the request from the interface.
// 2) Notifying the client that the request is complete.
//
//    According to the mutex acquisition discipline, a write mutex can only be
// acquired when the interface mutex is held. Suppose that the request is
// removed from the interface and, as for other writes to the client, the
// interface mutex is released before the write starts. Then suppose that
// the client erroneously re-uses the request id of the request. The interface
// will accept a begin request record with this request id. A request could
// then be produced by the interface. The presence of two request objects which
// are associated with the same connection and which share a request identifier
// could cause several errors.
//   In this scenario, an error on the part of the client can corrupt interface
// state. Holding the interface mutex during the write prevents the interface
// from spuriously validating an erroneous begin request record.
bool fcgi_si::FCGIRequest::Complete(int32_t app_status)
{
  if(completed_)
    return false;

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

  // ACQUIRE interface_state_mutex_ to allow interface request_map_
  // update and to prevent race conditions between the client server and
  // the interface.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  if(!InterfaceStateCheckForWritingUponMutexAcquisition())
    return false;

  // Implicitly ACQUIRE and RELEASE *write_mutex_ptr_.
  bool write_return {ScatterGatherWriteHelper(iovec_array, seq_num,
    seq_num*fcgi_si::FCGI_HEADER_LEN, true)};

  // Update interface state and FCGIRequest state.
  if(write_return)
  {
    completed_ = true;
    interface_ptr_->RemoveRequest(request_identifier_);
    if(close_connection_)
    {
      try
      {
        interface_ptr_->application_closure_request_set_.insert(
          request_identifier_.descriptor());
      }
      catch(...)
      {
        interface_ptr_->bad_interface_state_detected_ = true;
        throw;
      }
    }
  }
  return write_return;
} // RELEASE interface_state_mutex_.

bool fcgi_si::FCGIRequest::
InterfaceStateCheckForWritingUponMutexAcquisition()
{
  // Check if the interface has been destroyed.
  if(FCGIServerInterface::interface_identifier_ != associated_interface_id_)
  {
    completed_ = true;
    was_aborted_ = true;

    throw std::runtime_error {ERROR_STRING("The FCGIServerInterface associated "
      "with an FCGIRequest object was destroyed before the request.")};
  }
  // Check if the interface has closed the connection.
  if(request_data_ptr_->connection_closed_by_interface_)
  {
    completed_   = true;
    was_aborted_ = true;
    interface_ptr_->RemoveRequest(request_identifier_);
    return false;
  }
  
  return true;
}

bool fcgi_si::FCGIRequest::
ScatterGatherWriteHelper(struct iovec* iovec_ptr, int iovec_count,
  const std::size_t number_to_write, bool interface_mutex_held)
{
  std::unique_lock<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_, std::defer_lock_t {}};

  // Conditionally ACQUIRE interface_state_mutex_.
  if(!interface_mutex_held)
  {
    interface_state_lock.lock();
    if(!InterfaceStateCheckForWritingUponMutexAcquisition())
      return false;
  }
  
  std::size_t working_number_to_write {number_to_write};
  // write_lock has the following property in the loop below:
  // The mutex is always held when writing and, once some data has been written,
  // the mutex is never released. This allows the write mutex to be released
  // while the thread sleeps in select in the case that writing blocks and
  // nothing was written.
  std::unique_lock<std::mutex> write_lock {*write_mutex_ptr_,
    std::defer_lock_t {}};
  int fd {request_identifier_.descriptor()};
  int select_descriptor_range {fd + 1};
  // A set for file descriptors for a select call below.
  fd_set write_set {};
  // interface_state_mutex_ is guaranteed to be held during the first iteration.
  bool first_iteration {true};
  while(working_number_to_write > 0) // Start write loop.
  {
    // Conditionally ACQUIRE interface_state_mutex_.
    // If the mutex is acquired, is is possible that the interface was destroyed
    // or that the connection was closed.
    if(!interface_mutex_held && !first_iteration && !write_lock.owns_lock())
    {
      interface_state_lock.lock();
      if(!InterfaceStateCheckForWritingUponMutexAcquisition())
        return false;
    }
    first_iteration = false;

    // Conditionally ACQUIRE *write_mutex_ptr_.
    if(!write_lock.owns_lock())
      write_lock.lock();
    // *write_mutex_ptr_ is held.

    // Conditionally RELEASE interface_state_mutex_ to free the interface
    // before the write.
    if(interface_state_lock.owns_lock())
      interface_state_lock.unlock();

    std::tuple<struct iovec*, int, std::size_t> write_return
      {socket_functions::ScatterGatherSocketWrite(fd, iovec_ptr, iovec_count,
        working_number_to_write)};
    if(std::get<2>(write_return) == 0)
    {
      // RELEASE *write_mutex_ptr_.
      write_lock.unlock();
      working_number_to_write = 0;
    }
    else // The number written is less than number_to_write. We must check 
         // errno.
    {
      // EINTR is handled by ScatterGatherSocketWrite.
      // Handle blocking errors.
      if(errno == EAGAIN || errno == EWOULDBLOCK)
      {
        // Check if nothing was written and nothing was written prior.
        if(std::get<2>(write_return) == number_to_write)
          // RELEASE *write_mutex_ptr_. (As no record content has been written.)
          write_lock.unlock();
        else // Some but not all was written.
          std::tie(iovec_ptr, iovec_count, working_number_to_write) =
            write_return;
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
            {
              std::error_code ec {errno, std::system_category()};
              throw std::system_error {ec, ERRNO_ERROR_STRING("select")};
            }
            // else: loop (EINTR is handled)
          }
          else
            break; // Exit select loop.
        }
      }
      // Handle a connection which was closed by the peer.
      // *write_mutex_ptr_ is held.
      else if(errno == EPIPE)
      {
        // *write_mutex_ptr_ MUST NOT be held to prevent potential deadlock.
        // The acquisition pattern "has write mutex, wants interface mutex"
        // is forbidden.
        // RELEASE *write_mutex_ptr_.
        write_lock.unlock();
        // Conditionally ACQUIRE interface_state_mutex_
        if(!interface_mutex_held)
        {
          interface_state_lock.lock();
          if(!InterfaceStateCheckForWritingUponMutexAcquisition())
            return false;
        }
        completed_   = true;
        was_aborted_ = true;
        interface_ptr_->RemoveRequest(request_identifier_);

        if(close_connection_)
        {
          try
          {
            interface_ptr_->application_closure_request_set_.insert(
              request_identifier_.descriptor());
          }
          catch(...)
          {
            interface_ptr_->bad_interface_state_detected_ = true;
            throw;
          }
        }
        // Conditionally RELEASE interface_state_mutex_.
        if(interface_state_lock.owns_lock())
          interface_state_lock.unlock();

        return false;
      } 
      // An unrecoverable error was encountered during the write.
      else
      {
        // RELEASE *write_mutex_ptr_.
        write_lock.unlock();
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, ERRNO_ERROR_STRING("write from a call to "
          "socket_functions::SocketWrite")};
      }
    } // End handling incomplete writes. Loop.
  } // Exit write loop.
  return true;
}
