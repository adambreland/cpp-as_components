#! /bin/bash

# This script is executed by the external test container for namespace fcgi.
# It runs the external tests of namespace fcgi.
test_root=/usr/local/bin/as_components/fcgi/test
${test_root}/fcgi_shared_library_installation_test
