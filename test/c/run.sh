#!/bin/sh
# Builds a ten-triple index with qlever-cli and runs the C ABI test against it.
# Usage (from a build directory): sh ../test/c/run.sh
set -e
BUILD="${QLEVER_BUILD:-$(pwd)}"
DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT
i=0
while [ $i -lt 10 ]; do
  echo "<http://example.org/s$i> <http://example.org/p> <http://example.org/o$i> <http://example.org/g> ." >> "$DIR/data.nq"
  i=$((i + 1))
done
"$BUILD/qlever-cli" build-index "{\"index_name\": \"full\", \"index_directory\": \"$DIR\", \"input_files\": [{\"path\": \"$DIR/data.nq\", \"format\": \"nq\"}]}" > /dev/null 2>&1
"$BUILD/qlever_c_test" "$DIR/full" "$DIR"
