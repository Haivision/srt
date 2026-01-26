# SRT Build Environment - Ubuntu 22.04 (Jammy)
# This Dockerfile can be used standalone or as part of a devcontainer setup

FROM ubuntu:22.04

# Set non-interactive mode for apt
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    # Build essentials
    build-essential \
    cmake \
    gcc \
    g++ \
    make \
    pkg-config \
    # SRT dependencies
    tclsh \
    libssl-dev \
    # Testing and code coverage
    gcovr \
    lcov \
    # Spell checking
    python3 \
    python3-pip \
    # Git for source control
    git \
    && rm -rf /var/lib/apt/lists/*

# Install codespell using pip
RUN pip3 install --no-cache-dir codespell

# Create a non-root user for development
ARG USERNAME=srt-dev
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && apt-get update \
    && apt-get install -y sudo \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME \
    && rm -rf /var/lib/apt/lists/*

# Set the default user
USER $USERNAME

# Set working directory
WORKDIR /srt_build_env

# Default command
CMD ["/bin/bash"]
