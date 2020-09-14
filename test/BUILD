cc_library(
    name       = "fcgi_si_testing_utilities",
    deps       = ["@googletest//:gtest"],
    srcs       = ["//test:fcgi_si_testing_utilities.cc"],
    hdrs       = ["//:include/protocol_constants.h", 
                  "//test:fcgi_si_testing_utilities.h"],
    copts      = ["-std=c++17", "-iquote .", "-Wextra"]
)

cc_test(
    name       = "fcgi_si_testing_utilities_test",
    deps       = ["//test:fcgi_si_testing_utilities", "//:fcgi_si_utilities",
                  "@googletest//:gtest", "@googletest//:gtest_main"],
    srcs       = ["//test:fcgi_si_testing_utilities_test.cc"],
    copts      = ["-std=c++17", "-iquote .", "-Wextra"]
)

cc_test(
    name       = "fcgi_si_utilities_test",
    deps       = ["//:fcgi_si_utilities", "//test:fcgi_si_testing_utilities", 
                  "@googletest//:gtest", "@googletest//:gtest_main"],
    srcs       = ["//test:fcgi_si_utilities_test.cc"],
    copts      = ["-std=c++17", "-iquote .", "-Wextra"]
)

cc_test(
    name       = "fcgi_si_test",
    deps       = ["//:fcgi_si", "//test:fcgi_si_testing_utilities",
                  "@socket_functions//:socket_functions", "@googletest//:gtest", 
                  "@googletest//:gtest_main"],
    srcs       = ["//test:fcgi_si_test.cc"],
    copts      = ["-std=c++17", "-iquote .", "-Wextra"],
    timeout    = "short"
)
