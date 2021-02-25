FROM  ubuntu:20.04 as ubuntu_base

# The following few commands install Bazel from its custom APT repository.
# https://docs.bazel.build/versions/4.0.0/install-ubuntu.html
#
# g++ and make are also installed.

# Installs the tools needed to retrieve the Bazel respository information and
# retrieves it.
FROM  ubuntu_base as build_and_test
SHELL ["/bin/bash", "-c"]
RUN   apt-get -y update && apt-get -y install curl gnupg
RUN   set -o pipefail && \
      curl -fsSL https://bazel.build/bazel-release.pub.gpg | \
        gpg --dearmor > bazel.gpg && \
      mv bazel.gpg /etc/apt/trusted.gpg.d/ && \
      echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" | \
        tee /etc/apt/sources.list.d/bazel.list
# Updates the new Bazel repository. Installs bazel, g++, googletest, and
# make. Make is required to build NGINX and OpenSSL.
RUN   apt-get -y update && apt-get -y install bazel g++ make
# Retrieves the googletest sources.
RUN   curl -fsSL https://github.com/google/googletest/tarball/release-1.10.0 \
        > temp_repo_tarball.tar.gz && \
      tar --extract -f temp_repo_tarball.tar.gz --gunzip && \
      cd *googletest* && \
      mv ${PWD} /usr/local/src/googletest && \
      cd / && \
      rm temp_repo_tarball.tar.gz

FROM  build_and_test as build_lib
SHELL ["/bin/bash", "-c"]
# Retrieves the source of as_components.
RUN   curl -fsSL https://github.com/adambreland/cpp-as_components/tarball/master \
        > temp_repo_tarball.tar.gz && \
      tar --extract -f temp_repo_tarball.tar.gz --gunzip && \
      cd *cpp-as_components* && \
      mv ${PWD} /usr/local/src/as_components && \
      cd / && \
      rm temp_repo_tarball.tar.gz
# Builds and tests the library artifacts.
RUN   cd /usr/local/src/as_components && \
      ./make_repository_symlinks.sh googletest=/usr/local/src/googletest && \
      cd /usr/local/src/as_components/socket_functions && \
      bazel build //:socket_functions && \
      cd /usr/local/src/as_components/fcgi && \
      bazel build //:fcgi_utilities //:server_interface_combined \
                  //test:test_fcgi_client_interface
RUN   cd /usr/local/src/as_components/socket_functions && \
      bazel test //test:socket_functions_test && \
      cd /usr/local/src/as_components/fcgi && \
      bazel test //test:fcgi_utilities_test && \
      bazel test --test_env=NO_IPV6= \
                 //test:server_interface_combined_test \
                 //test/test:test_fcgi_client_interface_test        
RUN   mkdir /usr/local/bin/as_components \
            /usr/local/bin/as_components/fcgi \
            /usr/local/bin/as_components/fcgi/test \
            /usr/local/bin/as_components/socket_functions && \
      cd /usr/local/src/as_components && \
      cp fcgi/bazel-bin/libfcgi_utilities.so \
         fcgi/bazel-bin/libserver_interface_combined.so \
           /usr/local/bin/as_components/fcgi/ && \
      cp fcgi/bazel-bin/test/libtest_fcgi_client_interface.so \
           /usr/local/bin/as_components/fcgi/test/ && \
      cp socket_functions/bazel-bin/libsocket_functions.so \
           /usr/local/bin/as_components/socket_functions/

FROM  ubuntu_base as lib
SHELL ["/bin/bash", "-c"]
COPY  --from=build_lib /usr/local/bin/as_components/ \
                      /usr/local/bin/as_components/
RUN   ldconfig -X /usr/local/bin/as_components
