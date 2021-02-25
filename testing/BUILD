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

load("@as_components//:common_build_data.bzl", "copts_list")

cc_library(
    name = "as_components_testing_utilities",
    deps = [],
    srcs = [
        "src/as_components_testing_utilities.cc",
        "include/as_components_testing_utilities_templates.h"
    ],
    hdrs = ["include/as_components_testing_utilities.h"],
    copts = copts_list,
    linkstatic = True,
    features = ["interpret_as_archive"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)

# It was found that, for the binaries of:
# //test:as_components_testing_utilities_test
# //gtest/test:as_components_testing_gtest_utilities_test
#
# Bazel uses a RUNPATH value that depends on the existence of the directory
# _solib_x86_64 at the output root. However, Bazel does not create this
# directory. This genrule creates an empty directory which allows the RUNPATH
# value to be valid. This is a hack to allow the test binaries to
# find their shared library dependencies.
#
# The question is why Bazel creates _solib_k8 and not _solib_x86_64.
genrule(
    name = "patch_missing_x86_64_directory",
    srcs = [],
    outs = ["_solib_x86_64"],
    cmd_bash = "mkdir $@",
    visibility = ["//visibility:public"]
)
