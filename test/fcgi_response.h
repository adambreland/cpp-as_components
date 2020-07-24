#ifndef FCGI_SERVER_INTERFACE_TEST_FCGI_RESPONSE_H_
#define FCGI_SERVER_INTERFACE_TEST_FCGI_RESPONSE_H_

#include <cstdint>

namespace fcgi_si_test {

class FcgiResponse
{
 public:
  virtual std::uint16_t FcgiId() = 0;

  virtual ~FcgiResponse();
};

} // fcgi_si_test

#endif // FCGI_SERVER_INTERFACE_TEST_FCGI_RESPONSE_H_
