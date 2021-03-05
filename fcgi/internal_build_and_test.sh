# This script is used by fcgi/build_and_test.sh. It is executed by the
# as_components/build_and_test container as part of the build and test actions
# for namespace fcgi.
#
# Preconditions:
# 1) The container has a Docker volume mounted at /usr/local/src/bazel_files.
#    This volume is used as the ouputUserRoot for the build actions of
#    Bazel as determined by the .bazelrc of as_components.
# 2) The container has the source directory of as_components mounted at
#    /usr/local/src/as_components.
# 3) The following directories exist in as_components:
#    a) output_binaries_and_tests
#    b) output_binaries_and_tests/application_libraries
#    c) output_binaries_and_tests/tests
#
# Arguments: none
#
# Actions:
# 1) Bazel is instructed to build all of the targets of as_components.
# 2) If the build succeeded, then:
#    a) Any updated application libraries are copied to:
#       /usr/local/src/as_components/ouput_binaries_and_tests/application_libraries
#       This implies that a modifcation may be made to the mounted
#       as_components directory.
#    b) Any updated binaries for the external tests are copied to:
#       /usr/local/src/as_components/ouput_binaries_and_tests/tests/fcgi/test
#    c) All of the tests which are registered with Bazel for as_components
#       are executed.
#
# Return status:
# 1) The script returns a non-zero status if the build failed or if an internal
#    operation could not be performed. Otherwise the script returns zero.
# 2) Test failure does not influence the return status.

cd /usr/local/src/as_components &&
if [[ !(-d output_binaries_and_tests/application_libraries/fcgi) ]]; then
    mkdir output_binaries_and_tests/application_libraries{/fcgi,/fcgi/test,/socket_functions}
fi &&
if [[ !(-d output_binaries_and_tests/tests/fcgi) ]]; then
    mkdir output_binaries_and_tests/tests{/fcgi,/fcgi/test}
fi &&
bazel build ... &&
cp --update \
    bazel-bin/fcgi/{libfcgi_server_interface_combined.so,libfcgi_utilities.so} \
    output_binaries_and_tests/application_libraries/fcgi/ &&
cp --update \
    bazel-bin/fcgi/test/libtest_fcgi_client_interface.so \
    output_binaries_and_tests/application_libraries/fcgi/test &&
cp --update \
    bazel-bin/socket_functions/libsocket_functions.so \
    output_binaries_and_tests/application_libraries/socket_functions &&
cp --update \
    bazel-bin/fcgi/test/fcgi_shared_library_installation_test \
    fcgi/test/external_test_runner.sh \
    output_binaries_and_tests/tests/fcgi/test &&
{
    bazel test --test_env=NO_IPV6= ...
    exit 0;
}

