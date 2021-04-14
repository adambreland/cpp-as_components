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

# A convenience script to build and test namespace fcgi.
#    This script builds as_components from a local source directory. A
# container is derived from the build and test image (image:
# as_components/build_and_test). The source directory is exposed to the
# container through a bind mount. The script is configured to use a directory
# within a Docker volume as the Bazel outputUserRoot directory. The use of a
# volume allows incremental builds without container image modifications. It
# also prevents internal Bazel build state from being stored in any container.
#    If the build was successful, tests for namespace fcgi are executed. These
# include unit tests and external tests. Unit tests are executed by Bazel, e.g.
# through cc_test targets, in its testing environment. External tests are
# simply tests which are executed outside of the Bazel testing environment.
# These tests are executed in a container which is derived from an image which
# is built specifically for external tests (image:
# as_components/fcgi/external_tests).
#
# Arguments: none
#
# Preconditions:
# 1) The script is located in directory fcgi of the as_components source
#    directory.
# 2) The as_components source directory has been configured for build and
#    test actions with docker_build_configure.sh.
# 3) The script is executed by the root user.
# 4) Docker configuration preconditions:
#    a) A Docker volume named as_components_build_and_test either does
#       not exist or exists with the intended use as persistent storage for
#       the build and test actions of as_components.
#       1) An immediate subdirectory bazel_files of this directory is used as
#          the Bazel outputUserRoot directory.
#    b) The Dockerfile for the external test container image
#       as_components/fcgi/external_tests exists at fcgi/test/Dockerfile.
#    c) The external test runner script is located at
#       fcgi/test/external_test_runner.sh.
#
# Effects:
# 1) An attempt is made to build the modules of fcgi.
# 2) If the build actions were successful:
#    a) The build outputs are copied to the appropriate locations of
#       output_binaries_and_tests in the local source directory. These include
#       any external tests of fcgi.
#    b) Bazel tests were performed.
#    c) External tests were performed.

build_container_name=as_components_fcgi_build
test_container_name=as_components_fcgi_test

# AND list: Build as_components.
# It is assumed that the script is in as_components.
# An absolute path is needed to mount as_components.
as_components_path=${0%/*} &&
if [[ ${as_components_path} == ${0} ]]; then
    as_components_path=${PWD}
else
    cd ${as_components_path} &&
    as_components_path=${PWD}
fi &&
echo -e "Starting the build container." &&
execute_docker_run_for_build=0 &&
docker run \
    --name ${build_container_name} \
    -t \
    --mount type=volume,src=as_components_build_and_test,dst=/usr/local/src/build_and_test \
    --mount type=bind,src=${as_components_path},dst=/usr/local/src/as_components \
    as_components/build_and_test:latest \
    /bin/bash /usr/local/src/as_components/internal_build_and_test.sh

# Conditionally test after the build.
build_return_status=${?}
if [[ -v execute_docker_run_for_build ]]; then
    echo -e "\nRemoving the build container listed below:"
    docker container rm ${build_container_name}
    if [[ ${build_return_status} -eq 0 ]]; then
        # Build a test image and execute the external tests.
        # AND list: Build the test container image and run the external test
        # runner script in a derived container.
        echo -e "\nPerforming external tests:"
        echo    "Building the test container image:" &&
        docker build \
            -t as_components/fcgi/external_tests \
            --file fcgi/test/Dockerfile \
            output_binaries_and_tests &&
        echo -e "\nStarting the test container:" &&
        execute_docker_run_for_test=0 &&
        docker run \
            --name ${test_container_name} \
            -t \
            as_components/fcgi/external_tests \
            /bin/bash /usr/local/bin/as_components/fcgi/test/external_test_runner.sh

        test_return_status=${?}
        if [[ -v execute_docker_run_for_test ]];then
            echo -e "\nRemoving the test container listed below:"
            docker container rm ${test_container_name}
        fi
        exit ${test_return_status}
    fi
else
    exit ${build_return_status}
fi
