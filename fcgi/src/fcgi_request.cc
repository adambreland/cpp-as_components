#include "include/fcgi_request.h"

#include <sys/select.h>
#include <sys/time.h>           // For portable use of select.
#include <sys/types.h>          // For ssize_t and portable use of select.
#include <sys/uio.h>
#include <unistd.h>

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

#include "include/protocol_constants.h"
#include "include/request_identifier.h"
#include "include/utility.h"

// Class implementation notes:
// 1) Updating interface state:
//    a) Removing requests from the collection of requests tracked by the
//       interface:
//          Requests are responsible for removing themselves from their
//       interface. The interface will not remove an item from request_map_ if
//       the associated request has been assigned to the application.
//       ("Assignment" and FcgiRequest object construction are equivalent.)
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
//          FcgiServerInterface object is used to indicate that the connection
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
//       interface has been destroyed or has already been put into a bad state,
//       then the bad state flag need not be set. In cases where the interface
//       update is adding the connection to application_closure_request_set_,
//       the update is not actually needed if the connection was closed by the
//       interface. As such, the interface need not be put into a bad state in
//       this case.
//    e) Informing the interface while it is blocked waiting for incoming data
//       and connections that an interface state change occurred.
//       1) The interface has a self-pipe that it monitors for read readiness.
//          Writes to this pipe are performed by request objects to inform the
//          interface of two state changes:
//          a) Corruption of a connection.
//          b) The transition of the interface from a good to a bad state
//             because of the action of a request.
//          This mechanism is used to prevent the interface from blocking when
//          local work is present or when blocking doesn't make sense because
//          the interface was corrupted.
//       2) Writes to the self-pipe will be associated with adding a descriptor
//          to application_closure_request_set_ (a connection was corrupted) or
//          setting bad_interface_state_detected_. The write must occur within
//          the same period of mutex ownership that is used to perform these
//          actions. In other words, the changes in shared state caused by
//          these actions must appear to the interface to be atomic. Incorrect
//          behavior from race conditions may occur otherwise.
//       3)    A write mutex cannot be held by a request once the connection
//          associated with the write mutex has been "atomically" added to
//          application_closure_request_set_ from the perspective of entities
//          which obey the appropriate inferface mutex acquisition and release
//          rules for shared interface state.
//             To ensure this, when a request intends to add a connection to
//          application_closure_request_set_ for any reason, it must acquire
//          the write mutex associated with the connection after acquisition of
//          the interface mutex before modifying the closure set. This means
//          that the write mutex may need to be released, the interface mutex
//          acquired, and then the write mutex reacquired before a modification
//          of the closure set can occur. This is because the pattern "has
//          write mutex, wanted interface mutex" is forbidden. 
//    f) Terminating the program:
//       1) Obligatory termination: (as invariants cannot be maintained)
//          a) If the interface cannot be put into a bad state, regardless of
//             whether the desire to put the interface into a bad state was
//             direct or the result of another error, the program must be 
//             terminated.
//          b)    If the interface cannot be informed that a critical state
//             change has occurred through a write to 
//             interface_pipe_write_descriptor_, then the program must be
//             terminated. Only a single "critical state change" is currently
//             known: the corruption of a connection. 
//                In this case, if the interface is blocked waiting for
//             incoming data or connections and the client does not have a
//             response time-out, then failure to be able to wake the interface
//             about the connection corruption or an interface bad state
//             transition may cause the interface and the client to wait for
//             an indeterminate amount of time even though the connection
//             should be closed by the interface.
//       2) Voluntary termination:
//          a) Corruption of the mechanism to inform the interface of state
//             changes while it is blocked waiting for incoming data and
//             connections is viewed as a serious error. It can lead to
//             indeterminate wait times even though the interface may be in
//             a bad state or may have connections to close. Termination is
//             performed in these cases.  
//
// 2) Discipline for mutex acquisition and release:
//    a) Immediately after acquisition of interface_state_mutex_, a request
//       must check if:
//       1) Its interface has been destroyed. This is done by comparing the 
//          value of FcgiServerInterface::interface_identifier_ to the value of
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
//       1) FcgiRequest objects are separate from their associated
//          FcgiServerInterface object yet need to access state which belongs to
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
//    a) Only shared data members may be accessed by FcgiRequest. These data
//       members must be accessed under mutex protection.
//    b) The FcgiServerInterface data member write_mutex_map_ must not be
//       accessed directly. A write mutex must only be accessed through an
//       FcgiRequest object's write_mutex_ptr_. In other words, the mutexes are
//       shared, but the map which stores them is not. FcgiServerInterface may
//       treat the map as a non-shared data member which locates shared objects.
//    c) Of the methods of FcgiServerInterface, only RemoveRequest may be
//       called. It must be called under mutex protection.
//
// 4) General implementation notes:
//    a) The destructor of an FcgiRequest object acquires and releases
//       FcgiServerInterface::interface_state_mutex_. This is not problematic
//       when requests are destroyed within the scope of user code. It will
//       lead to deadlock in implementation code if the destructor is executed
//       in a scope which owns the interface mutex.
//
// 5) Discipline brief summary:
//    a) Updating completed_ and was_aborted_ of an FcgiRequest object.
//    b) Removing a request from the interface.
//    c) Adding a connection to application_closure_request_set_.
//    d) Marking a connection as corrupted.
//    e) Writing to the interface self-pipe (i.e. waking the interface if it
//       is asleep).
//    f) Marking the interface as corrupted.
//    g) Obeying mutex acquisition and release rules.
//    h) Not accessing private interface state or methods even though
//       a pointer to the interface is available and FcgiRequest is a friend.
//    i) Terminating the program when invariants cannot be maintained.
namespace fcgi_si {


FcgiRequest::FcgiRequest()
: associated_interface_id_         {0U},
  interface_ptr_                   {nullptr},
  request_identifier_              {RequestIdentifier {}},
  request_data_ptr_                {nullptr},
  write_mutex_ptr_                 {nullptr},
  bad_connection_state_ptr_        {nullptr},
  interface_pipe_write_descriptor_ {-1},
  environment_map_                 {},
  request_stdin_content_           {},
  request_data_content_            {},
  role_                            {0U},
  close_connection_                {false},
  was_aborted_                     {false},
  completed_                       {false}
{}

// Implementation notes:
// This constructor should only be called by an FcgiServerInterface object.
//
// Synchronization:
// 1) It is assumed that interface_state_mutex_ is held prior to a call.
FcgiRequest::FcgiRequest(
  RequestIdentifier request_id,
  unsigned long interface_id,
  FcgiServerInterface* interface_ptr,
  FcgiServerInterface::RequestData* request_data_ptr,
  std::mutex* write_mutex_ptr,
  bool* bad_connection_state_ptr,
  int write_fd)
  : associated_interface_id_         {interface_id},
    interface_ptr_                   {interface_ptr},
    request_identifier_              {request_id},
    request_data_ptr_                {request_data_ptr},
    write_mutex_ptr_                 {write_mutex_ptr},
    bad_connection_state_ptr_        {bad_connection_state_ptr},
    interface_pipe_write_descriptor_ {write_fd},
    environment_map_                 {},
    request_stdin_content_           {},
    request_data_content_            {},
    role_                            {request_data_ptr->role_},
    close_connection_                {request_data_ptr->close_connection_},
    was_aborted_                     {false},
    completed_                       {false}
{
  if((interface_ptr == nullptr || request_data_ptr == nullptr
     || write_mutex_ptr == nullptr || bad_connection_state_ptr == nullptr)
     || (request_data_ptr->request_status_ ==
         FcgiServerInterface::RequestStatus::kRequestAssigned))
  {
    auto NullPointerCheck = [](void* ptr)->const char*
    {
      return (ptr == nullptr) ? "null\n" : "non-null\n";
    };
    interface_ptr_->bad_interface_state_detected_ = true;
    std::string error_status {"An FcgiRequest could not be "
      "constructed.\n"};
    (error_status += "interface_ptr: ")    +=
      NullPointerCheck(interface_ptr);
    (error_status += "request_data_ptr: ") +=
      NullPointerCheck(request_data_ptr);
    (error_status += "write_mutex_ptr: ")  +=
      NullPointerCheck(write_mutex_ptr);
    (error_status += "bad_connection_state_ptr: ") +=
      NullPointerCheck(bad_connection_state_ptr);
    error_status  += "RequestStatus: ";
    if(request_data_ptr == nullptr)
    {
      error_status += "unknown\n";
    }
    else
    {
      error_status +=
        (request_data_ptr->request_status_ ==
         FcgiServerInterface::RequestStatus::kRequestAssigned)
        ? "assigned\n" : "unassigned\n";
    }
    throw std::logic_error {error_status};
  }

  // TODO double check noexcept specifications for move assignments.
  // It is currently assumed that move assignments are noexcept.
  // This assumption also applies to the move constructor and move assignment
  // operator.
  environment_map_       = std::move(request_data_ptr->environment_map_);
  request_stdin_content_ = std::move(request_data_ptr->FCGI_STDIN_);
  request_data_content_  = std::move(request_data_ptr->FCGI_DATA_);
  
  // Update the status of the RequestData object to reflect its use in the
  // construction of an FcgiRequest which will be exposed to the application.
  request_data_ptr_->request_status_ =
    FcgiServerInterface::RequestStatus::kRequestAssigned;
}

FcgiRequest::FcgiRequest(FcgiRequest&& request) noexcept
: associated_interface_id_         {request.associated_interface_id_},
  interface_ptr_                   {request.interface_ptr_},
  request_identifier_              {request.request_identifier_},
  request_data_ptr_                {request.request_data_ptr_},
  write_mutex_ptr_                 {request.write_mutex_ptr_},
  bad_connection_state_ptr_        {request.bad_connection_state_ptr_},
  interface_pipe_write_descriptor_ {request.interface_pipe_write_descriptor_},
  environment_map_                 {std::move(request.environment_map_)},
  request_stdin_content_           {std::move(request.request_stdin_content_)},
  request_data_content_            {std::move(request.request_data_content_)},
  role_                            {request.role_},
  close_connection_                {request.close_connection_},
  was_aborted_                     {request.was_aborted_},
  completed_                       {request.completed_}
{
  request.associated_interface_id_ = 0U;
  request.interface_ptr_ = nullptr;
  request.request_identifier_ = RequestIdentifier {};
  request.request_data_ptr_ = nullptr;
  request.write_mutex_ptr_ = nullptr;
  request.bad_connection_state_ptr_ = nullptr;
  request.interface_pipe_write_descriptor_ = -1;
  request.environment_map_.clear();
  request.request_stdin_content_.clear();
  request.request_data_content_.clear();
  request.role_ = 0U;
  request.close_connection_ = false;
  request.was_aborted_ = false;
  request.completed_ = false;
}

FcgiRequest& FcgiRequest::operator=(FcgiRequest&& request)
{
  if(this != &request)
  {
    if(!(completed_ || associated_interface_id_ == 0))
      throw std::logic_error {"Move assignment would have occurred on an "
        "FcgiRequest object which was not in a valid state to be moved to."};

    associated_interface_id_ = request.associated_interface_id_;
    interface_ptr_ = request.interface_ptr_;
    request_identifier_ = request.request_identifier_;
    request_data_ptr_ = request.request_data_ptr_;
    write_mutex_ptr_ = request.write_mutex_ptr_;
    bad_connection_state_ptr_ = request.bad_connection_state_ptr_;
    interface_pipe_write_descriptor_ = request.interface_pipe_write_descriptor_;
    environment_map_ = std::move(request.environment_map_);
    request_stdin_content_ = std::move(request.request_stdin_content_);
    request_data_content_ = std::move(request.request_data_content_);
    role_ = request.role_;
    close_connection_ = request.close_connection_;
    was_aborted_ = request.was_aborted_;
    completed_ = request.completed_;

    request.associated_interface_id_ = 0U;
    request.interface_ptr_ = nullptr;
    request.request_identifier_ = RequestIdentifier {};
    request.request_data_ptr_ = nullptr;
    request.write_mutex_ptr_ = nullptr;
    request.bad_connection_state_ptr_ = nullptr;
    request.interface_pipe_write_descriptor_ = -1;
    request.environment_map_.clear();
    request.request_stdin_content_.clear();
    request.request_data_content_.clear();
    request.role_ = 0U;
    request.close_connection_ = false;
    request.was_aborted_ = false;
    request.completed_ = false;
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
FcgiRequest::~FcgiRequest()
{
  if(!(completed_ || (associated_interface_id_ == 0U)))
  {
    // ACQUIRE interface_state_mutex_.
    std::unique_lock<std::mutex> interface_state_lock
      {FcgiServerInterface::interface_state_mutex_, std::defer_lock};
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
    if((FcgiServerInterface::interface_identifier_ == associated_interface_id_)
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
          // ACQUIRE the write mutex. (Once acquired, no request thread will
          // acquire the write mutex after the current thread releases the 
          // interface mutex.)
          std::lock_guard<std::mutex> {*write_mutex_ptr_};
          interface_ptr_->application_closure_request_set_.insert(
            request_identifier_.descriptor());
          InterfacePipeWrite();
        } // RELEASE the write mutex.
        interface_ptr_->RemoveRequest(request_identifier_);
      }
      catch(...) 
      {
        interface_ptr_->bad_interface_state_detected_ = true;
        try
        {
          InterfacePipeWrite();
        }
        catch(...)
        {
          std::terminate();
        }
      }
    }
  } // RELEASE interface_state_mutex_.
}

// Implementation notes:
// Synchronization:
// 1) Acquires interface_state_mutex_.
bool FcgiRequest::AbortStatus()
{
  if(completed_ || was_aborted_ || associated_interface_id_ == 0U)
    return was_aborted_;
  
  // The actual abort status is unknown if this point is reached.
  // ACQUIRE interface_state_mutex_ to determine current abort status.
  std::lock_guard<std::mutex> interface_state_lock
    {FcgiServerInterface::interface_state_mutex_};
  // Check if the interface has been destroyed.
  if(FcgiServerInterface::interface_identifier_ != associated_interface_id_)
  {
    completed_ = true;
    was_aborted_ = true;
    throw std::runtime_error {"The FcgiServerInterface associated "
      "with an FcgiRequest object was destroyed before the request."};
  }
  // Check if the interface is in a bad state.
  if(interface_ptr_->bad_interface_state_detected_)
  {
    completed_ = true;
    was_aborted_ = true;
    throw std::runtime_error {"The FcgiServerInterface associated "
      "with an FcgiRequest object was in a bad state."};
  }
  // Check if the connection has been closed by the interface.
  if(request_data_ptr_->connection_closed_by_interface_)
  {
    completed_   = true;
    was_aborted_ = true;
    // RemoveRequest implicitly sets bad_interface_state_detected_ if it
    // throws.
    try 
    {
      interface_ptr_->RemoveRequest(request_identifier_);
    }
    catch(...) 
    {
      try 
      {
        InterfacePipeWrite();
      }
      catch(...) 
      {
        std::terminate();
      }
      throw;
    }
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
//    If interface_state_mutex_ is not held for the duration of the write, 
// it is possible that a race condition may occur. There are two steps to
// consider:
// 1) Removing the request from the interface.
// 2) Notifying the client that the request is complete.
//
//    According to the mutex acquisition discipline, a write mutex can only
// be acquired when the interface mutex is held. Suppose that the request is
// removed from the interface and, as for other writes to the client, the
// interface mutex is released before the write starts. Then suppose that
// the client erroneously re-uses the request id of the request. The
// interface will accept a begin request record with this request id. A
// request could then be produced by the interface. The presence of two
// request objects which are associated with the same connection and which
// share a request identifier could cause several errors.
//   In this scenario, an error on the part of the client can corrupt
// interface state. Holding the interface mutex during the write prevents the
// interface from spuriously validating an erroneous begin request record.
bool FcgiRequest::EndRequestHelper(std::int32_t app_status, 
  std::uint8_t protocol_status)
{
  if(completed_ || associated_interface_id_ == 0U)
    return false;

  constexpr int seq_num {4}; // Three headers and an 8-byte body. 3+1=4
  constexpr int app_status_byte_length {32 / 8};
  uint8_t header_and_end_content[seq_num][FCGI_HEADER_LEN];

  PopulateHeader(&header_and_end_content[0][0],
    FcgiType::kFCGI_STDOUT,
    request_identifier_.Fcgi_id(), 0, 0);
  PopulateHeader(&header_and_end_content[1][0],
    FcgiType::kFCGI_STDERR,
    request_identifier_.Fcgi_id(), 0, 0);
  PopulateHeader(&header_and_end_content[2][0],
    FcgiType::kFCGI_END_REQUEST,
    request_identifier_.Fcgi_id(), FCGI_HEADER_LEN, 0);

  // Fill end request content.
  for(int i {0}; i < app_status_byte_length ; ++i)
    header_and_end_content[3][i] = static_cast<uint8_t>(
      app_status >> (24 - (8*i)));
  header_and_end_content[3][app_status_byte_length] = 
    protocol_status;
  for(int i {app_status_byte_length + 1}; i < FCGI_HEADER_LEN; ++i)
    header_and_end_content[3][i] = 0;

  // Fill iovec structure for a call to ScatterGatherWriteHelper.
  constexpr std::size_t number_to_write {seq_num*FCGI_HEADER_LEN};
  struct iovec iovec_wrapper[1] = {{header_and_end_content, number_to_write}};

  // ACQUIRE interface_state_mutex_ to allow interface request_map_
  // update and to prevent race conditions between the client server and
  // the interface.
  std::lock_guard<std::mutex> interface_state_lock
    {FcgiServerInterface::interface_state_mutex_};
  if(!InterfaceStateCheckForWritingUponMutexAcquisition())
    return false;

  // Implicitly ACQUIRE and RELEASE *write_mutex_ptr_.
  bool write_return {ScatterGatherWriteHelper(iovec_wrapper, 1, number_to_write,
    true)};

  // Update interface state and FcgiRequest state.
  //
  // If write_return is false, ScatterGatherWriteHelper updated interface
  // state by removing the request. The descriptor does not need to be
  // conditionally added to application_closure_request_set_.
  if(write_return)
  {
    completed_ = true;
    try 
    {
      interface_ptr_->RemoveRequest(request_identifier_);
    }
    catch(...) 
    {
      try 
      {
        InterfacePipeWrite();
      }
      catch(...) 
      {
        std::terminate();
      }
      throw;
    }
    if(close_connection_)
    {
      try
      {
        // ACQUIRE the write mutex. (Once acquired, no request thread will
        // acquire the write mutex after the current thread releases the 
        // interface mutex.)
        std::lock_guard<std::mutex> {*write_mutex_ptr_};
        interface_ptr_->application_closure_request_set_.insert(
          request_identifier_.descriptor());
        InterfacePipeWrite();
      } // RELEASE the write mutex.
      catch(...)
      {
        interface_ptr_->bad_interface_state_detected_ = true;
        try
        {
          InterfacePipeWrite();
        }
        catch(...)
        {
          std::terminate();
        }
        throw;
      }
    }
  }
  return write_return;
} // RELEASE interface_state_mutex_.

// Implementation note:
// Preconditions: 
// 1) The interface associated with the request must exist.
// 2) The interface associated with the request must be in a valid state.
//
// Synchronization: 
// 1) FcgiServerInterface::interface_state_mutex_ must be held before a call.
void FcgiRequest::InterfacePipeWrite()
{
  // Inform the interface that a connection closure was requested.
  std::uint8_t pipe_buff[1] = {0};
  ssize_t write_return {};
  while(((write_return = write(interface_pipe_write_descriptor_, 
    pipe_buff, 1)) < 0) && (errno == EINTR))
    continue;
  // Failure to write indicates that something is wrong with the
  // pipe and, hence, the interface.
  if(write_return <= 0)
    throw std::logic_error {"The interface pipe could not be written to."};
}

// Implementation notes:
// Synchronization:
// 1) interface_state_mutex_ must be held prior to a call.
bool FcgiRequest::
InterfaceStateCheckForWritingUponMutexAcquisition()
{
  // Check if the interface has been destroyed.
  if(FcgiServerInterface::interface_identifier_ != associated_interface_id_)
  {
    completed_   = true;
    was_aborted_ = true;
    return false;
  }
  // Check if the interface is in a bad state.
  if(interface_ptr_->bad_interface_state_detected_)
  {
    completed_   = true;
    was_aborted_ = true;
    return false;
  }
  // Check if the interface has closed the connection.
  // Check if the connection is scheduled for closure.
  if(request_data_ptr_->connection_closed_by_interface_ ||
     (interface_ptr_->application_closure_request_set_.
        find(request_identifier_.descriptor()) != 
      interface_ptr_->application_closure_request_set_.end()))
  {
    completed_   = true;
    was_aborted_ = true;
    try 
    {
      interface_ptr_->RemoveRequest(request_identifier_);
    }
    catch(...) 
    {
      try 
      {
        InterfacePipeWrite();
      }
      catch(...) 
      {
        std::terminate();
      }
      throw;
    }
    return false;
  }
  
  return true;
}

bool FcgiRequest::
ScatterGatherWriteHelper(struct iovec* iovec_ptr, int iovec_count,
  std::size_t number_to_write, bool interface_mutex_held)
{
  std::unique_lock<std::mutex> interface_state_lock
    {FcgiServerInterface::interface_state_mutex_, std::defer_lock};

  // write_lock has the following property in the loop below:
  // The mutex is always held when writing and, once some data has been written,
  // the mutex is never released. This allows the write mutex to be released
  // while the thread sleeps in select in the case that writing blocks and
  // nothing was written.
  std::unique_lock<std::mutex> write_lock {*write_mutex_ptr_, std::defer_lock};

  // An internal helper function used when application_closure_request_set_
  // of the interface must be accessed. This occurs when:
  // 1) The connection was found to be closed by an attempt to write and 
  //    close_connection_ == true. (Addition to the set is not strictly
  //    necessary as closure would eventually be discovered by the interface.)
  // 2) The connection from the server to the client was corrupted by
  //    an incomplete write during the current call to ScatterGatherWriteHelper.
  // 3) A time-out occurred when blocked for writing. The client is regarded as
  //    dead.
  // 4) An error occurred when trying to wait for write readiness. Putting
  //    the connection in application_closure_request_set_ is a pragmatic
  //    approach to handling the error internally.
  //
  // Parameters:
  // insert: If force_insert == true, insertion is attempted.
  //         If force_insert == false, insertion is attempted if 
  //         close_connection == true.
  //
  // Preconditions:
  // 1) interface_state_mutex cannot be held locally. (That is,
  //    interface_state_mutex cannot be held if interface_mutex_held is false.)
  //
  // Synchronization:
  // 1) interface_state_mutex_ will be conditionally acquired depending on the
  //    value of interface_mutex_held.
  // 2) If interface_state_mutex_ was acquired, it was released upon normal
  //    exit.
  // 3) May acquire and release the write mutex of the request.
  //
  // Exceptions:
  // 1) May throw exceptions derived from std::exception.
  // 2) In the event of a throw:
  //    a) completed_ and was_aborted_ were set.
  //    b) The interface is in a bad state.
  // 3) Program termination may occur if interface state cannot be updated
  //    during a throw.
  // 
  // Effects:
  // 1) If false was returned, InterfaceStateCheckForWritingUponMutexAcquisition
  //    returned false.
  // 2) If true was returned:
  //    a) completed_ and was_aborted_ were set.
  //    b) The request was removed from the interface.
  //    c) Conditional connection insertion to application_closure_request_set_
  //       was successful.
  //    d) If connection insertion occurred, a write was performed on the
  //       interface self-pipe.
  auto TryToAddToApplicationClosureRequestSet = 
  [this, interface_mutex_held, &interface_state_lock, &write_lock]
  (bool force_insert)->bool
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
      // Conditionally RELEASE interface_state_mutex_.
      if(!InterfaceStateCheckForWritingUponMutexAcquisition())
      {
        interface_state_lock.unlock();
        return false;
      }
    }
    // interface_state_mutex held.
    try
    {
      completed_ = true;
      was_aborted_ = true;
      interface_ptr_->RemoveRequest(request_identifier_);
      if(force_insert || close_connection_)
      {
        // ACQUIRE the write mutex to ensure that request threads will not
        // hold the write mutex in the future.
        write_lock.lock();
        interface_ptr_->application_closure_request_set_.insert(
          request_identifier_.descriptor());
        InterfacePipeWrite();
        // RELEASE the write mutex.
        write_lock.unlock();
      }
      // Conditionally RELEASE interface_state_mutex_.
      if(interface_state_lock.owns_lock())
        interface_state_lock.unlock();
      return true;
    }
    catch(...)
    {
      interface_ptr_->bad_interface_state_detected_ = true;
      try
      {
        InterfacePipeWrite();
      }
      catch(...)
      {
        std::terminate();
      }
      throw;
    }
  };
  
  std::size_t working_number_to_write {number_to_write};
  int fd {request_identifier_.descriptor()};
  while(working_number_to_write > 0) // Start write loop.
  {
    // Conditionally ACQUIRE interface_state_mutex_.
    // If interface_state_mutex_ is acquired, is is possible that the interface
    // was destroyed or that the connection was closed.
    if(!interface_mutex_held && !write_lock.owns_lock())
    {
      // Note that the write mutex is not released once some data has been
      // written. As such, a throw from lock() does not risk corrupting
      // the connection.
      // This is a case where a throw may occur but FcgiRequest object state
      // is not updated.
      interface_state_lock.lock();
      if(!InterfaceStateCheckForWritingUponMutexAcquisition())
        return false;
    }

    // Conditionally ACQUIRE *write_mutex_ptr_.
    if(!write_lock.owns_lock())
    {
      // As above, no data will have been written to the connection. A throw
      // from lock() does not risk connection corruption.
      // This is a case where a throw may occur but FcgiRequest object state
      // is not updated.
      write_lock.lock();
      if(*bad_connection_state_ptr_)
      {
        // application_closure_request_set_ does not need to be updated. An
        // appropriate update was performed by the entity which set
        // *bad_connection_state_ptr_ from false to true.
        completed_ = true;
        was_aborted_ = true;
        interface_ptr_->RemoveRequest(request_identifier_);
        return false;
      }
    }
    // *write_mutex_ptr_ is held.

    // Conditionally RELEASE interface_state_mutex_ to free the interface
    // before the write. The mutex will still be held by the caller if
    // interface_mutex_held == true.
    if(interface_state_lock.owns_lock())
      interface_state_lock.unlock();

    std::tuple<struct iovec*, int, std::size_t> write_return
      {a_component::socket_functions::ScatterGatherSocketWrite(fd, iovec_ptr,
        iovec_count, working_number_to_write)};
    // Start return processing if-else-if ladder.
    if(std::get<2>(write_return) == 0) // All data was written.
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
        int select_descriptor_range {fd + 1};
        fd_set write_set {};
        struct timeval timeout {};
        while(true) // Start select loop.
        {
          // The loop exits only when writing won't block or an error occurs.
          FD_ZERO(&write_set);
          FD_SET(fd, &write_set);
          timeout = {FcgiServerInterface::write_block_timeout, 0};
          int select_return {};
          if((select_return = select(select_descriptor_range, nullptr, 
            &write_set, nullptr, &timeout)) <= 0)
          {
            if((select_return == 0) || (errno != EINTR))
            {
              // If some data was written and a throw will occur, the
              // connection must be closed. This is becasue, if the write mutex
              // was immediately acquired by another request and data was 
              // written, that data would be corrupt as a partial record was
              // written here. However, indicating that the connection should
              // be closed requires an update that must be done under the
              // protection of interface_state_mutex_. Yet, this mutex cannot 
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
              }

              // May ACQUIRE interface_state_mutex_.
              // Connection closure is attempted even if nothing was written
              // and select had an error other than EINTR. This is done as
              // the error can likely not be solved and will likely affect
              // other writes to the connection. Also, the fact that blocking
              // occurred at all on the connection is suspicious.
              TryToAddToApplicationClosureRequestSet(true);

              if(select_return != 0) {
                std::error_code ec {errno, std::system_category()};
                throw std::system_error {ec, "select"};
              }
              else
                return false; // A time-out is not exceptional.
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
        if(std::get<2>(write_return) < number_to_write)
          *bad_connection_state_ptr_ = true;
      
        // *write_mutex_ptr_ MUST NOT be held to prevent potential deadlock.
        // RELEASE *write_mutex_ptr_.
        write_lock.unlock();
        // May ACQUIRE interface_state_mutex_.
        TryToAddToApplicationClosureRequestSet(true);

        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "write from a call to "
          "socket_functions::SocketWrite"};
      }
    } // End handling incomplete writes. Loop.
  } // Exit write loop.
  return true;
}

} // namespace fcgi_si
