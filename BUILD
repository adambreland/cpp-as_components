cc_library(
    name = "fcgi_si",
    deps =
        ["@linux_scw//:linux_scw",
        "@socket_functions//:socket_functions"],
    srcs =
        glob(["include/*.h", "src/*.cc"])
        + ["@error_handling//:include/error_handling.h"],
    hdrs = ["fcgi_si.h"],
    copts = ["-iquote .", "-Wextra"],
    visibility = ["//visibility:public"]
)
