FROM  build_and_test as build_lib
SHELL ["/bin/bash", "-c"]
# Retrieve the source of as_components
RUN   curl -fsSL https://github.com/adambreland/cpp-as_components/tarball/docker_trial \
        > temp_repo_tarball.tar.gz && \
      tar --extract -f temp_repo_tarball.tar.gz --gunzip && \
      cd *cpp-as_components* && \
      mv ${PWD} /usr/local/src/as_components && \
      cd / && \
      rm temp_repo_tarball.tar.gz
# Build and test the library artifacts.
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
COPY --from=build_lib /usr/local/bin/as_components/ \
                      /usr/local/bin/as_components/
