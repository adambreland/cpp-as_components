# Container image: as_components/fcgi/external_test_base

# This Dockerfile defines the container image which is used as the base for
# the as_components/fcgi/external_test image. The current image includes all
# external dependencies which are needed for the external tests.

FROM as_components/ubuntu_base
SHELL ["/bin/bash", "-c"]
# Installs libcurl.
RUN apt-get update -y && apt-get install -y libcurl4
