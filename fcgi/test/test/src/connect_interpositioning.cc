#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "test/test/include/shared_interpositioning_state.h"

namespace {
  extern "C" using ConnectType = int(int, const struct sockaddr*, socklen_t);
  void* connect_void_address {dlsym(RTLD_NEXT, "connect")};
  ConnectType* connect_address
    {reinterpret_cast<ConnectType*>(connect_void_address)};
}

extern "C" int connect(int sockfd, const struct sockaddr* addr,
  socklen_t addrlen)
{
  if(!connect_address)
  {
    std::cout << "The address of connect could not be resolved during "
      "program initialization.\n";
    exit(EXIT_FAILURE);
  }

  if(a_component::fcgi::test::test::shared_connect_EINTR_return_flag)
  {
    a_component::fcgi::test::test::shared_connect_EINTR_return_flag = false;
    std::cout << "Returning -1 and setting errno to EINTR from\n" <<
      __FILE__ << '\n';
    errno = EINTR;
    return -1;
  }

  return (*connect_address)(sockfd, addr, addrlen);
}
