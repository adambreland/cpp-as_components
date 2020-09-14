exports_files(["include/id_manager_template.h"])

cc_test(
    name    = "id_manager_template_test",
    srcs    = [
               "//:include/id_manager_template.h",
               "//:test/id_manager_template_test.cc"
    ],
    deps    = [
               "@googletest//:gtest", 
               "@googletest//:gtest_main"
    ],
    copts   = ["-iquote .", "-Wextra"],
    timeout = "eternal"
)
