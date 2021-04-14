#! /bin/bash

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
