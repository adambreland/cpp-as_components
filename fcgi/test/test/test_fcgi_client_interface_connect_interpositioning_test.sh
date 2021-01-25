#! /bin/bash

# MIT License
#
# Copyright (c) 2021 Adam J. Breland
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

echo ${0}
echo -e \
"This script invokes a test which examines the behavior of \
TestFcgiClientInterface::Connect when the connect system call initially \
returns -1 with errno == EINTR. See the primary test documentation for a \
description of the other test cases for TestFcgiClientInterface::Connect.\n\n\
The implicit dependency of the test, //test/test:connect_interpositioning, \
is built. If the build was successful, then the test is built. If each build \
was successful, then the test is executed.\n\n\
This script is used to allow a test version of the connect function to \
interpose on the connect system call. The variables of \
//test/test:shared_interpositioning_state are used by the test \
and the interposing connect function.\n\n\
Starting build and test procedure.\n"

bazel build //test/test:connect_interpositioning      && \
bazel build //test/test:connect_interpositioning_test && \
LD_PRELOAD=${PWD}/bazel-bin/test/test/libconnect_interpositioning.so \
  ./bazel-bin/test/test/connect_interpositioning_test

echo -e "\nExiting test script."
