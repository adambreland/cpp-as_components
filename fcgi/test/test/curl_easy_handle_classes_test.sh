#! /bin/bash

# The Bazel test environment provides the runfiles root directory as the value
# of the environment variable TEST_SRCDIR. The path to a file in the runfiles
# directory is currently given as ${TEST_SRCDIR}/__main__/<path> where <path>
# is the path to the file or generated target relative to the workspace root.

# AND list.
mkdir -p /usr/local/src/build_and_test/nginx/curl_easy_test/{log,data} &&
cp --update ${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_internal_test_http_response_body.txt \
            /usr/local/src/build_and_test/nginx/curl_easy_test/data/response.txt &&
# Uses envsubst to substitute the path to the Bazel test environment temporary
# directory in the NGINX configuration file for the location of the file which
# will hold the process ID of the NGINX master process. Note that hard quotes
# are needed for the argument to envsubst because it treats the argument as
# text which contains replacement patterns which determine which environment
# variables will be substituted in STDIN.
envsubst '${TEST_TMPDIR}' < ${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_test_nginx.conf.template \
         > ${TEST_TMPDIR}/curl_easy_handle_classes_test_nginx.conf

if [[ $? -eq 0 ]]; then
    nginx -c ${TEST_TMPDIR}/curl_easy_handle_classes_test_nginx.conf \
          -g "daemon off;" &
    if [[ $? -eq 0 ]]; then
        # Waits so that NGINX has time to initialize.
        sleep 0.1 &&
        # Executes the internal test which uses libcurl. The test will fail if the
        # NGINX instance was not created or is not ready.
        ${TEST_SRCDIR}/__main__/fcgi/test/test/curl_easy_handle_classes_internal_test
        test_status=$?
        # kill is needed because the command "nginx -s quit" assumes that the default
        # location for the process ID of the NGINX master process is used.
        kill -QUIT $(cat ${TEST_TMPDIR}/nginx.pid)
        exit $test_status
    else
        exit 1
    fi
else
    exit 1
fi
