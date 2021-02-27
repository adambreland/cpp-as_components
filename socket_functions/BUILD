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

load("//common_data:common_build_data.bzl", "copts_with_optimization_list")

# Build targets

# The header and binary target definitions for libsocket_functions.so.
cc_library(
    name = "socket_functions_header",
    deps = [],
    srcs = [],
    hdrs = ["include/socket_functions.h"],
    visibility = ["//visibility:public"]
)

cc_binary(
    name = "libsocket_functions.so",
    deps = [":socket_functions_header"],
    srcs = ["src/socket_functions.cc"],
    copts = copts_with_optimization_list,
    features = ["interpret_as_shared_library"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)
