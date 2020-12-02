cc_library(
    name       = "client_interface_testing_utilities",
    deps       = [
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "@a_component_testing//:a_component_testing_utilities",
        "@a_component_testing//gtest:a_component_testing_gtest_utilities",
        "@googletest//:gtest"
    ],
    srcs       = ["//test/test:src/client_interface_testing_utilities.cc"],
    hdrs       = ["//test/test:include/client_interface_testing_utilities.h"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ],
    visibility = ["//visibility:public"]
)

cc_test(
    name  = "test_fcgi_client_interface_test",
    deps  = [
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "//test/test:client_interface_testing_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@socket_functions//:socket_functions"
    ],
    srcs  = ["//test/test:test_fcgi_client_interface_test.cc"],
    copts = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_test(
    name  = "fcgi_si_testing_utilities_test",
    deps  = [
        "//:fcgi_utilities",
        "//test:fcgi_si_testing_utilities",
        "@a_component_testing//gtest:a_component_testing_gtest_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs  = ["//test/test:fcgi_si_testing_utilities_test.cc"],
    copts = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_library(
    name       = "shared_interpositioning_state",
    srcs       = ["//test/test:src/shared_interpositioning_state.cc"],
    hdrs       = ["//test/test:include/shared_interpositioning_state.h"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ],
    visibility = ["//visibility:public"]
)

cc_library(
    name     = "connect_interpositioning",
    deps     = ["//test/test:shared_interpositioning_state"],
    srcs     = ["//test/test:src/connect_interpositioning.cc"],
    copts    = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ],
    linkopts = ["-ldl"]
)

cc_test(
    name     = "connect_interpositioning_test",
    deps     = [
        "//:server_interface_combined",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "//test/test:client_interface_testing_utilities",
        "//test/test:shared_interpositioning_state",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
        "@socket_functions//:socket_functions"
    ],
    srcs     = ["//test/test:connect_interpositioning_test.cc"],
    copts    = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)
