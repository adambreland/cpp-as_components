#ifndef FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_TEMPLATES_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_TEMPLATES_H_

#include <type_traits>

template <typename C>
void fcgi_si::FCGIServerInterface::
ConnectionClosureProcessing(C* first_ptr, typename C ::iterator first_iter,
  typename C ::iterator first_end_iter, C* second_ptr, 
  typename C ::iterator second_iter, typename C ::iterator second_end_iter)
{
  static_assert(
    std::is_same<typename C ::value_type, int>::value, 
    "In a call to ConnectionClosureProcessing, value_type of the template "
    "container type C was not int."
  );

  try
  {
    while(first_iter != first_end_iter || second_iter != second_end_iter)
    {
      if(first_iter == first_end_iter)
      {
        while(second_iter != second_end_iter)
        {
          RemoveConnection(*second_iter);
          typename C ::iterator second_copy_for_erasure_iter {second_iter};
          ++second_iter;
          second_ptr->erase(second_copy_for_erasure_iter);
        }
      }
      else if(second_iter == second_end_iter)
      {
        while(first_iter != first_end_iter)
        {
          RemoveConnection(*first_iter);
          typename C ::iterator first_copy_for_erasure_iter  {first_iter};
          ++first_iter;
          first_ptr->erase(first_copy_for_erasure_iter);
        }
      }
      else
      {
        if(*first_iter == *second_iter)
        {
          RemoveConnection(*first_iter);
          typename C ::iterator first_copy_for_erasure_iter  {first_iter};
          typename C ::iterator second_copy_for_erasure_iter {second_iter};
          ++first_iter;
          ++second_iter;
          first_ptr->erase(first_copy_for_erasure_iter);
          second_ptr->erase(second_copy_for_erasure_iter);
        }
        else if(*first_iter < *second_iter)
        {
          RemoveConnection(*first_iter);
          typename C ::iterator first_copy_for_erasure_iter  {first_iter};
          ++first_iter;
          first_ptr->erase(first_copy_for_erasure_iter);
        }
        else // *first_iter > *second_iter
        {
          RemoveConnection(*second_iter);
          typename C ::iterator second_copy_for_erasure_iter {second_iter};
          ++second_iter;
          second_ptr->erase(second_copy_for_erasure_iter);
        }
      }
    }
  }
  catch(...)
  {
    bad_interface_state_detected_ = true;
    throw;
  }
}

#endif // FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_TEMPLATES_H_