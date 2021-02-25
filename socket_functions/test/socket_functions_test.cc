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

#include <sys/uio.h>

#include <cstdint>
#include <iostream>
#include <utility>

#include "googletest/include/gtest/gtest.h"

#include "include/socket_functions.h"

TEST(SocketFunctions, ScatterGatherHelper)
{
  // Initialize a array of struct iovec instances for testing.
  uint8_t iovec_0[7];
  uint8_t iovec_1[10];
  uint8_t iovec_2[2];
  uint8_t iovec_3[3];
  uint8_t iovec_4[10];
  struct iovec iovec_array[5];

  auto set_test_array = [&](struct iovec* iovec_ptr)->void {
    iovec_ptr->iov_base = static_cast<void*>(iovec_0);
    iovec_ptr->iov_len = 7;
    ++iovec_ptr;
    iovec_ptr->iov_base = static_cast<void*>(iovec_1);
    iovec_ptr->iov_len = 10;
    ++iovec_ptr;
    iovec_ptr->iov_base = static_cast<void*>(iovec_2);
    iovec_ptr->iov_len = 2;
    ++iovec_ptr;
    iovec_ptr->iov_base = static_cast<void*>(iovec_3);
    iovec_ptr->iov_len = 0;  // Notice that a zero length was given.
    ++iovec_ptr;
    iovec_ptr->iov_base = static_cast<void*>(iovec_4);
    iovec_ptr->iov_len = 10;
  };

  set_test_array(&(iovec_array[0]));

  // No information written (as in an initial blocking error).
  EXPECT_EQ(
    (as_components::socket_functions::ScatterGatherHelper(0, iovec_array, 5)),
    (std::pair<struct iovec*, int> {iovec_array, 5}));
  EXPECT_EQ(
    iovec_array[0].iov_base,
    static_cast<void*>(iovec_0));
  EXPECT_EQ(
    (iovec_array[0].iov_len),
    (7U));

  // Less than the first iov_len.
  set_test_array(&(iovec_array[0]));
  EXPECT_EQ(
    (as_components::socket_functions::ScatterGatherHelper(4, iovec_array, 5)),
    (std::pair<struct iovec*, int> {&iovec_array[0], 5}));
  EXPECT_EQ(
    (iovec_array[0].iov_base),
    ((void*)(((uint8_t*)(&iovec_0[0])) + 4)));
  EXPECT_EQ(
    (iovec_array[0].iov_len),
    (3U));

  // Multiple iovec instances terminating with a partial read or write.
  set_test_array(&(iovec_array[0]));
  EXPECT_EQ(
    (as_components::socket_functions::ScatterGatherHelper(18, iovec_array, 5)),
    (std::pair<struct iovec*, int> {&iovec_array[2], 3}));
  EXPECT_EQ(
    (iovec_array[2].iov_base),
    ((void*)(((uint8_t*)(&iovec_2[0])) + 1)));
  EXPECT_EQ(
    (iovec_array[2].iov_len),
    (1U));

  // Multiple iovec instances terminating with a partial read or write.
  // Include the zero length argument.
  set_test_array(&(iovec_array[0]));
  EXPECT_EQ(
    (as_components::socket_functions::ScatterGatherHelper(25, iovec_array, 5)),
    (std::pair<struct iovec*, int> {&iovec_array[4], 1}));
  EXPECT_EQ(
    (iovec_array[4].iov_base),
    ((void*)(((uint8_t*)(&iovec_4[0])) + 6)));
  EXPECT_EQ(
    (iovec_array[4].iov_len),
    (4U));

  // Multiple iovec instances terminating on a boundary.
  set_test_array(&(iovec_array[0]));
  EXPECT_EQ(
    (as_components::socket_functions::ScatterGatherHelper(17, iovec_array, 5)),
    (std::pair<struct iovec*, int> {&iovec_array[2], 3}));
  EXPECT_EQ(
    (iovec_array[2].iov_base),
    ((void*)(((uint8_t*)(&iovec_2[0])) + 0)));
  EXPECT_EQ(
    (iovec_array[2].iov_len),
    (2U));

  // Multiple iovec instances terminating on a boundary.
  // Stop at the boundary of the zero length argument and then skip ahead.
  set_test_array(&(iovec_array[0]));
  EXPECT_EQ(
    (as_components::socket_functions::ScatterGatherHelper(19, iovec_array, 5)),
    (std::pair<struct iovec*, int> {&iovec_array[4], 1}));
  EXPECT_EQ(
    (iovec_array[4].iov_base),
    ((void*)(((uint8_t*)(&iovec_4[0])) + 0)));
  EXPECT_EQ(
    (iovec_array[4].iov_len),
    (10U));
}
