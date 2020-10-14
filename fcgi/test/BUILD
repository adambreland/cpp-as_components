cc_library(
    name      = "test_fcgi_client_interface",
    deps      = [
        "//:utilities",
        "@socket_functions//:socket_functions"
    ],
    srcs      = ["//test:src/test_fcgi_client_interface.cc"],
    hdrs      = [
        "//test:include/test_fcgi_client_interface.h",
        "//:include/protocol_constants.h",
        "//:include/request_identifier.h",
        "@id_manager//:include/id_manager_template.h"
    ],
    copts     = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote .",
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name       = "fcgi_si_testing_utilities",
    deps       = [
        "//:server_interface",
        "@googletest//:gtest"
    ],
    srcs       = [
        "//test:src/fcgi_si_testing_utilities.cc",
        "//test:include/fcgi_si_testing_utilities_templates.h"
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
    name       = "utilities_test",
    deps       = [
        "//:utilities",
        "//test:fcgi_si_testing_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:utilities_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote .",
    ]
)

cc_test(
    name       = "server_interface_test",
    deps       = [
        "//:server_interface",
        "//test:fcgi_si_testing_utilities",
        "@socket_functions//:socket_functions",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:server_interface_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote .",
    ]
)
