#! /bin/bash

# A convenience script to build and test namespace fcgi.
#    This script builds as_components from a local source directory. A
# container is derived from the build and test image
# (image: as_components/build_and_test). The source directory is exposed to the
# container through a bind mount. The script is configured to use a Docker
# volume as the Bazel outputUserRoot directory. The use of a volume allows
# incremental builds without container image modifications. It also prevents
# internal Bazel build state from being stored in any container.
#    If the build was successful, tests for namespace fcgi are executed. These
# include unit tests and external tests. Unit tests are executed by Bazel, e.g.
# through cc_test targets, in its testing environment. External tests are
# simply tests which are executed outside of the Bazel testing environment.
# These tests are executed in a container which is derived from an image which
# is built specifically for external tests. A Ubuntu base image is modified to
# include:
# 1) The shared libraries which are needed to use the modules of namespace fcgi. 
# 2) Any dependecies of the tests other than the aforementioned libraries.
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
#    a) A Docker volume named as_components_bazel_output_user_root either does
#       not exist or exists with an intended use as an outputUserRoot directory
#       for Bazel.
#    b) The build and test container image as_components/build_and_test
#       exists.
#    c) The Dockerfile for the external test container image
#       as_components/fcgi/external_tests exists at fcgi/test/Dockerfile.
#    d) The external test runner script is located at
#       fcgi/test/external_test_runner.sh.

build_container_name=as_components_fcgi_build
test_container_name=as_components_fcgi_test

# AND list: Build as_components.
# It is assumed that the script is in fcgi.
script_path=${0%/*} &&
if [[ ${script_path} == ${0} ]]; then
    script_path=${PWD}
else
    cd ${script_path} &&
    script_path=${PWD}
fi &&
cd ${script_path}/.. &&
# An absolute path is needed for a Docker bind mount.
as_components_path=${PWD} &&
echo -e "Starting the build container." &&
execute_docker_run_for_build=0 &&
docker run \
    --name ${build_container_name} \
    -t \
    --mount type=volume,src=as_components_bazel_output_user_root,dst=/usr/local/src/bazel_files \
    --mount type=bind,src=${as_components_path},dst=/usr/local/src/as_components \
    as_components/build_and_test:latest \
    /bin/bash /usr/local/src/as_components/fcgi/internal_build_and_test.sh

# Conditionally test after the build.
build_return_status=${?}
if [[ -v execute_docker_run_for_build ]]; then
    echo -e "\nRemoving the build container listed below:"
    docker container rm ${build_container_name}
    if [[ ${build_return_status} -eq 0 ]]; then
        # Build a test image and execute the external tests.
        # AND list: Build the test container image and run the external test
        # runner script in a derived container.
        echo -e "\nBuilding the test container image:" &&
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
