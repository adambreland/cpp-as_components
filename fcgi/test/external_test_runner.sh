#! /bin/bash

# This script is executed by the external testing container for namespace fcgi
# (image as_components/fcgi/external_tests). It runs the external tests of
# namespace fcgi. An external test is a test which is executed outside of the
# test environment of Bazel.
#
# Arguments: none
#
# Preconditions:
# 1) It is assumed that the shared libraries which are needed for
#    FcgiServerInterface and TestFcgiClientInterface were installed in the
#    external testing container image. These libraries should be accessible
#    through the standard shared library loading mechanism.
# 2) It is assumed that the external test binaries and scripts for namespace
#    fcgi are in directory /usr/local/bin/as_components/fcgi/test.
#
# Effects:
# 1) The script has no effects on the container.
#
# Return status:
# 1) The script returns a non-zero value if a test could not be executed or if
#    any test failed. The return status is zero otherwise.

test_root=/usr/local/bin/as_components/fcgi/test
${test_root}/fcgi_shared_library_installation_test
