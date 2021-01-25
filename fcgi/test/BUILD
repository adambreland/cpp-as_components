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
    name      = "test_fcgi_client_interface",
    deps      = [
        "//:fcgi_utilities",
        "@socket_functions//:socket_functions"
    ],
    srcs      = ["//test:src/test_fcgi_client_interface.cc"],
    hdrs      = [
        "//test:include/test_fcgi_client_interface.h",
        "//:include/fcgi_protocol_constants.h",
        "//:include/fcgi_request_identifier.h",
        "@id_manager//:include/id_manager_template.h"
    ],
    copts     = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote ."
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name       = "fcgi_si_testing_utilities",
    deps       = [
        "//:server_interface_combined",
        "@googletest//:gtest"
    ],
    srcs       = [
        "//test:src/fcgi_si_testing_utilities.cc",
    ],
    hdrs       = ["//test:include/fcgi_si_testing_utilities.h"],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote ."   
    ],
    visibility = ["//visibility:public"]
)

cc_test(
    name       = "fcgi_utilities_test",
    deps       = [
        "//:fcgi_utilities",
        "//test:fcgi_si_testing_utilities",
        "@as_components_testing//gtest:as_components_testing_gtest_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:utilities_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_test(
    name       = "server_interface_combined_test",
    deps       = [
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "@as_components_testing//:as_components_testing_utilities",
        "@as_components_testing//gtest:as_components_testing_gtest_utilities",
        "@socket_functions//:socket_functions",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:server_interface_combined_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)
