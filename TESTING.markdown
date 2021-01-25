# Testing the modules of `as_components`
## Introduction
[Google Test](https://github.com/google/googletest) is used as the testing
framework for as_components. It is a C++ testing framework which is based on
the xUnit architecture. Almost all test programs are built from
[Bazel](https://bazel.build/) `cc_test` definitions. Such test programs use
the default Google Test main function with Bazel label
`@googletest//:gtest_main`.

Test program source files are present within `test` directories. Some test
utilities are associated with header files and implementation files. Nested
test directories (`.../test/test/...`) indicate the presence of tests for test
utilities.

## Using Google Test assertions in functions
It is often desirable to modularize several Google Test assertions and related
logic into a function. Doing so presents several problems.

First, assertion failure line numbers are reported relative to the lines of the
function and not relative to the source which invoked the function. A mechanism
which reports both locations is desirable. This problem occurs for both single
function calls and for nested calls of functions which use Google Test
assertions.

Second, Google Test fatal failures do not cause an immediate halt to the
execution of a test. Instead, they behave similarly to function return
statements: the current function only is exited when a fatal assertion fails.
It is assumed that a fatal assertion in a function call is equivalent in
meaning to a fatal assertion in the source that called the function.

These two problems motivate the following discipline when functions are defined
which use Google Test assertions.

### Discipline for defining functions which use Google Test assertions
* Any function which may be used by multiple clients must be named with a
  prefix of either "GTestFatal" or "GTestNonFatal". The presence of a fatal
  Google Test assertion in the definition of the function determines which
  prefix is used.
* Any function which is named with one of the preceding prefixes must
  include a Google Test ScopedTrace instance as its first variable
  (`::testing::ScopedTrace`). In addition, the last parameter of the function
  will be an `int` with the meaning of invocation line number.

  The ScopedTrace variable will be initialized with (in order):
  * The file path such as through use of `__FILE__`.
  * The value of the last argument of the function call. This should be the
    invocation line of the function.
  * The name of the function.
* Any function which uses a fatal assertion must return `void`. As such, any
  function which is named with the "GTestFatal" prefix must return `void`. The
  requirement on returning `void` is not present for functions which do not use
  fatal assertions.
* Exceptions from tested modules which occur during the execution of a
  function which uses Google Test assertions are usually caught and treated
  as fatal failures. For example, `ASSERT_NO_THROW` could be used to catch an
  exception from a function under test and report it as a fatal failure.

### Use of functions which use Google Test assertions:
* When using a function with a Google Test prefix, the invocation line of the
  call will be supplied to the trailing `int` parameter of the function. This
  is usually done with `__LINE__`.
* If the function uses fatal assertions, a call to the function will be
  wrapped with a call to `ASSERT_NO_FATAL_FAILURE` whenever this is necessary
  to preserve the desired semantics of an immediate halt to the execution of
  the test upon the occurrence of a fatal failure.

For example:
```
ASSERT_NO_FATAL_FAILURE(testing::gtest::GTestFatalIgnoreSignal(SIGPIPE,
   __LINE__));
```
