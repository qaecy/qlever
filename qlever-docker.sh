qlever() {
  docker run --rm --user root \
    -v "$(pwd)":/workspace \
    -w /workspace \
    --entrypoint="" \
    qlever-cli:alpine-test \
    /qlever/qlever-cli "$@"
}