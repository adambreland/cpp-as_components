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

exports_files([
    "include/fcgi_protocol_constants.h",
    "include/fcgi_request_identifier.h"
])

cc_library(
    name       = "fcgi_utilities",
    srcs       = [
        "include/fcgi_utilities_templates.h",
        "src/fcgi_utilities.cc"
    ],
    hdrs       = [
        "include/fcgi_protocol_constants.h",
        "include/fcgi_utilities.h"
    ],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote ."
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name       = "server_interface_combined",
    deps       = [
        "//:fcgi_utilities",
        "@socket_functions//:socket_functions",
    ],
    srcs       = [
        "include/fcgi_request_templates.h",
        "src/fcgi_request.cc",
        "src/fcgi_server_interface.cc",
        "src/record_status.cc",
        "src/request_data.cc"
    ],
    hdrs       = [
        "include/fcgi_protocol_constants.h",
        "include/fcgi_request.h",
        "include/fcgi_server_interface.h",
        "include/fcgi_request_identifier.h",
        "server_interface_combined.h"
    ],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote ."
    ],
    visibility = ["//visibility:public"]
)
