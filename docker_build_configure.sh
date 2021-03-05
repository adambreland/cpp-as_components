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
