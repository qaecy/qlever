# The Ubuntu that builds libqlever_c and qlever-cli, with their runtime libraries and a Rust toolchain.
# See run.sh.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential pkg-config curl ca-certificates git clang libclang-dev \
    libicu74 libssl3 libzstd1 libjemalloc2 libgomp1 \
    libboost-program-options1.83.0 libboost-iostreams1.83.0 libboost-url1.83.0 libboost-container1.83.0 \
    && rm -rf /var/lib/apt/lists/*
ARG RUST_VERSION=1.98.1
ENV RUSTUP_HOME=/usr/local/rustup CARGO_HOME=/usr/local/cargo PATH=/usr/local/cargo/bin:$PATH
RUN curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --default-toolchain $RUST_VERSION
