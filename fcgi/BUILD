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

copts_list = [
    "-std=c++17",
    "-g",
    "-O2",
    "-Wextra"
]

# The header and binary target definitions for libfcgi_protocol_constants.so.
cc_library(
    name       = "fcgi_protocol_constants_header",
    deps       = [],
    srcs       = [],
    hdrs       = ["//:include/fcgi_protocol_constants.h"],
    visibility = ["//visibility:public"]
)

cc_binary(
    name       = "libfcgi_protocol_constants.so",
    deps       = [],
    srcs       = ["//:src/fcgi_protocol_constants.cc"],
    copts      = copts_list,
    features   = ["interpret_as_shared_library"],
    visibility = ["//visibility:public"]
)

# The header-only target definition for fcgi_request_identifier.h.
cc_library(
    name       = "fcgi_request_identifier",
    deps       = [],
    srcs       = [],
    hdrs       = ["//:include/fcgi_request_identifier.h"],
    visibility = ["//visibility:public"]
)

# The header and binary target definitions for libfcgi_utilities.so.
cc_header(
    name       = "fcgi_utilities_header",
    deps       = [],
    srcs       = [],
    hdrs       = [
        "//:include/fcgi_utilities.h",
        "//:include/fcgi_utilities_templates.h"
    ],
    visibility = ["//visibility:public"]
)

cc_binary(
    name       = "libfcgi_utilities.so",
    deps       = [
        "//:fcgi_utilities_header",
        "//:fcgi_protocol_constants_header"
    ],
    srcs       = [
        "//:src/fcgi_utilities.cc",
        "//:libfcgi_protocol_constants.so"
    ],
    copts      = copts_list,
    features   = ["interpret_as_shared_library"],
    visibility = ["//visibility:public"]
)

# The header and binary target definitions for
# libfcgi_server_interface_combined.so.
pthread_list = ["-pthread"]

cc_library(
    name       = "fcgi_server_interface_combined_header",
    deps       = [
        "//:fcgi_protocol_constants_header",
        "//:fcgi_request_identifier",
        "//:fcgi_utilities_header",
    ],
    srcs       = [],
    hdrs       = [
        "//:include/fcgi_request.h",
        "//:include/fcgi_request_templates.h",
        "//:include/fcgi_server_interface.h"
    ],
    copts      = pthread_list,
    linkopts   = pthread_list,
    visibility = ["//visibility:public"]
)

cc_binary(
    name       = "libfcgi_server_interface_combined.so",
    deps       = [
        "//:fcgi_server_interface_combined_header",
        "@socket_functions//:socket_functions_header"
    ],
    srcs       = [
        "src/fcgi_request.cc",
        "src/fcgi_server_interface.cc",
        "src/record_status.cc",
        "src/request_data.cc",
        "//:libfcgi_protocol_constants.so",
        "//:libfcgi_utilities.so",
        "@socket_functions//:libsocket_functions.so"
    ],
    copts      = copts_list,
    features   = ["interpret_as_shared_library"],
    visibility = ["//visibility:public"]
)
