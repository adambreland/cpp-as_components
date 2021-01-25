// MIT License
//
// Copyright (c) 2021 Adam J. Breland
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

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

  if(as_components::fcgi::test::test::shared_connect_EINTR_return_flag)
  {
    as_components::fcgi::test::test::shared_connect_EINTR_return_flag = false;
    std::cout << "Returning -1 and setting errno to EINTR from\n" <<
      __FILE__ << '\n';
    errno = EINTR;
    return -1;
  }

  return (*connect_address)(sockfd, addr, addrlen);
}
