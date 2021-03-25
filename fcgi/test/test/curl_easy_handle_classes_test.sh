#! /bin/bash

nginx -g "daemon off;" &
# The Bazel test environment provides the runfiles root directory as the value
# of the environment variable TEST_SRCDIR. The path to a file in the runfiles
# directory is currently given as __main__/<path> where <path> is the path
# to the file or generated target relative to the workspace root.
${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_internal_test
test_status=$?
nginx -s quit
exit $test_status
