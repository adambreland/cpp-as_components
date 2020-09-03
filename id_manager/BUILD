cc_library(
    name       = "id_manager",
    hdrs       = ["//:include/id_manager.h"],
    srcs       = ["//:src/id_manager.cc"],
    deps       = [],
    copts      = ["-iquote .", "-Wextra"],
    visibility = ["//visibility:public"]
)

cc_test(
    name    = "id_manager_test",
    srcs    = ["//:test/id_manager_test.cc"],
    deps    = ["//:id_manager", "@googletest//:gtest", 
               "@googletest//:gtest_main"],
    copts   = ["-iquote .", "-Wextra"],
    timeout = "eternal"
)
