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
        "@a_component_testing//gtest:a_component_testing_gtest_utilities",
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
        "@a_component_testing//:a_component_testing_utilities",
        "@a_component_testing//gtest:a_component_testing_gtest_utilities",
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
