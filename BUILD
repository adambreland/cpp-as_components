cc_library(
    name       = "fcgi_si",
    deps       = ["@socket_functions//:socket_functions"],
    srcs       = glob(["include/*.h", "src/*.cc"]),
    hdrs       = ["fcgi_si.h"],
    copts      = ["-iquote .", "-Wextra"],
    visibility = ["//visibility:public"]
)
