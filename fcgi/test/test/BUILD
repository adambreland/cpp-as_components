cc_test(
    name       = "test_fcgi_client_interface_test",
    deps       = [
        "//test:test_fcgi_client_interface",
        "//test:fcgi_si_testing_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
],
    srcs       = ["//test/test:test_fcgi_client_interface_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)

cc_test(
    name       = "fcgi_si_testing_utilities_test",
    deps       = [
        "//:utilities",
        "//test:fcgi_si_testing_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test/test:fcgi_si_testing_utilities_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
        "-iquote ."
    ]
)
