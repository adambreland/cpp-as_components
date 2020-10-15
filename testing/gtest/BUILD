cc_library(
  name       = "a_component_testing_gtest_utilities",
  deps       = [
      "//:a_component_testing_utilities",
      "@googletest//:gtest"
  ],
  srcs       = ["//gtest:src/a_component_testing_gtest_utilities.cc"],
  hdrs       = ["//gtest:include/a_component_testing_gtest_utilities.h"],
  copts      = [
      "-std=c++17",
      "-g",
      "-O2",
      "-Wextra",
      "-iquote ."
  ],
  visibility = ["//visibility:public"]
)
