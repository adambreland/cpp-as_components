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
    "//common_data:common_build_data.bzl",
    "copts_list",
    "copts_with_optimization_list"
)

cc_library(
    name = "fcgi_si_testing_utilities",
    deps = [
        "//fcgi:fcgi_protocol_constants",
        "//fcgi:fcgi_server_interface_combined_header",
        # The Google Test library will be compiled with its own toolchain.
        # As such, it can be listed as a dependency in the usual way.
        "@googletest//:gtest"
    ],
    srcs = [
        "src/fcgi_si_testing_utilities.cc",
        "//fcgi:libfcgi_server_interface_combined.so"
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
        "//fcgi:fcgi_protocol_constants",
        "//fcgi:fcgi_request_identifier",
        "//id_manager:id_manager"
    ],
    srcs = [],
    hdrs = ["include/test_fcgi_client_interface.h"],
    visibility = ["//visibility:public"]
)

cc_binary(
    name = "libtest_fcgi_client_interface.so",
    deps = [
        ":test_fcgi_client_interface_header",
        "//fcgi:fcgi_utilities_header",
        "//socket_functions:socket_functions_header"
    ],
    srcs = [
        "src/test_fcgi_client_interface.cc",
        "//fcgi:libfcgi_utilities.so",
        "//socket_functions:libsocket_functions.so"
    ],    
    copts = copts_with_optimization_list,
    features = ["interpret_as_shared_library"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)

cc_test(
    name = "fcgi_utilities_test",
    deps = [
        "//fcgi:fcgi_protocol_constants",
        "//fcgi:fcgi_utilities_header",
        ":fcgi_si_testing_utilities", # Archive
        "//testing/gtest:as_components_testing_gtest_utilities", # Archive
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs = [
        "fcgi_utilities_test.cc",
        "//fcgi:libfcgi_utilities.so"
    ],
    copts = copts_list,
    # 1) Expose the indirect libfcgi_server_interface_combined.so dependency
    #    with $${ORIGIN}/..
    # 2) Expose the indirect libsocket_functions.so depedency (from
    #    libfcgi_server_interface_combined.so) with
    #    $${ORIGIN}/../../socket_functions
    env = {
        "LD_LIBRARY_PATH":
            "$${ORIGIN}/..:$${ORIGIN}/../../socket_functions"
    },
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

cc_test(
    name       = "fcgi_server_interface_combined_test",
    deps       = [
        "//fcgi:fcgi_protocol_constants",
        "//fcgi:fcgi_utilities_header",
        "//fcgi:fcgi_server_interface_combined_header",
        ":fcgi_si_testing_utilities", # Archive
        "//testing:as_components_testing_utilities", # Archive
        "//testing/gtest:as_components_testing_gtest_utilities", # Archive
        "//socket_functions:socket_functions_header",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = [
        "fcgi_server_interface_combined_test.cc",
        "//fcgi:libfcgi_utilities.so",
        "//fcgi:libfcgi_server_interface_combined.so",
        "//socket_functions:libsocket_functions.so"
    ],
    copts      = copts_list,
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

cc_binary(
    name = "fcgi_shared_library_installation_test",
    deps = [
        "//fcgi:fcgi_server_interface_combined_header",
        ":test_fcgi_client_interface_header",
        "//socket_functions:socket_functions_header"
    ],
    srcs = [
        "fcgi_shared_library_installation_test.cc",
        "//fcgi:libfcgi_server_interface_combined.so",
        # Link in fcgi_utilities due to the use of this library by templates
        # of fcgi_server_interface_combined.
        "//fcgi:libfcgi_utilities.so",
        ":libtest_fcgi_client_interface.so",
        "//socket_functions:libsocket_functions.so"
    ],
    copts = copts_list,
    features = ["interpret_as_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

cc_library(
    name = "curl_easy_handle_classes",
    deps = [],
    srcs = ["src/curl_easy_handle_classes.cc"],
    hdrs = ["include/curl_easy_handle_classes.h"],
    copts = copts_list,
    # It is assumed that libcurl is installed as a system library.
    linkopts = ["-lcurl"],
    linkstatic = True,
    features = ["interpret_as_archive"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)

# This test is executed by fcgi_server_interface_integration_test.
# The env attribute is set to document test dependencies.
cc_test(
    tags = ["manual"],
    name = "fcgi_server_interface_integration_test_app_server",
    deps = [
        "//fcgi:fcgi_request_identifier",
        "//fcgi:fcgi_server_interface_combined_header",
        "//fcgi:fcgi_utilities_header"
    ],
    srcs = [
        "fcgi_server_interface_integration_test_app_server.cc",
        "//fcgi:libfcgi_server_interface_combined.so",
        "//fcgi:libfcgi_utilities.so"
    ],
    copts = copts_list,
    env = {
        "LD_LIBRARY_PATH": "$${ORIGIN}/../../socket_functions"
    },
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
)

# This test is executed by fcgi_server_interface_integration_test.
cc_test(
    tags = ["manual"],
    name = "fcgi_server_interface_integration_test_http_client",
    deps = [
        # It is assumed that libcurl is present as a system library.
        ":curl_easy_handle_classes",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs = ["fcgi_server_interface_integration_test_http_client.cc"],
    copts = copts_list,
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
)

sh_test(
    name = "fcgi_server_interface_integration_test",
    deps = [],
    data = [
        ":fcgi_server_interface_integration_test_app_server",
        ":fcgi_server_interface_integration_test_http_client",
        "fcgi_server_interface_integration_test_nginx.conf.template"
    ],
    srcs = ["fcgi_server_interface_integration_test.sh"]
)
