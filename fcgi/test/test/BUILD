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

load("//common_data:common_build_data.bzl", "copts_list")

cc_library(
    name = "client_interface_testing_utilities",
    deps = [
        "//fcgi:fcgi_protocol_constants",
        "//fcgi:fcgi_request_identifier",
        "//fcgi:fcgi_server_interface_combined_header",
        "//fcgi:fcgi_utilities_header",
        "//fcgi/test:fcgi_si_testing_utilities", # Archive
        "//fcgi/test:test_fcgi_client_interface_header",
        "//testing:as_components_testing_utilities", # Archive
        "//testing/gtest:as_components_testing_gtest_utilities", # Archive
        "//socket_functions:socket_functions_header",
        "@googletest//:gtest"
    ],
    srcs = [
        "src/client_interface_testing_utilities.cc",
        "src/client_interface_testing_utilities_global_variables.cc",
        "//fcgi:libfcgi_server_interface_combined.so",
        "//fcgi:libfcgi_utilities.so",
        "//fcgi/test:libtest_fcgi_client_interface.so",
        "//socket_functions:libsocket_functions.so"
    ],
    hdrs = [
        "include/client_interface_testing_utilities.h"
    ],
    copts = copts_list,
    # Set the feature test macro which is required for use of timer_create and
    # timer_settime. The minimum value is 199309L.
    local_defines = ["_POSIX_C_SOURCE=199309L"],
    # Link against the realtime library librt. Needed on Linux for use of
    # timer_create and timer_settime (POSIX interval timer functions).
    linkopts = ["-lrt"],
    linkstatic = True,
    features = ["interpret_as_archive"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"],
    visibility = ["//visibility:public"]
)

cc_test(
    name  = "test_fcgi_client_interface_test",
    deps  = [
        "//fcgi:fcgi_protocol_constants",
        "//fcgi:fcgi_request_identifier",
        "//fcgi:fcgi_server_interface_combined_header",
        "//fcgi/test:fcgi_si_testing_utilities", # Archive
        "//fcgi/test:test_fcgi_client_interface_header",
        ":client_interface_testing_utilities", # Archive
        "//socket_functions:socket_functions_header",
        "//testing/gtest:as_components_testing_gtest_utilities", # Archive
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
    srcs  = [
        "test_fcgi_client_interface_test.cc",
        "//fcgi:libfcgi_server_interface_combined.so",
        "//fcgi/test:libtest_fcgi_client_interface.so",
        "//socket_functions:libsocket_functions.so"
    ],
    copts = copts_list,
    # Use $${ORIGIN}/../.. to expose the indirect dependency
    # libfcgi_utilities.so.
    env = {
        "LD_LIBRARY_PATH": "$${ORIGIN}/../.."
    },
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

cc_test(
    name  = "fcgi_si_testing_utilities_test",
    deps  = [
        "//fcgi:fcgi_utilities_header",
        "//fcgi/test:fcgi_si_testing_utilities", # Archive
        "//testing/gtest:as_components_testing_gtest_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs  = [
        "fcgi_si_testing_utilities_test.cc",
        "//fcgi:libfcgi_utilities.so"
    ],
    copts = copts_list,
    # 1) Use $${ORIGIN}/../.. to expose the indirect dependency
    #    libfcgi_server_interface_combined.so.
    # 2) Use $${ORIGIN}/../../../socket_functions to expose the indirect
    #    dependency libsocket_functions.so.
    env = {
        "LD_LIBRARY_PATH":
            "$${ORIGIN}/../..:$${ORIGIN}/../../../socket_functions"
    },
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

# Define a shared library with a global variable which will be accessed by
# both :libconnect_interpositioning.so and :connect_interpositioning_test.
# This header target solely provides an extern declaration of
# shared_connect_EINTR_return_flag.
cc_library(
    name = "shared_interpositioning_state_header",
    deps = [],
    srcs = [],
    hdrs = ["include/shared_interpositioning_state.h"]
)

cc_binary(
    name = "libshared_interpositioning_state.so",
    deps = [":shared_interpositioning_state_header"],
    srcs = ["src/shared_interpositioning_state.cc"],
    copts = copts_list,
    features = ["interpret_as_shared_library"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

# Define the shared library which will be preloaded to interpose on connect. A
# header is not needed in this case.
cc_binary(
    name = "libconnect_interpositioning.so",
    deps = [
        "//fcgi:fcgi_server_interface_combined_header",
        ":shared_interpositioning_state_header"
    ],
    srcs = [
        "src/connect_interpositioning.cc",
        "//fcgi:libfcgi_server_interface_combined.so",
        ":libshared_interpositioning_state.so"
    ],
    copts = copts_list,
    linkopts = ["-ldl"],
    features = ["interpret_as_shared_library"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

cc_test(
    name = "connect_interpositioning_test",
    deps = [
        "//fcgi:fcgi_server_interface_combined_header",
        "//fcgi/test:fcgi_si_testing_utilities", # Archive
        "//fcgi/test:test_fcgi_client_interface_header",
        ":client_interface_testing_utilities", # Archive
        ":shared_interpositioning_state_header",
        "//socket_functions:socket_functions_header",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs = [
        "connect_interpositioning_test.cc",
        "//fcgi:libfcgi_server_interface_combined.so",
        "//fcgi/test:libtest_fcgi_client_interface.so",
        ":libshared_interpositioning_state.so",
        "//socket_functions:libsocket_functions.so"
    ],
    copts = copts_list,
    # Use the data attribute to ensure that libconnect_interpositioning.so is
    # regarded as an indirect dependency of connect_interpositioning_test.
    data = [":libconnect_interpositioning.so"],
    # 1) For LD_PRELOAD, use ${ORIGIN} to determine the correct path to
    #    libconnect_interpositioning.so.
    # 2) For LD_LIBRARY_PATH, use ${ORIGIN}/../.. to expose the indirect
    #    dependency libfcgi_utilities.so.
    env = {
        "LD_PRELOAD": "$${ORIGIN}/libconnect_interpositioning.so",
        "LD_LIBRARY_PATH": "$${ORIGIN}/../.."
    },
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

# This test is executed by curl_easy_handle_classes_internal_test.
cc_test(
    tags = ["manual"],
    name = "curl_easy_handle_classes_internal_test",
    deps = [
        # It is assumed that libcurl is present as a system library.
        "//fcgi/test:curl_easy_handle_classes",
        ":client_interface_testing_utilities",
        "//socket_functions:socket_functions_header",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs = [
        "curl_easy_handle_classes_internal_test.cc",
        "//socket_functions:libsocket_functions.so"
    ],
    copts = copts_list,
    features = ["interpret_as_test_executable"],
    exec_compatible_with = ["@simple_bazel_cpp_toolchain//nonstandard_toolchain:simple_cpp_toolchain"]
)

sh_test(
    name = "curl_easy_handle_classes_test",
    deps = [],
    data = [
        ":curl_easy_handle_classes_internal_test",
        "curl_easy_handle_classes_test_nginx.conf.template",
        "curl_easy_handle_classes_internal_test_http_response_body.txt"
    ],
    srcs = ["curl_easy_handle_classes_test.sh"],
)
