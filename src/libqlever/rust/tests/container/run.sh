#!/bin/sh
# Runs this crate's tests, the ignored ones included, against a real libqlever_c and qlever-cli, on the Ubuntu
# that builds them. Usage: run.sh [build directory holding lib/libqlever_c.so and qlever-cli] (default: build-ubuntu).
set -e
CRATE=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=$(cd "${1:-$CRATE/../../../build-ubuntu}" && pwd)
docker build -q -t qlever-embedded-tests "$(dirname "$0")" > /dev/null
JEMALLOC=/usr/lib/$(docker run --rm qlever-embedded-tests uname -m)-linux-gnu/libjemalloc.so.2
exec docker run --rm \
  -v "$CRATE":/crate -v "$BUILD":/build:ro \
  -v qlever-embedded-tests-target:/target -v qlever-embedded-tests-cargo:/usr/local/cargo/registry \
  -e CARGO_TARGET_DIR=/target \
  -e QLEVER_C_LIBRARY=/build/lib/libqlever_c.so -e QLEVER_CLI=/build/qlever-cli \
  -e LD_PRELOAD="$JEMALLOC" \
  -w /crate qlever-embedded-tests \
  cargo test -- --include-ignored
