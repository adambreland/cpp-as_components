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
