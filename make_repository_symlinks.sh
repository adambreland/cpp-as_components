#! /bin/bash

echo -e "${0}""\n"

# Constants, containers, and functions which are viewed as script globals.

# A map whose keys are the working names of the external dependencies.
# The values for the keys are initially empty. They should be provided within
# arguments when the script is invoked. The arguments are processed, and each
# value is extracted and associated with its respective key.
declare -A external_dependency_map
external_dependency_map=(\
  [googletest]= \
  [simple_bazel_cpp_toolchain]=)

declare -a workspace_name_list
workspace_name_list=(\
  fcgi \
  id_manager \
  socket_functions \
  testing)

function PrintHelp
{
  local escaped_help_message=\
"This script creates symbolic links which are necessary to build the modules \
of as_components. A directory named \"external_repo_links\" is created in \
each of the Bazel workspaces of as_components. Symbolic links to the internal \
and external dependencies of a workspace are added to the external directory \
for each workspace. External dependency paths are provided as script \
arguments with the form <dependency name>=<dependency path>. For example:\n\
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

# This function defines the logic for internal symbolic link creation. It is
# invoked after the state of the workspaces and the values of the script
# arguments have been verified.
function CreateSymlinksForInternalDependencies
{
  local workspace_name
  for workspace_name in "${workspace_name_list[@]}"; do
    mkdir -v "${workspace_name}"/external_repo_links
    case ${workspace_name} in
      (fcgi)
        ln -v -s ${PWD}                  fcgi/external_repo_links/as_components
        ln -v -s ${PWD}/testing          fcgi/external_repo_links/as_components_testing
        ln -v -s ${PWD}/id_manager       fcgi/external_repo_links/id_manager
        ln -v -s ${PWD}/socket_functions fcgi/external_repo_links/socket_functions
        ;;
      (id_manager)
        ln -v -s ${PWD}                  id_manager/external_repo_links/as_components
        ;;
      (socket_functions)
        ln -v -s ${PWD}                  socket_functions/external_repo_links/as_components
        ;;
      (testing)
        ln -v -s ${PWD}                  testing/external_repo_links/as_components
        ;;
      (*)
        PrintEscapedErrorMessageForExit "Internal error. An unknown workspace \
name was encountered during the execution of the script function \
CreateSymlinksForInternalDependencies."
        exit 1
        ;;
    esac
  done
}

function CreateSymlinksForExternalDependencies
{
  # All workspaces use googletest and simple_bazel_cpp_toolchain.
  local original_directory=${PWD}
  cd "${external_dependency_map[googletest]}"
  local absolute_googletest_directory=${PWD}
  cd "${external_dependency_map[simple_bazel_cpp_toolchain]}"
  local absolute_toolchain_directory=${PWD}
  cd ${original_directory}
  local workspace_name
  for workspace_name in "${workspace_name_list[@]}"; do
    # Common actions.
    ln -v -s ${absolute_googletest_directory} \
"${workspace_name}"/external_repo_links/googletest
    ln -v -s ${absolute_toolchain_directory} \
"${workspace_name}"/external_repo_links/simple_bazel_cpp_toolchain
    # Actions specific to a workspace.
    # (No actions are needed in this case.)
    case ${workspace_name} in
      (fcgi)
        ;;
      (id_manager)
        ;;
      (socket_functions)
        ;;
      (testing)
        ;;
      (*)
        PrintEscapedErrorMessageForExit "Internal error. An unknown workspace \
name was encountered during the execution of the script function \
CreateSymlinksForExternalDependencies."
        exit 1
        ;;
    esac
  done
}

# Script logic start.

# Did the user ask for help?
if [[ (${1} == "--help") || (${1} == "help") ]]; then
  PrintHelp
  exit 0
fi

# Performs the following checks on workspace state:
# 1) Is each expected workspace present?
# 2) For each workspace, is a directory named "external" absent as it should be
#    when this script is executed?
# An error message is printed and the script exits if any check fails.
declare -a present_external_directory_list
declare -a missing_workspace_name_list
no_errors=0
all_workspaces_present=0
all_external_directories_absent=0
for workspace_name in "${workspace_name_list[@]}"; do
  if [[ -d ./${workspace_name} ]]; then
    if [[ -d ./${workspace_name}/external ]]; then
      present_external_directory_list+=("${workspace_name}")
      all_external_directories_absent=1
      no_errors=1
    fi
  else
    missing_workspace_name_list+=("${workspace_name}")
    all_workspaces_present=1
    no_errors=1
  fi
done
escaped_error_message=
error_message_new=0
function UpdateForPreviousErrors
{
  if [[ error_message_new -eq 0 ]]; then
    error_message_new=1
  else
    escaped_error_message+="\n\n"
  fi
}
if [[ all_workspaces_present -ne 0 ]]; then
  UpdateForPreviousErrors
  escaped_error_message="Not all workspaces were present. Missing:"
  for workspace_name in "${missing_workspace_name_list[@]}"; do
    escaped_error_message+="\n""${workspace_name}"
  done
fi
if [[ all_external_directories_absent -ne 0 ]]; then
  UpdateForPreviousErrors
  escaped_error_message+="An unexpected external directory was present in:"
  for workspace_name in "${present_external_directory_list[@]}"; do
    escaped_error_message+="\n""${workspace_name}"
  done
fi
if [[ no_errors -ne 0 ]]; then
  PrintEscapedErrorMessageForExit "${escaped_error_message}"
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
    # (( externals_in_arguments_count = externals_in_arguments_count + 1 ))
    (( ++externals_in_arguments_count ))
  fi
  external_dependency_map[${argument_dependency_name}]=\
${argument_dependency_path}
done
# Check that an argument was provided for each external dependency.
if [[ externals_in_arguments_count -ne ${#external_dependency_map[@]} ]]; then
  PrintEscapedErrorMessageForExit "Some external dependency paths were not \
provided."
  exit 1
fi

CreateSymlinksForInternalDependencies
CreateSymlinksForExternalDependencies
echo -e "\nThe symbolic links for as_components were created."
