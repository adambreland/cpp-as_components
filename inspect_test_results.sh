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

as_components_path=${0%/*} &&
if [[ ${as_components_path} == ${0} ]]; then
    as_components_path=${PWD}
else
    cd ${as_components_path} &&
    # An absolute path is needed for a Docker bind mount.
    as_components_path=${PWD}
fi &&
{
    docker run \
        --name test_result_inspector \
        -i -t \
        --mount type=volume,src=as_components_build_and_test,dst=/usr/local/src/build_and_test \
        --mount type=bind,src=${as_components_path},dst=/usr/local/src/as_components \
        as_components/build_and_test
        /bin/bash
    echo "Removing the container which was used to inspect test results."
    docker container rm test_result_inspector
}

exit
