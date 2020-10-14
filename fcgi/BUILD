cc_library(
    name      = "test_fcgi_client_interface",
    deps      = [
        "@fcgi_si//:fcgi_si_utilities",
        "@socket_functions//:socket_functions"
    ],
    srcs      = ["//:src/test_fcgi_client_interface.cc"],
    hdrs      = [
        "//:include/test_fcgi_client_interface.h",
        "@fcgi_si//:include/protocol_constants.h",
        "@fcgi_si//:include/request_identifier.h",
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
