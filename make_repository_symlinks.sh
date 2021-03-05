#! /bin/bash

echo -e "${0}""\n"

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
