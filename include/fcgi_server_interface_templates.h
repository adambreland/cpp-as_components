#ifndef FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_TEMPLATES_H_
#define FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_TEMPLATES_H_

#include <type_traits>

namespace fcgi_si {

// Implementation notes:
// This function is a specialized instance of a general iteration algorithm
// over two sorted lists. It processes both lists together and performs the
// same conceptual action for an item regardless of which lists the item
// occurs in. It this sense, it is equivalent to performing a set union on the
// two lists and then iterating over the union.
// 
// This function also clears both lists during the single processing pass.
template <typename C>
void FCGIServerInterface::
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

} // namspace fcgi_si

#endif // FCGI_SERVER_INTERFACE_INCLUDE_FCGI_SERVER_INTERFACE_TEMPLATES_H_