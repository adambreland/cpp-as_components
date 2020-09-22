exports_files([
    "include/protocol_constants.h",
    "include/request_identifier.h",
    "include/utilities.h"
])

cc_library(
    name       = "fcgi_si_utilities",
    srcs       = [
        "include/utility_templates.h",
        "src/utility.cc"
    ],
    hdrs       = [
        "include/protocol_constants.h",
        "include/utility.h"
    ],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote .",
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name       = "fcgi_si",
    deps       = [
        "//:fcgi_si_utilities",
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
        "include/protocol_constants.h",
        "include/fcgi_request.h",
        "include/fcgi_server_interface.h",
        "include/record_status.h",
        "include/request_data.h",
        "include/request_identifier.h",
        "fcgi_si.h"
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
