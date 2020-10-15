cc_test(
    name       = "test_fcgi_client_interface_test",
    deps       = [
        "//test:test_fcgi_client_interface",
        "//test:fcgi_si_testing_utilities",
        "@a_component_testing//:a_component_testing_utilities",
        "@a_component_testing//gtest:a_component_testing_gtest_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
],
    srcs       = ["//test/test:test_fcgi_client_interface_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra"
    ]
)

cc_test(
    name       = "fcgi_si_testing_utilities_test",
    deps       = [
        "//:fcgi_utilities",
        "//test:fcgi_si_testing_utilities",
        "@a_component_testing//gtest:a_component_testing_gtest_utilities",
        "@googletest//:gtest",
        "@googletest//:gtest_main"
    ],
    srcs       = ["//test/test:fcgi_si_testing_utilities_test.cc"],
    copts      = [
        "-std=c++17",
        "-g",
        "-Wextra",
    ]
)
