cc_test(
    name       = "test_fcgi_client_interface_test",
    deps       = [
        "//:test_fcgi_client_interface",
        "@fcgi_si//test:fcgi_si_testing_utilities",
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
