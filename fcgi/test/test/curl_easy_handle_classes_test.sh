#! /bin/bash

nginx -g "daemon off;" &
${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_internal_test
test_status=$?
nginx -s quit
exit $test_status
