// 1) Iterate over application_closure_request_set_ and 
//    connection_found_closed_set_ together.
// 2) Each distinct descriptor is causes the closure procedure to be
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
  record_status_map_.erase(connection);
  write_mutex_map_.erase(connection);

  bool assigned_requests {RequestCleanupDuringConnectionClosure(connection)};
  if(assigned_requests)
  {
    // Go through the process to make the descriptor a dummy.
    // Implicitly and atomically call close(connection).
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
    dummy_descriptor_set_.insert(connection);
  }
  else
  {
    int close_return {close(connection)};
    if(close_return == -1 && errno != EINTR)
    {
      std::error_code ec {errno, std::system_category()};
      throw std::system_error {ec, "close"};
    }
  }
}

bool RequestCleanupDuringConnectionClosure(int connection)
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