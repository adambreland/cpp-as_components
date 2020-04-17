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

#include "external/socket_functions/include/socket_functions.h"

#include "include/fcgi_request.h"
#include "include/fcgi_server_interface.h"
#include "include/protocol_constants.h"
#include "include/request_data.h"
#include "include/request_identifier.h"
#include "include/utility.h"

// Class implementation notes:
// 1) Updating interface state:
//    a) Removing requests from the collection of requests tracked by the
//       interface:
//          Requests are responsible for removing themselves from their
//       interface. The interface will not remove an item from request_map_ if
//       the associated request has been assigned to the application.
//       (Assignment and FCGIRequest object construction are equivalent.)
//          Removal must occur when the request is no longer relevant to the
//       interface. This occurs when:
//       1) A call to Complete is made on the request.
//       2) Through calls on the request, it is detected that the client closed
//          the request connection.
//       3) Through calls on the request, it is detected that the interface
//          closed the request connection.
//       4) Through calls on a request, the request informs the interface
//          that the connection of the request should be closed because the
//          request corrupted the connection from a partial write.
//       5) Through calls on the request, the request discovers that the
//          connection of the request has been corrupted.
//       6) The destructor of a request is called and the request has not
//          yet removed itself from the interface.
//       
//          The cases above may be viewed as occurring on the transition of 
//       completed_ from false to true. This should only occur once for a 
//       request and, once it has occurred, the request is no longer relevant
//       to its interface.
//          Removing a request should be performed by:
//             interface_ptr_->RemoveRequest(request_identifier_);
//       The call to RemoveRequest maintains invariants on interface state.
//    b) Updating interface state for connection closure.
//       1) Normal connection closure processing.
//             When a request is completed and the connection of the request is
//          still open, the request should conditionally add the descriptor of
//          the connection to application_closure_request_set_ according to the
//          value of close_connection_. In other words, 
//          application_closure_request_set_ should be modified if: 
//             close_connection_ && 
//               !(request_data_ptr_->connection_closed_by_interface_)
//       2) Connection closure processing due to connection corruption.
//             Because the FastCGI protocol is based on records, a partial
//          write to a connection from the server to the client corrupts the
//          connection. Partial writes only occur when an error prevents a
//          write from being completed. In this case, the server must abort
//          requests on the connection. This is done in the FastCGI protocol
//          by closing the connection. Note that the request cannot be ended
//          with a failure status as doing so would require writing an
//          FCGI_END_REQUEST record on the corrupted connection.
//             The shared application_closure_request_set_ of the request's
//          FCGIServerInterface object is used to indicate that the connection
//          should be closed in this case.
//    c) Indicating that a connection is corrupt.
//          When a request corrupts its connection from a partial write:
//       1) It must set the flag associated with the connection's write mutex.
//          This must be performed under the protection of the write mutex as 
//          this flag is shared state.
//       2) It must add the descriptor of the connection to
//          application_closure_request_set_. This is described in 1.b.2 above.
//    d) Putting the interface into a bad state.
//          Anytime interface state should be updated but the update cannot be
//       made due to an error, the interface should be put into a bad state
//       by setting interface_ptr_->bad_interface_state_detected_. If the
//       interface has been destroyed, has already been put into a bad state,
//       or the connection of the request has been closed, then the bad state
//       flag need not be set.
//    e) Terminating the program:
//          If the interface cannot be put into a bad state, regardless of
//       the whether the desire to put the interface into a bad state was
//       direct or the result of another error, the program must be terminated.
//
// 2) Discipline for mutex acquisition and release:
//    a) Immediately after acquisition of interface_state_mutex_, a request
//       must check if:
//       1) Its interface has been destroyed. This is done by comparing the 
//          value of FCGIServerInterface::interface_identifier_ to the value of
//          associated_interface_id_.
//       2) Its interface is in a bad state. This is done after the check for
//          interface destruction by checking if 
//          bad_interface_state_detected_ == true.
//    b) Any use of:
//       1) The file descriptor of the connection (such as from
//          request_id.descriptor()) in a method which requires that the
//          file description associated with the descriptor is valid.
//       2) A write mutex.
//       by a request requires the request to check the value of
//       connection_closed_by_interface_ in the RequestData object associated
//       with the request. If the connection was closed, the state above cannot
//       be used.
//    c) Any write to the connection must be preceded by a check for connection
//       corruption. This is done under the protection of the write mutex by
//       checking if the boolean value associated with the write mutex has been
//       set.
//    d) Acquisition of a write mutex may only occur when interface_state_mutex_
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
//
// 3) Other disciplines:
//    a) Only shared data members may be accessed by FCGIRequest. These data
//       members must be accessed under mutex protection.
//    b) The FCGIServerInterface data member write_mutex_map_ must not be
//       accessed directly. A write mutex must only be accessed through an
//       FCGIRequest object's write_mutex_ptr_. In other words, the mutexes are
//       shared, but the map which stores them is not. FCGIServerInterface may
//       treat the map as a non-shared data member which locates shared objects.
//    c) Of the methods of FCGIServerInterface, only RemoveRequest may be
//       called. It must be called under mutex protection.
//
// 4) General notes:
// a) The destructor of an FCGIRequest object acquires and releases
//    FCGIServerInterface::interface_state_mutex_. This is not problematic
//    when requests are destroyed within the scope of user code. It will
//    lead to deadlock in implementation code if the destructor is executed
//    in a scope which owns the interface mutex.
namespace fcgi_si {

// Implementation notes:
// This constructor should only be called by an FCGIServerInterface object.
//
// Synchronization:
// 1) It is assumed that interface_state_mutex_ is held prior to a call.
FCGIRequest::FCGIRequest(
  RequestIdentifier request_id,
  unsigned long interface_id,
  FCGIServerInterface* interface_ptr,
  RequestData* request_data_ptr,
  std::mutex* write_mutex_ptr,
  bool* bad_connection_state_ptr)
  : associated_interface_id_   {interface_id},
    interface_ptr_             {interface_ptr},
    request_identifier_        {request_id},
    request_data_ptr_          {request_data_ptr},
    write_mutex_ptr_           {write_mutex_ptr},
    bad_connection_state_ptr_  {bad_connection_state_ptr},
    environment_map_           {},
    request_stdin_content_     {},
    request_data_content_      {},
    role_                      {request_data_ptr->role_},
    close_connection_          {request_data_ptr->close_connection_},
    was_aborted_               {false},
    completed_                 {false}
{
  if((interface_ptr == nullptr || request_data_ptr == nullptr
     || write_mutex_ptr == nullptr || bad_connection_state_ptr_ == nullptr)
     || (request_data_ptr_->request_status_ == RequestStatus::kRequestAssigned))
  {
    interface_ptr_->bad_interface_state_detected_ = true; 
    throw std::logic_error {"An FCGIRequest could not be "
      "constructed."};
  }

  // TODO double check noexcept specifications for move assignments.
  // It is currently assumed that move assignments are noexcept.
  // This assumption also applies to the move constructor and move assignment
  // operator.
  environment_map_       = std::move(request_data_ptr->environment_map_);
  request_stdin_content_ = std::move(request_data_ptr->FCGI_STDIN_);
  request_data_content_  = std::move(request_data_ptr->FCGI_DATA_);
  
  // Update the status of the RequestData object to reflect its use in the
  // construction of an FCGIRequest which will be exposed to the application.
  request_data_ptr_->request_status_ = RequestStatus::kRequestAssigned;
}

FCGIRequest::FCGIRequest(FCGIRequest&& request) noexcept
: associated_interface_id_   {request.associated_interface_id_},
  interface_ptr_             {request.interface_ptr_},
  request_identifier_        {request.request_identifier_},
  request_data_ptr_          {request.request_data_ptr_},
  write_mutex_ptr_           {request.write_mutex_ptr_},
  bad_connection_state_ptr_  {request.bad_connection_state_ptr_},
  environment_map_           {std::move(request.environment_map_)},
  request_stdin_content_     {std::move(request.request_stdin_content_)},
  request_data_content_      {std::move(request.request_data_content_)},
  role_                      {request.role_},
  close_connection_          {request.close_connection_},
  was_aborted_               {request.was_aborted_},
  completed_                 {request.completed_}
{
  request.associated_interface_id_ = 0;
  request.interface_ptr_ = nullptr;
  request.request_identifier_ = RequestIdentifier {};
  request.request_data_ptr_ = nullptr;
  request.write_mutex_ptr_ = nullptr;
  request.bad_connection_state_ptr_ = nullptr;
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  // request.role_ is unchanged.
  request.close_connection_ = false;
  request.completed_ = true;
}

FCGIRequest& FCGIRequest::operator=(FCGIRequest&& request)
{
  if(this != &request)
  {
    if((!completed_ || associated_interface_id_ != 0))
      throw std::logic_error {""};

    associated_interface_id_ = request.associated_interface_id_;
    interface_ptr_ = request.interface_ptr_;
    request_identifier_ = request.request_identifier_;
    request_data_ptr_ = request.request_data_ptr_;
    write_mutex_ptr_ = request.write_mutex_ptr_;
    bad_connection_state_ptr_ = request.bad_connection_state_ptr_;
    environment_map_ = std::move(request.environment_map_);
    request_stdin_content_ = std::move(request.request_stdin_content_);
    request_data_content_ = std::move(request.request_data_content_);
    role_ = request.role_;
    close_connection_ = request.close_connection_;
    was_aborted_ = request.was_aborted_;
    completed_ = request.completed_;

    request.associated_interface_id_ = 0;
    request.interface_ptr_ = nullptr;
    request.request_identifier_ = RequestIdentifier {};
    request.request_data_ptr_ = nullptr;
    request.write_mutex_ptr_ = nullptr;
    request.bad_connection_state_ptr_ = nullptr;
    request.environment_map_.clear();
    request.request_stdin_content_.clear();
    request.request_data_content_.clear();
    // request.role_ is unchanged.
    request.close_connection_ = false;
    request.was_aborted_ = false;
    request.completed_ = true;
  }
  return *this;
}

// Implementation specification:
// If a request was not completed, the destructor attempts to update interface
// state to reflect that the request is no longer relevant to the interface.
//
// Exceptions:
// 1) An exception may be thrown by the attempt to acquire
//    interface_state_mutex_. This will result in program termination.
//
// Synchronization:
// 1) Acquires and releases interface_state_mutex_.
//
// Effects:
// 1) If the request was not completed and the destructor exited normally,
//    one of the following applies:
//    a) The bad_interface_state_detected_ flag of the interface was not set
//       and interface state was updated:
//       1) The request was removed from the interface.
//       2) If close_connection_ and the connection was not already closed,
//          the descriptor of the connection was added to
//          application_closure_request_set_.
//    b) Interface state could not be updated successfully. The
//       bad_interface_state_detected_ flag of the interface was set.
// 2) If the request was completed, the call had no effect.
FCGIRequest::~FCGIRequest()
{
  if(!completed_)
  {
    // ACQUIRE interface_state_mutex_.
    std::unique_lock<std::mutex> interface_state_lock
      {FCGIServerInterface::interface_state_mutex_, std::defer_lock};
    try
    {
      interface_state_lock.lock();
    }
    catch(...)
    {
      // bad_interface_state_detected_ cannot be set. The program must end.
      std::terminate();
    }
    // Check if the interface has not been destroyed and is not in a bad state.
    if((FCGIServerInterface::interface_identifier_ == associated_interface_id_)
       && (interface_ptr_->bad_interface_state_detected_ == false))
    {
      // Try to remove the request from the interface.
      try 
      {
        // completed_ may be regarded as being implicitly set. There is no need
        // to actually set it as the request is being destroyed.

        // Check for connection closure first as request_data_ptr_ is about to
        // be invalidated by RemoveRequest.
        if(close_connection_ 
           && !(request_data_ptr_->connection_closed_by_interface_))
        {
          interface_ptr_->application_closure_request_set_.insert(
          request_identifier_.descriptor());
        }
        interface_ptr_->RemoveRequest(request_identifier_);
      }
      catch(...) 
      {
        interface_ptr_->bad_interface_state_detected_ = true;
      }
    }
  } // RELEASE interface_state_mutex_.
}

// Implementation notes:
// Synchronization:
// 1) Acquires interface_state_mutex_.
bool FCGIRequest::AbortStatus()
{
  if(completed_ || was_aborted_)
    return was_aborted_;
  
  // The actual abort status is unknown if this point is reached.
  // ACQUIRE interface_state_mutex_ to determine current abort status.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  // Check if the interface has been destroyed.
  if(FCGIServerInterface::interface_identifier_ != associated_interface_id_)
  {
    completed_ = true;
    was_aborted_ = true;
    throw std::runtime_error {"The FCGIServerInterface associated "
      "with an FCGIRequest object was destroyed before the request."};
  }
  // Check if the interface is in a bad state.
  if(interface_ptr_->bad_interface_state_detected_)
  {
    completed_ = true;
    was_aborted_ = true;
    throw std::runtime_error {"The FCGIServerInterface associated "
      "with an FCGIRequest object was in a bad state."};
  }
  // Check if the connection has been closed by the interface.
  if(request_data_ptr_->connection_closed_by_interface_)
  {
    completed_   = true;
    was_aborted_ = true;
    // RemoveRequest implicitly sets bad_interface_state_detected_ if it
    // throws.
    interface_ptr_->RemoveRequest(request_identifier_);
    return was_aborted_;
  }

  if(request_data_ptr_->client_set_abort_)
    was_aborted_ = true;

  return was_aborted_;
} // RELEASE interface_state_mutex_.

// Implementation notes:
// Synchronization:
// 1) Acquires and releases interface_state_mutex_.
// 2) May acquire and release a write mutex.
//
// Race condition discussion:
// If interface_state_mutex_ is not held for the duration of the write, it is
// possible that a race condition may occur. There are two steps to
// consider:
// 1) Removing the request from the interface.
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
bool FCGIRequest::Complete(std::int32_t app_status)
{
  if(completed_)
    return false;

  constexpr int seq_num {4}; // Three headers and an 8-byte body. 3+1=4
  constexpr int app_status_byte_length {32 / 8};
  uint8_t header_and_end_content[seq_num][FCGI_HEADER_LEN];

  PopulateHeader(&header_and_end_content[0][0],
    FCGIType::kFCGI_STDOUT,
    request_identifier_.FCGI_id(), 0, 0);
  PopulateHeader(&header_and_end_content[1][0],
    FCGIType::kFCGI_STDERR,
    request_identifier_.FCGI_id(), 0, 0);
  PopulateHeader(&header_and_end_content[2][0],
    FCGIType::kFCGI_END_REQUEST,
    request_identifier_.FCGI_id(), FCGI_HEADER_LEN, 0);

  // Fill end request content.
  for(int i {0}; i < app_status_byte_length ; ++i)
    header_and_end_content[3][i] = static_cast<uint8_t>(
      app_status >> (24 - (8*i)));
  header_and_end_content[3][app_status_byte_length] = 
    FCGI_REQUEST_COMPLETE;
  for(int i {app_status_byte_length + 1}; i < FCGI_HEADER_LEN; ++i)
    header_and_end_content[3][i] = 0;

  // Fill iovec structure for a call to ScatterGatherWriteHelper.
  constexpr std::size_t number_to_write {seq_num*FCGI_HEADER_LEN};
  struct iovec iovec_wrapper[1] = {{header_and_end_content, number_to_write}};

  // ACQUIRE interface_state_mutex_ to allow interface request_map_
  // update and to prevent race conditions between the client server and
  // the interface.
  std::lock_guard<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_};
  if(!InterfaceStateCheckForWritingUponMutexAcquisition())
    return false;

