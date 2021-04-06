# MIT License
#
# Copyright (c) 2021 Adam J. Breland
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# Container image: as_components/fcgi/external_tests

# This Dockerfile defines the container image for the external tests for
# namespace fcgi.
# It is assumed that the build context is output_binaries_and_tests.

FROM as_components/ubuntu_base
SHELL ["/bin/bash", "-c"]
# Installs the application shared libraries of as_components.
COPY application_libraries \
     /usr/local/lib/as_components
RUN ldconfig -X /usr/local/lib/as_components{/fcgi,/fcgi/test,/socket_functions}
# Copies the tests from the build context.
COPY tests/fcgi/test \
     /usr/local/bin/as_components/fcgi/test
