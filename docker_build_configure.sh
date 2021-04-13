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

# This script configures Docker and the source directory so that later build
# actions can be performed. The script:
# 1) Builds the container images which are needed to build and test
#    as_components.
# 2) Creates an output directory tree for application shared libraries and
#    external test files.
#
# Note:
# 1) Run as the root user.
# 2) The container images are built without using the cache.

# AND list.
script_path=${0%/*} &&
if [[ ${script_path} == ${0} ]]; then
    script_path=${PWD}
else
    cd ${script_path} &&
    script_path=${PWD}
fi &&
cd ${script_path}/ubuntu_base &&
docker build --no-cache -t as_components/ubuntu_base . &&
cd ${script_path}/build_and_test &&
docker build --no-cache -t as_components/build_and_test . &&
cd ${script_path} &&
{
  if [[ -d output_binaries_and_tests ]]; then
      echo "Removing a previous instance of the directory \
output_binaries_and_tests."
      rm -r output_binaries_and_tests
  fi &&
  mkdir output_binaries_and_tests{,/application_libraries,/tests};
}
