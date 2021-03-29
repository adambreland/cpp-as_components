#! /bin/bash

# The Bazel test environment provides the runfiles root directory as the value
# of the environment variable TEST_SRCDIR. The path to a file in the runfiles
# directory is currently given as ${TEST_SRCDIR}/__main__/<path> where <path>
# is the path to the file or generated target relative to the workspace root.

# AND list.
mkdir -p /usr/local/src/build_and_test/nginx/{run,log,data} &&
cp --update ${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_internal_test_http_response_body.txt \
            /usr/local/src/build_and_test/nginx/data/response.txt &&
cp --update ${TEST_SRCDIR}/__main__/fcgi/test/test/nginx.conf \
            /usr/local/src/build_and_test/nginx &&
nginx -c /usr/local/src/build_and_test/nginx/nginx.conf -g "daemon off;" &

# Waits so that NGINX has time to initialize.
sleep 0.1 &&
# Executes the internal test which uses libcurl. The test will fail if the
# NGINX instance was not created or is not ready.
${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_internal_test

test_status=$?
nginx -s quit
exit $test_status
