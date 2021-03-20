#! /bin/bash

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
docker run \
    --name test_result_inspector \
    -i -t \
    --mount type=volume,src=as_components_bazel_output_user_root,dst=/usr/local/src/bazel_files \
    --mount type=bind,src=${as_components_path},dst=/usr/local/src/as_components \
    as_components/build_and_test
    /bin/bash &&
echo "Removing the container which was used to inspect test results." &&
docker container rm test_result_inspector &&
exit
