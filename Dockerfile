FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV CLANG_VERSION=15

RUN apt-get -y update && apt-get -y install \
    build-essential \
    clang-${CLANG_VERSION} \
    clang-format-${CLANG_VERSION} \
    clang-tidy-${CLANG_VERSION} \
    cmake \
    doxygen \
    git \
    pkg-config \
    zlib1g-dev \
    libelf-dev \
    libdwarf-dev \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# 设为默认编译器
ENV CC=clang-${CLANG_VERSION}
ENV CXX=clang++-${CLANG_VERSION}

WORKDIR /workspace
CMD ["/bin/bash"]
