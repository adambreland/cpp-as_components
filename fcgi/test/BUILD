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

load(
    "@as_components//:common_build_data.bzl",
    "copts_list",
    "copts_with_optimization_list"
)

cc_library(
    name = "fcgi_si_testing_utilities",
    deps = [
        "//:fcgi_protocol_constants",
        "//:fcgi_server_interface_combined_header",
        # The Google Test library will be compiled with its own toolchain.
        # As such, it can be listed as a dependency in the usual way.
        "@googletest//:gtest"
    ],
    srcs = [
        "src/fcgi_si_testing_utilities.cc",
        "//:libfcgi_server_interface_combined.so"
    ],
    hdrs = ["include/fcgi_si_testing_utilities.h"],
    copts = copts_list,
    linkstatic = True,
    features = ["interpret_as_archive"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)

# The header and binary target definitions for
# libtest_fcgi_client_interface.so.
cc_library(
    name = "test_fcgi_client_interface_header",
    deps = [
        "//:fcgi_protocol_constants",
        "//:fcgi_request_identifier",
        "@id_manager//:id_manager"
    ],
    srcs = [],
    hdrs = ["include/test_fcgi_client_interface.h"],
    visibility = ["//visibility:public"]
)

cc_binary(
    name = "libtest_fcgi_client_interface.so",
    deps = [
        ":test_fcgi_client_interface_header",
        "//:fcgi_utilities_header",
        "@socket_functions//:socket_functions_header"
    ],
    srcs = [
        "src/test_fcgi_client_interface.cc",
        "//:libfcgi_utilities.so",
        "@socket_functions//:libsocket_functions.so"
    ],    
    copts = copts_with_optimization_list,
    features = ["interpret_as_shared_library"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)

cc_test(
    name = "fcgi_utilities_test",
    deps = [
        "//:fcgi_protocol_constants",
        "//:fcgi_utilities_header",
        ":fcgi_si_testing_utilities", # Archive
        "@as_components_testing//gtest:as_components_testing_gtest_utilities", # Archive
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs = [
        "fcgi_utilities_test.cc",
        "//:libfcgi_utilities.so"
    ],
    copts = copts_list,
    # 1) Expose the indirect libfcgi_server_interface_combined.so dependency
    #    with $${ORIGIN}/..
    # 2) Expose the indirect libsocket_functions.so depedency (from
    #    libfcgi_server_interface_combined.so) with
    #    $${ORIGIN}/../external/socket_functions
    env = {
        "LD_LIBRARY_PATH":
            "$${ORIGIN}/..:$${ORIGIN}/../external/socket_functions"
    },
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

cc_test(
    name       = "fcgi_server_interface_combined_test",
    deps       = [
        "//:fcgi_protocol_constants",
        "//:fcgi_utilities_header",
        "//:fcgi_server_interface_combined_header",
        ":fcgi_si_testing_utilities", # Archive
        "@as_components_testing//:as_components_testing_utilities", # Archive
        "@as_components_testing//gtest:as_components_testing_gtest_utilities", # Archive
        "@socket_functions//:socket_functions_header",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = [
        "//test:fcgi_server_interface_combined_test.cc",
        "//:libfcgi_utilities.so",
        "//:libfcgi_server_interface_combined.so",
        "@socket_functions//:libsocket_functions.so"
    ],
    copts      = copts_list,
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)
