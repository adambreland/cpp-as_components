exports_files([
    "include/protocol_constants.h",
    "include/request_identifier.h"
])

cc_library(
    name       = "utilities",
    srcs       = [
        "include/utilities_templates.h",
        "src/utilities.cc"
    ],
    hdrs       = [
        "include/protocol_constants.h",
        "include/utilities.h"
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
    name       = "server_interface",
    deps       = [
        "//:utilities",
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
        "include/request_identifier.h",
        "server_interface.h"
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
