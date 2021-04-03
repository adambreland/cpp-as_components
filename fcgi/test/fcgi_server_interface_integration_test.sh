#! /bin/bash

# Creates the FcgiServerInterface application server and waits for it to signal
# that it is ready. LD_LIBRARY_PATH is needed here because the test executable
# is executed directly (and not by Bazel with environment variables).
LD_LIBRARY_PATH='${ORIGIN}/../../socket_functions' \
    ${TEST_SRCDIR}/__main__/fcgi/test/fcgi_server_interface_integration_test_app_server &&
mkdir -p /usr/local/src/build_and_test/nginx/fcgi_server_interface_integration_test/log &&
envsubst '${TEST_TMPDIR}' < ${TEST_SRCDIR}/__main__/fcgi/test/fcgi_server_interface_integration_test_nginx.conf.template \
         > ${TEST_TMPDIR}/fcgi_server_interface_integration_test_nginx.conf

if [[ $? -eq 0 ]]; then
    nginx -c ${TEST_TMPDIR}/fcgi_server_interface_integration_test_nginx.conf \
          -g "daemon off;" &
    if [[ $? -eq 0 ]]; then
        # Waits so that NGINX has time to initialize.
        sleep 0.1
        # Executes the internal test which uses libcurl. The test will fail if the
        # NGINX instance was not created or is not ready.
        # ${TEST_SRCDIR}/__main__/fcgi/test/fcgi_server_interface_integration_test_http_client
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
