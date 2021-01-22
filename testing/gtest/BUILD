cc_library(
  name       = "as_components_testing_gtest_utilities",
  deps       = [
      "//:as_components_testing_utilities",
      "@googletest//:gtest"
  ],
  srcs       = ["//gtest:src/as_components_testing_gtest_utilities.cc"],
  hdrs       = ["//gtest:include/as_components_testing_gtest_utilities.h"],
  copts      = [
      "-std=c++17",
      "-g",
      "-O2",
      "-Wextra",
      "-iquote ."
  ],
  visibility = ["//visibility:public"]
)
