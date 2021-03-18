# Container image: as_components/fcgi/external_tests

# This Dockerfile defines the container image for the external tests for
# namespace fcgi.
# It is assumed that the build context is output_binaries_and_tests.

FROM as_components/fcgi/external_test_base
SHELL ["/bin/bash", "-c"]
# Installs the application shared libraries of as_components.
COPY application_libraries \
     /usr/local/lib/as_components
RUN ldconfig -X /usr/local/lib/as_components{/fcgi,/fcgi/test,/socket_functions}
# Copies the tests from the build context.
COPY tests/fcgi/test \
     /usr/local/bin/as_components/fcgi/test
