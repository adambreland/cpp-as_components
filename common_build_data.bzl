# Lists of C++ compiler options which may be usedacross the workspaces of
# as_components.
copts_list = [
    "-std=c++17",
    "-g",
    "-Wextra"
]
copts_with_optimization_list = copts_list + ["-O2"]
