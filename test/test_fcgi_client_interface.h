#ifndef FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
#define FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_

#include <map>
#include <memory>

#include "include/protocol_constants.h"
#include "include/request_identifier.h"

class FcgiResponse;
class TestClientRecordStatus;
struct RequestData;

namespace fcgi_si_test {

class TestFcgiClientInterface
{
 public:
  int CreateConnection();

  bool CloseConnection(int connection);

  fcgi_si::RequestIdentifier SubmitApplicationRequest(int connection,
    struct RequestData* data);

  bool AbortApplicationRequest(fcgi_si::RequestIdentifier id);
  
  template<typename NameIter>
  bool SubmitFcgiGetValuesRequest(NameIter start_iter, NameIter end_iter);

  template<typename ByteIter>
  bool SubmitUnknownManagementRequest(fcgi_si::FCGIType type, 
    ByteIter start_iter, ByteIter end_iter);

  std::vector<std::unique_ptr<FcgiResponse>> AcceptResponses();

 private:
  std::map<int, TestClientRecordStatus> record_status_map_;

};

} // fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_TEST_FCGI_CLIENT_INTERFACE_H_
