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
    name          = "client_interface_testing_utilities",
    deps          = [
        "//:fcgi_protocol_constants",
        "//:fcgi_request_identifier",
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "@as_components_testing//:as_components_testing_utilities",
        "@as_components_testing//gtest:as_components_testing_gtest_utilities",
        "@googletest//:gtest"
    ],
    srcs          = ["//test/test:src/client_interface_testing_utilities.cc"],
    hdrs          = [
        "//test/test:include/client_interface_testing_utilities.h"
    ],
    copts         = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ],
    # Set the feature test macro which is required for use of timer_create and
    # timer_settime. The minimum value is 199309L.
    local_defines = ["_POSIX_C_SOURCE=199309L"],
    # Link against the realtime library librt. Needed on Linux for use of
    # timer_create and timer_settime (POSIX interval timer functions).
    linkopts      = ["-lrt"],
    visibility    = ["//visibility:public"]
)

cc_test(
    name  = "test_fcgi_client_interface_test",
    deps  = [
        "//:fcgi_protocol_constants",
        "//:fcgi_request_identifier",
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "//test/test:client_interface_testing_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@socket_functions//:socket_functions"
    ],
    srcs  = ["//test/test:test_fcgi_client_interface_test.cc"],
    copts = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_test(
    name  = "fcgi_si_testing_utilities_test",
    deps  = [
        "//:fcgi_utilities",
        "//test:fcgi_si_testing_utilities",
        "@as_components_testing//gtest:as_components_testing_gtest_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs  = ["//test/test:fcgi_si_testing_utilities_test.cc"],
    copts = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_library(
    name       = "shared_interpositioning_state",
    srcs       = ["//test/test:src/shared_interpositioning_state.cc"],
    hdrs       = ["//test/test:include/shared_interpositioning_state.h"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name     = "connect_interpositioning",
    deps     = [
        "//:server_interface_combined",
        "//test/test:shared_interpositioning_state"
    ],
    srcs     = ["//test/test:src/connect_interpositioning.cc"],
    copts    = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ],
    linkopts = ["-ldl"]
)

cc_test(
    name     = "connect_interpositioning_test",
    deps     = [
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "//test/test:client_interface_testing_utilities",
        "//test/test:shared_interpositioning_state",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@socket_functions//:socket_functions"
    ],
    srcs     = ["//test/test:connect_interpositioning_test.cc"],
    copts    = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)
