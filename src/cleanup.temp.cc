// 1) Iterate over application_closure_request_set_ and 
//    connection_found_closed_set_ together.
// 2) Each distinct descriptor causes the closure procedure to be
//    executed.

#include <system_error>




template <typename C>
void OrderedSetPairProcessAndEmpty(C* first_ptr, C::iterator first_iter,
C::iterator first_end_iter, C* second_ptr, C::iterator second_iter,
C::iterator second_end_iter)
{
  while(first_iter != first_end_iter || second_iter != second_end_iter)
  {
    if(first_iter == first_end_iter)
    {
      while(second_iter != second_end_iter)
      {
        RemoveConnection(*second_iter);
        C::iterator second_copy_for_erasure_iter {second_iter};
        ++second_iter;
        second_ptr->erase(second_copy_for_erasure);
      }
    }
    else if(second_iter == second_end_iter)
    {
      while(first_iter != first_end_iter)
      {
        RemoveConnection(*first_iter);
        C::iterator first_copy_for_erasure_iter  {first_iter};
        ++first_iter;
        first_ptr->erase(first_copy_for_erasure);
      }
    }
    else
    {
      if(*first_iter == *second_iter)
      {
        RemoveConnection(*first_iter);
        C::iterator first_copy_for_erasure_iter  {first_iter};
        C::iterator second_copy_for_erasure_iter {second_iter};
        ++first_iter;
        ++second_iter;
        first_ptr->erase(first_copy_for_erasure);
        second_ptr->erase(second_copy_for_erasure);
      }
      else if(*first_iter < *second_iter)
      {
        RemoveConnection(*first_iter);
        C::iterator first_copy_for_erasure_iter  {first_iter};
        ++first_iter;
        first_ptr->erase(first_copy_for_erasure);
      }
      else // *first_iter > *second_iter
      {
        RemoveConnection(*second_iter);
        C::iterator second_copy_for_erasure_iter {second_iter};
        ++second_iter;
        second_ptr->erase(second_copy_for_erasure);
      }
    }
  }
}

void RemoveConnection(int connection)
{
  // Care must be taken to prevent descriptor leaks or double closures.
  try
  {
    bool assigned_requests {RequestCleanupDuringConnectionClosure(connection)};
    if(assigned_requests)
    {
      // Go through the process to make the descriptor a dummy.
      // Implicitly and atomically call close(connection).
      //
      // TODO Should a way to check for errors on the implicit closure of
      // connection be implemented?
      int dup2_return {};
      while((dup2_return = dup2(FCGI_LISTENSOCK_FILENO, connection)) == -1)
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
      try
      {
        record_status_map_.erase(connection);
        write_mutex_map_.erase(connection);
      }
      catch(...)
      {
        std::terminate();
      }
    }
    else
    {
      // Order as given. If erasure is not ordered before the call of
      // close(connection), it is possible that erasure does not occur and
      // close(connection) will be called twice.
      try
      {
        record_status_map_.erase(connection);
        write_mutex_map_.erase(connection);
      }
      catch(...)
      {
        std::terminate();
      }
      int close_return {close(connection)};
      if(close_return == -1 && errno != EINTR)
      {
        std::error_code ec {errno, std::system_category()};
        throw std::system_error {ec, "close"};
      }
    }
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
}

bool RequestCleanupDuringConnectionClosure(int connection)
{
  try
  {
    bool assigned_requests_present {false};
    for(auto request_map_iter =
          request_map_.lower_bound(RequestIdentifier {connection, 0});
        !(request_map_iter == request_map_.end()
          || request_map_iter->first.descriptor() > connection);
        /*no-op*/)
    {
      if(request_map_iter->second.get_status() ==
        fcgi_si::RequestStatus::kRequestAssigned)
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




AcceptRequests()
{
  // CLEANUP
  {
    // ACQUIRE interface_state_mutex_.
    std::lock_guard<std::mutex> interface_state_lock 
      {FCGIServerInterface::interface_state_mutex_};

    // Remove dummy descriptors if possible.
    //
    // Exception safety: 
    // Removal of a descriptor from dummy_descriptor_set_ and calling close on
    // that descriptor must be transactional. If performance of these actions was 
    // not a transactional step, the following scenario is possible:  
    // 1) The descriptor is released for use but not removed from  
    //    dummy_descriptor_set_.
    // 2) The descriptor is allocated for use by the application.
    // 3) When the destructor of the interface executes, the descriptor is 
    //    spuriously closed as the descriptor remained in dummy_descriptor_set_.
    for(auto dds_iter {dummy_descriptor_set_.begin()};
        dds_iter != dummy_descriptor_set_.end(); /*no-op*/)
    {
      bool request_present {false};
      std::map<RequestIdentifier, RequestData>::iterator request_map_iter
        {request_map_.lower_bound(RequestIdentifier {*dds_iter, 0})};
      // The absence of requests allows closure of the descriptor.
      // Remember that RequestIdentifier is lexically ordered.
      if(request_map_iter == request_map_.end() 
          || request_map_iter->first.descriptor() > *dds_iter)
      {
        try
        {
          int connection_to_be_closed {*dds_iter};
          std::set<int>::iterator safe_erasure_iterator {dds_iter};
          ++dds_iter
          // Erase first to prevent closure without removal from
          // dummy_descriptor_set_.
          dummy_descriptor_set_.erase(safe_erasure_iterator);
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

    // Close connection descriptors for connections which were found to be 
    // closed and for which closure was requested by FCGIRequest objects.
    // Update interface state to allow FCGIRequest objects to inspect for
    // connection closure. 
    //
    // Note that dummy_descriptor_set_ is disjoint from the union of
    // connection_found_closed_set_ and application_closure_request_set_.
    OrderedSetPairProcessAndEmpty(&connections_found_closed_set_,
      connections_found_closed_set_.begin(), connections_found_closed_set_.end(),
      &application_closure_request_set_, application_closure_request_set_.begin(),
      application_closure_request_set_.end());
  } // RELEASE interface_state_mutex_;
  
  // ... 

} 