# Container image: as_components/build_and_test

# The following few commands install Bazel from its custom APT repository.
# https://docs.bazel.build/versions/4.0.0/install-ubuntu.html
#
# Installs the tools needed to retrieve the Bazel respository information and
# retrieves it.
FROM as_components/ubuntu_base
SHELL ["/bin/bash", "-c"]
RUN apt-get -y update && apt-get -y install curl gnupg
RUN set -o pipefail && \
    curl -fsSL https://bazel.build/bazel-release.pub.gpg | \
      gpg --dearmor > bazel.gpg && \
    mv bazel.gpg /etc/apt/trusted.gpg.d/ && \
    echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" | \
      tee /etc/apt/sources.list.d/bazel.list
# Updates the new Bazel repository. Installs bazel, g++, gettext (for envsubst),
# nginx-light and libcurl4-openssl-dev (libcurl with development files).
RUN apt-get -y update && apt-get -y install bazel g++ gettext nginx-light \
    libcurl4-openssl-dev
# Retrieves the googletest source code and installs it.
RUN curl -fsSL https://github.com/google/googletest/tarball/release-1.10.0 \
      > temp_repo_tarball.tar.gz && \
    tar --extract -f temp_repo_tarball.tar.gz --gunzip && \
    cd *googletest* && \
    mv ${PWD} /usr/local/src/googletest && \
    cd / && \
    rm temp_repo_tarball.tar.gz
# Retrieves the source code of simple_bazel_cpp_toolchain and installs it.
RUN curl -fsSL https://github.com/adambreland/cpp-simple_bazel_cpp_toolchain/tarball/master \
      > temp_repo_tarball.tar.gz && \
    tar --extract -f temp_repo_tarball.tar.gz --gunzip && \
    cd *cpp-simple_bazel_cpp_toolchain* && \
    mv ${PWD} /usr/local/src/simple_bazel_cpp_toolchain && \
    cd / && \
    rm temp_repo_tarball.tar.gz
