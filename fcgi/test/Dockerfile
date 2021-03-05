# This Dockerfile defines the container image for the external tests for
# namespace fcgi.

FROM as_components/ubuntu_base
SHELL ["/bin/bash", "-c"]
# Installs the application shared libraries of as_components.
COPY application_libraries \
     /usr/local/lib/as_components
RUN ldconfig -X /usr/local/lib/as_components{/fcgi,/fcgi/test,/socket_functions}
# Copies the tests from the build context.
COPY tests/fcgi/test \
     /usr/local/bin/as_components/fcgi/test
