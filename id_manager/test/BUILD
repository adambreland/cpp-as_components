cc_test(
    name    = "id_manager_template_test",
    srcs    = [
        "//:include/id_manager_template.h",
        "//test:id_manager_template_test.cc"
    ],
    deps    = [
        "@googletest//:gtest", 
        "@googletest//:gtest_main"
    ],
    copts   = [
      "-std=c++17",
      "-g",
      "-iquote .",
      "-Wextra"
    ],
    timeout = "eternal"
)
