#! /bin/bash
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
