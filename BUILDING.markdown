# Building `as_components`
## Build dependencies and actions upon installation
### Dependencies
* Build and test actions occur in Docker containers.
  * `as_components` uses [Bazel](https://bazel.build/) for build actions. Bazel
    is also used to execute most tests. Bazel is added automatically to the
    necessary Docker container images. If you are only interested in
    producing binaries, then the use of Bazel for build and test actions is
    transparent.
* The build process uses a Bazel toolchain which assumes Linux and x86-64.
  * Modifying the build process for other Unix variants and ISAs should be
    possible, though doing so requires knowledge of Bazel toolchain definition
    with platforms. Building for or on other platforms has not been tested.

### Initial installation and configuration
1. Install Docker if it is not installed.
2. Run `docker_build_configure.sh` as root. This script builds the Docker
   images which are needed for build and test actions.

## Building a module
1. Run the `build_and_test.sh` script for the module that you want to build.
   In most cases, this will be the `fcgi` script at `fcgi/build_and_test.sh`.
   Note that the local source directory is mounted into a Docker container and
   modified during script execution. If the build was successful, then:
   1. Build outputs were placed in the appropriate subdirectories of
      `output_binaries_and_tests/application_libraries`.
   2. Tests were run. The results were displayed.
2. Note that a Docker volume named `as_components_build_and_test` is created
   for build and test actions to store Bazel files and test files.

### Test result review (if performing development)
Test results can be inspected by running `inspect_test_results.sh`. This script
executes a Bash session in a container derived from the build and test image
with the local source directory mounted at `/usr/local/src/as_components` and
the Docker volume `as_components_build_and_test` mounted at
`/usr/local/src/build_and_test`.

Bazel includes the paths to the test logs of failed tests when it reports test
results. These paths can be used in the Bash session started by
`inspect_test_results.sh` to see the logs of failing tests.
