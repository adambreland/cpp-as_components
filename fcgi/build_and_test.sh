build_container_name=as_components_fcgi_build
test_container_name=as_components_fcgi_test

# AND list: build as_components.
# It is assumed that the script is in as_components/fcgi.
script_path=${0%/*} &&
cd ${script_path}/.. &&
# An absolute path is needed for a Docker bind mount.
as_components_path=${PWD} &&
echo -e "Starting the build container." &&
execute_docker_run_for_build=0 &&
docker run \
    --name ${build_container_name} \
    -t \
    --mount type=volume,src=bazel_files,dst=/usr/local/src/bazel_files \
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
        # AND list: build the test container image and run it.
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
