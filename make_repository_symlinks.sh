#! /bin/bash

# MIT License
#
# Copyright (c) 2021 Adam J. Breland
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# This script is invoked during the configuration of a local source directory.
# This configuration, among other purposes, allows build actions to be taken by
# a container which has a bind mount to the source directory. The script is
# responsible for creating symbolic links in the source directory to the
# external dependencies of as_components. See the WORKSPACE file of
# as_components for the Bazel targets definitions of external dependencies.
#
# Arguments:
# 1) The script accepts "help" and "--help" as the first argument.
# 2) If a help argument is not given, the script expects a sequence of
#    arguments where each argument is a name-value pair for an external
#    dependency path with the form:
#    <external dependency name>=<external dependency path>
#
#    For example: googletest=/usr/local/src/googletest
#
# Preconditions:
# 1) The working directory is the source directory of as_components.
#
# Return status:
# 1) The script returns zero if help was requested or if symbolic links to
#    all external dependencies were created. The script returns a non-zero
#    value otherwise.
# 2) Non-zero return status conditions:
#    a) The directory external_repo_links exists.
#    b) A path for an external dependency from an argument either does not
#       refer to a directory or refers to a directory which does not exist.
#    c) After processing the arguments, at least one external dependency was
#       not associated with a path.
#    d) An argument refers to an unknown external dependency name.

# Constants, containers, and functions which are viewed as script globals.

# A map whose keys are the working names of the external dependencies.
# The values for the keys are initially empty. Paths should be provided within
# arguments when the script is invoked. The arguments are processed, and each
# path is extracted and associated with its respective key.
declare -A external_dependency_map
external_dependency_map=(\
  [googletest]= \
  [simple_bazel_cpp_toolchain]=)

function PrintHelp
{
  local escaped_help_message=\
"This script creates symbolic links which are necessary to build the modules \
of as_components. A directory named external_repo_links is created in \
as_components. Symbolic links to the external dependencies of the workspaces \
are added. External dependency paths are provided as script arguments with the \
form <dependency name>=<dependency path>. For example:\n\
googletest=/usr/local/src/googletest\n\n\
The names of the necessary external dependencies are printed below:"

  echo -e ${escaped_help_message}
  local external_dependency_name
  for external_dependency_name in "${!external_dependency_map[@]}"; do
    echo "${external_dependency_name}"
  done
}

function PrintEscapedErrorMessageForExit
{
  PrintHelp
  echo -e "\nERROR\n"${1}"\n\nScript exiting."
}

function CreateSymlinksForExternalDependencies
{
  mkdir -v external_repo_links || return 1
  local original_directory=${PWD}
  cd "${external_dependency_map[googletest]}"
  local absolute_googletest_directory=${PWD}
  cd "${external_dependency_map[simple_bazel_cpp_toolchain]}"
  local absolute_toolchain_directory=${PWD}
  cd ${original_directory}
  ln -v -s ${absolute_googletest_directory} external_repo_links/googletest &&
  ln -v -s ${absolute_toolchain_directory}  external_repo_links/simple_bazel_cpp_toolchain ||
  return 1
}

# Script logic start.

# Did the user ask for help?
if [[ (${1} == "--help") || (${1} == "help") ]]; then
  PrintHelp
  exit 0
fi

# Performs the following checks on workspace state:
# 1) Is a directory named "external_repo_links" absent as it should be when
#    this script is executed?
# An error message is printed and the script exits if the check fails.
if [[ -d external_repo_links ]]; then
  PrintEscapedErrorMessageForExit "The directory external_repo_links was \
present."
  exit 1
fi

# Processes the arguments, and performs limited validation during processing.
argument_dependency_name=
argument_dependency_path=
declare -i externals_in_arguments_count=0
for argument in "${@}"; do
  argument_dependency_name=${argument%%=*}
  # Was an external dependency name not extracted from the argument?
  if [[ ${#argument} -eq ${#argument_dependency_name} ]]; then
    PrintEscapedErrorMessageForExit "An argument lacked a dependency name or \
was not a name-value pair:\n"${argument}
    exit 1
  fi
  # Is the external dependency name not one of the expected names?
  if [[ !(-v external_dependency_map[${argument_dependency_name}]) ]]; then
    PrintEscapedErrorMessageForExit "An invalid external dependency name was \
present in: ${argument}\nInvalid name: ${argument_dependency_name}"
    exit 1
  fi
  # Extracts the path.
  argument_dependency_path=${argument:${#argument_dependency_name}+1}
  # Does the extracted path not refer to a directory?
  if [[ !(-d ${argument_dependency_path}) ]]; then
    PrintEscapedErrorMessageForExit "An invalid path value for present for: \
${argument_dependency_name}\nInvalid value: ${argument_dependency_path}"
    exit 1
  fi
  # Conditionally updates the count of unique arguments which were processed.
  if [[ -z ${external_dependency_map[${argument_dependency_name}]} ]]; then
    (( ++externals_in_arguments_count ))
  fi
  external_dependency_map[${argument_dependency_name}]=${argument_dependency_path}
done
# Check that an argument was provided for each external dependency.
if [[ externals_in_arguments_count -ne ${#external_dependency_map[@]} ]]; then
  PrintEscapedErrorMessageForExit "Some external dependency paths were not \
provided."
  exit 1
fi

CreateSymlinksForExternalDependencies ||
{
  PrintEscapedErrorMessageForExit "An error occurred during symbolic link \
creation.";
  exit 1;
}
echo -e "\nThe symbolic links for as_components were created."
