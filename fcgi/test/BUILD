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
    ],
    visibility = ["//visibility:public"]
)

cc_test(
    name       = "fcgi_si_testing_utilities_test",
    deps       = [
        "//:fcgi_si_utilities",
        "//test:fcgi_si_testing_utilities",
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
