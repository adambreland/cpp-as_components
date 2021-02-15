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

exports_files(
    [
        "include/fcgi_protocol_constants.h",
        "include/fcgi_request_identifier.h"
    ]
)
# This header of constants is not associated with an implementation file.
cc_library(
    name       = "fcgi_protocol_constants",
    deps       = [],
    srcs       = [],
    hdrs       = ["//:include/fcgi_protocol_constants.h"],
    copts      = [],
    visibility = ["//visibility:public"]
)

# FcgiRequestIdentifier is a header-only class as its methods are inlined.
cc_library(
    name       = "fcgi_request_identifier",
    deps       = [],
    srcs       = [],
    hdrs       = ["//:include/fcgi_request_identifier.h"],
    copts      = [],
    visibility = ["//visibility:public"]
)

cc_library(
    name       = "fcgi_utilities",
    deps       = ["//:fcgi_protocol_constants"],
    srcs       = [
        "//:include/fcgi_utilities_templates.h",
        "//:src/fcgi_utilities.cc"
    ],
    hdrs       = ["//:include/fcgi_utilities.h"],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra"
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name       = "server_interface_combined",
    deps       = [
        "//:fcgi_protocol_constants",
        "//:fcgi_request_identifier",
        "//:fcgi_utilities",
        "@socket_functions//:socket_functions",
    ],
    srcs       = [
        "//:include/fcgi_request_templates.h",
        "//:src/fcgi_request.cc",
        "//:src/fcgi_server_interface.cc",
        "//:src/record_status.cc",
        "//:src/request_data.cc"
    ],
    hdrs       = [
        "//:include/fcgi_server_interface.h",
        "//:include/fcgi_request.h"
    ],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-pthread"
    ],
    linkopts   = [
        "-pthread",
        # "-L/home/adam/cpp/as_components/fcgi/bazel-bin",
        # "-lfcgi_utilities",
        # "-L/home/adam/cpp/as_components/socket_functions/bazel-bin",
        # "-lsocket_functions"
    ],
    visibility = ["//visibility:public"]
)