  // Implicitly ACQUIRE and RELEASE *write_mutex_ptr_.
  bool write_return {ScatterGatherWriteHelper(iovec_wrapper, 1, number_to_write,
    true)};

  // Update interface state and FCGIRequest state.
  //
  // If write_return is false, ScatterGatherWriteHelper updated interface
  // state by removing the request. Also, the connection is closed. The
  // descriptor does not need to be conditionally added to
  // application_closure_request_set_.
  if(write_return)
  {
    completed_ = true;
    interface_ptr_->RemoveRequest(request_identifier_);
    if(close_connection_)
    {
      try
      {
        // It's possible that the the descriptor is already in the set.
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

bool FCGIRequest::
InterfaceStateCheckForWritingUponMutexAcquisition()
{
  // Check if the interface has been destroyed.
  if(FCGIServerInterface::interface_identifier_ != associated_interface_id_)
  {
    completed_   = true;
    was_aborted_ = true;
    throw std::runtime_error {"The FCGIServerInterface associated "
      "with an FCGIRequest object was destroyed before the request."};
  }
  // Check if the interface is in a bad state.
  if(interface_ptr_->bad_interface_state_detected_)
  {
    completed_   = true;
    was_aborted_ = true;
    throw std::runtime_error {"The FCGIServerInterface associated "
      "with an FCGIRequest object was in a bad state."};
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

bool FCGIRequest::
ScatterGatherWriteHelper(struct iovec* iovec_ptr, int iovec_count,
  std::size_t number_to_write, bool interface_mutex_held)
{
  std::unique_lock<std::mutex> interface_state_lock
    {FCGIServerInterface::interface_state_mutex_, std::defer_lock};

  // An internal helper function used when application_closure_request_set_
  // of the interface may be be accessed. This occurs when:
  // 1) The connection was found to be closed and close_connection_ == true
  // 2) The connection from the server to the client was corrupted from
  //    an incomplete write.
  //
  // If insert == true, insertion is attempted.
  // If insert == false, insertion is attempted if close_connection == true.
  auto TryToAddToApplicationClosureRequestSet = 
  [this, interface_mutex_held, &interface_state_lock](bool insert)->bool
  {
    // Conditionally ACQUIRE interface_state_mutex_.
    if(!interface_mutex_held)
    {
      try
      {
        interface_state_lock.lock();
      }
      catch(...)
      {
        std::terminate();
      }
      if(!InterfaceStateCheckForWritingUponMutexAcquisition())
        return false;
    }
    // interface_state_mutex held.
    try
    {
      completed_ = true;
      was_aborted_ = true;
      interface_ptr_->RemoveRequest(request_identifier_);
      if(insert || close_connection_)
        interface_ptr_->application_closure_request_set_.insert(
          request_identifier_.descriptor());
      return true;
    }
    catch(...)
    {
      interface_ptr_->bad_interface_state_detected_ = true;
      throw;
    }
  };
  
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

  while(working_number_to_write > 0) // Start write loop.
  {
    // Conditionally ACQUIRE interface_state_mutex_.
    // If interface_state_mutex_ is acquired, is is possible that the interface
    // was destroyed or that the connection was closed.
    if(!interface_mutex_held && !write_lock.owns_lock())
    {
      interface_state_lock.lock();
      if(!InterfaceStateCheckForWritingUponMutexAcquisition())
        return false;
    }

    // Conditionally ACQUIRE *write_mutex_ptr_.
    if(!write_lock.owns_lock())
    {
      write_lock.lock();
      if(*bad_connection_state_ptr_)
      {
        // application_closure_request_set_ does not need to be updated. An
        // appropriate update was performed by the entity which set
        // *bad_connection_state_ptr_ from false to true.
        completed_ = true;
        was_aborted_ = true;
        interface_ptr_->RemoveRequest(request_identifier_);
        throw std::runtime_error {"A connection from the "
          "interface to the client was found which had a corrupted state."};
      }
    }
    // *write_mutex_ptr_ is held.

    // Conditionally RELEASE interface_state_mutex_ to free the interface
    // before the write. The mutex will still be held by the caller if
    // interface_mutex_held == true.
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
    else // The number written was less than number_to_write. We must check 
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
              // If some data was written and a throw will occur, the
              // connection must be closed. This is becasue, if the write mutex
              // was immediately acquired by another request and data was 
              // written, that data would be corrupt as a partial record was
              // written here. However, indicating that the connection should
              // be closed requires an update that must be done under the
              // protection of interface_state_mutex_. But, this mutex cannot 
              // be acquired if the write mutex is held. And, the write mutex
              // cannot be released without indicating some error as another
              // thread may hold interface_state_mutex_ to acquire the write
              // mutex. The solution is to set the flag 
              // *bad_connection_state_ptr_ of the write mutex before releasing
              // the write mutex.

              // Conditionally RELEASE *write_mutex_ptr_.
              // write_lock.owns_lock() is equivalent to a partial write and,
              // in this case, connection corruption.
              if(write_lock.owns_lock())
              {
                *bad_connection_state_ptr_ = true;
                write_lock.unlock();
                // May ACQUIRE interface_state_mutex_.
                if(!TryToAddToApplicationClosureRequestSet(true))
                  return false;
              }
              std::error_code ec {errno, std::system_category()};
              throw std::system_error {ec, "select"};
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
        // If close_connection_ == true, try to add to
        // application_closure_request_set_.
        TryToAddToApplicationClosureRequestSet(false);
        return false;
      } 
      // An unrecoverable error was encountered during the write.
      // *write_mutex_ptr_ is held.
      else
      {
        // The same situation applies here as above. Writing some data and
        // exiting corrupts the connection.

        // Conditionally RELEASE *write_mutex_ptr_.
        if(std::get<2>(write_return) < number_to_write) // Some but not all.
        {
          *bad_connection_state_ptr_ = true;
          // *write_mutex_ptr_ MUST NOT be held to prevent potential deadlock.
          // RELEASE *write_mutex_ptr_.
          write_lock.unlock();
          // May ACQUIRE interface_state_mutex_.
          if(!TryToAddToApplicationClosureRequestSet(true))
            return false;
        }
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "write from a call to "
          "socket_functions::SocketWrite"};
      }
    } // End handling incomplete writes. Loop.
  } // Exit write loop.
  return true;
}

} // namespace fcgi_si
