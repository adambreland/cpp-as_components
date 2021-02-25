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

cc_library(
    name = "id_manager",
    deps = [],
    srcs = [],
    hdrs = ["include/id_manager_template.h"],
    visibility = ["//visibility:public"]
)

# This target exists only to allow the execpath of a target at the workspace
# root to be determined for use in :patch_missing_x86_64_directory.
cc_library(
    name = "empty",
    srcs = ["empty.cc"],
    linkstatic = True
)

# It was found that, for the binary of //test:id_manager_template_test, Bazel
# uses an RUNPATH value that depends on the existence of the directory
# _solib_x86_64 at the output root. However, Bazel does not create this
# directory. This genrule creates an empty directory which allows the RUNPATH
# value to be valid. This is a hack to allow //test:id_manager_template_test to
# find its shared library dependencies.
#
# The question is why Bazel creates _solib_k8 and not _solib_x86_64.
genrule(
    name = "patch_missing_x86_64_directory",
    srcs = [":empty"],
    outs = ["_solib_x86_64"],
    # Escape $ with $$ to prevent make variable substitution.
    cmd_bash = "empty_lib_path=$(execpath //:empty); mkdir $${empty_lib_path%/*}/_solib_x86_64",
    visibility = ["//visibility:public"]
)
