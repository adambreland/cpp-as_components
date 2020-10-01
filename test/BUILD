cc_library(
    name       = "fcgi_si_testing_utilities",
    deps       = [
        "//:fcgi_si",
        "@googletest//:gtest"
    ],
    srcs       = [
        "//test:fcgi_si_testing_utilities.cc",
        "//test:fcgi_si_testing_utilities_templates.h"
    ],
    hdrs       = ["//test:fcgi_si_testing_utilities.h"],
    copts      = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote ."        
    ]
)

cc_library(
    name      = "test_fcgi_client_interface",
    deps      = [
        "//:fcgi_si_utilities",
        "@socket_functions//:socket_functions"
    ],
    srcs      = ["//test:test_fcgi_client_interface.cc"],
    hdrs      = [
        "//test:test_fcgi_client_interface.h",
        "//:include/protocol_constants.h",
        "//:include/request_identifier.h",
        "@id_manager//:include/id_manager_template.h"
    ],
    copts     = [
        "-std=c++17",
        "-g",
        "-O2",
        "-Wextra",
        "-iquote ." 
    ]
)

cc_test(
    name       = "fcgi_si_testing_utilities_test",
    deps       = [
        "//:fcgi_si_utilities",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:fcgi_si_testing_utilities_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_test(
    name       = "test_fcgi_client_interface_test",
    deps       = [
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
],
    srcs       = ["//test:test_fcgi_client_interface_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_test(
    name       = "fcgi_si_utilities_test",
    deps       = [
        "//:fcgi_si_utilities",
        "//test:fcgi_si_testing_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:fcgi_si_utilities_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote .",
    ]
)

cc_test(
    name       = "fcgi_si_test",
    deps       = [
        "//:fcgi_si",
        "//test:fcgi_si_testing_utilities",
        "//test:test_fcgi_client_interface",
        "@socket_functions//:socket_functions",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test:fcgi_si_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote .",
    ],
    timeout    = "short"
)
