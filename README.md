# QLever

This is a QAECY version of the Qlever quad store. It extends the existing work with a CLI tool that allows querying a dataset as an embedded database.

## Build, test and extract binary

If following passes, these are verified:

1. **Compilation** — `qlever-cli`, `qlever-index`, and all test binaries compile and link successfully.
2. **Unit tests** — `CliUtilsTest` and `CliUtilsRdfTest` pass (stream suppression, query type detection, RDF output utils, index builder utils). The Docker image is only produced if these pass.
3. **E2E integration tests** — the actual CLI binary is exercised end-to-end in Docker containers, covering:
   - `build-index` — create an index from RDF data (triples and quads)
   - `query` — SELECT queries with CSV output to verify data
   - `write` — insert triples/quads from stdin
   - `write --graph` — insert triples into a named graph
   - `delete` — delete triples/quads from stdin
   - `binary-rebuild` — merge delta triples into a new index, then query the rebuilt index
   - `update` — SPARQL UPDATE (INSERT DATA / DELETE DATA)
   - Correctness checks after every mutation (query to confirm inserts, deletes, and rebuilds)
4. **Binary extraction** — a working `qlever-cli` binary is copied to the current directory.

If the command completes successfully, all CLI commands work and you have a ready-to-use binary.

### Alpine
```bash
# 1. Build + unit tests + produce runtime image
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine-test .

# 2. Extract the binary (required for e2e tests — mounted at /workspace/qlever-cli)
docker run --rm --entrypoint="" qlever-cli:alpine-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli

# 3. Run e2e integration tests (inside a linux/amd64 container with the correct runtime libs)
docker compose -f docker-compose.cli-alpine.yml build test-runner
docker compose -f docker-compose.cli-alpine.yml run --rm test-runner

# 1., 2. and 3.
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine-test . && \
docker run --rm --entrypoint="" qlever-cli:alpine-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli && \
docker compose -f docker-compose.cli-alpine.yml build test-runner && \
docker compose -f docker-compose.cli-alpine.yml run --rm test-runner
```

### Ubuntu
```bash
# 1. Build + unit tests + produce runtime image
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu-test .

# 2. Extract the binary
docker run --rm --entrypoint="" qlever-cli:ubuntu-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli

# 3. Run e2e integration tests
# Note: Ubuntu uses the same Alpine-based test-runner container (see docker-compose.cli-alpine.yml).
# Ensure qlever-cli has been extracted to the repo root (step 2) before running.
docker compose -f docker-compose.cli-alpine.yml build test-runner
docker compose -f docker-compose.cli-alpine.yml run --rm test-runner

# 1., 2. and 3.
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu-test . && \
docker run --rm --entrypoint="" qlever-cli:ubuntu-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli && \
docker compose -f docker-compose.cli-alpine.yml build test-runner && \
docker compose -f docker-compose.cli-alpine.yml run --rm test-runner
```

## Use

After extracting the binary (step 3 above), run `./qlever-cli --help` to see all available commands.
For annotated usage examples see [README_examples.md](README_examples.md).

## Merge main repo

```bash
git remote add upstream https://github.com/ad-freiburg/qlever.git
git fetch upstream
git merge upstream/master
```

If there are merge conflicts use the following prompt with an LLM to resolve:
```
This repo extends the QLever quad store (https://github.com/ad-freiburg/qlever)
with a CLI tool (`qlever-cli`). The upstream repo is the source of truth for all
engine/parser/util/test/workflow code. Resolve all merge conflicts with the
following rules:

1. README.md — always keep OUR version (this repo's README overrides upstream).

2. All other conflicted files — take UPSTREAM (theirs) as the base, then
   re-apply the following repo-specific additions if they were lost:

   CMakeLists.txt
   - add_subdirectory(src/cli-utils) alongside the other add_subdirectory calls
   - add_executable(qlever-cli src/QleverCliMain.cpp) with
     qlever_target_link_libraries(qlever-cli cliUtils engine index parser util
     ${CMAKE_THREAD_LIBS_INIT} Boost::program_options compilationInfo global)
     placed just before the CPack section

   test/CMakeLists.txt
   - addLinkAndDiscoverTest(CliUtilsTest cliUtilsLight)
   - addLinkAndDiscoverTest(CliUtilsRdfTest cliUtils)

   .gitignore — append after the upstream content:
   - build-alpine/
   - e2e-cli/test-db-extended/
   - /*.nt /*.nq /*.ttl /*.trig /*.nq.gz
   - .DS_Store / *.DS_Store
   - .claude / .claude/settings.json

   src/parser/RdfParser.cpp — two RDF* detection hunks must be present:
   a) In TurtleParser<T>::iriref(), immediately after the
      `if (!ql::starts_with(view, '<')) { return false; }` guard, add:
        if (view.size() > 1 && view[1] == '<') {
          raise("Found RDF* syntax ('<<')...");
        }
   b) In RdfStreamParser<T>::getLineImpl(), inside the
      `if (byteVec_.size() > BZIP2_MAX_TOTAL_BUFFER_SIZE)` block, before the
      generic AD_LOG_ERROR, add:
        if (d.size() > 1 && d[0] == '<' && d[1] == '<') {
          throw std::runtime_error("Found RDF* syntax ('<<')...");
        }
   The error messages must contain the string "RDF*" (the e2e test checks this).

   src/util/File.h — the `openFromFilePointer` public method must be present.
   Add it at the top of the `public:` section (before the default constructor):
     // Open from an existing FILE* (e.g., stdin). Does not take ownership of
     // the FILE* — the caller is responsible for not closing it independently.
     bool openFromFilePointer(FILE* file) {
       if (!file) { return false; }
       file_ = file;
       name_ = "<stdin>";
       return true;
     }
   The class must have `public:` before this method and `private:` before the
   `using string = std::string;` member (upstream has `private:` first, which
   must be swapped). Without this the index builder cannot read from stdin
   (producing "No such device or address" on /dev/stdin inside Docker).

   src/parser/ParallelBuffer.cpp — the `open()` method must handle "-" as stdin.
   Replace `file_.open(filename, "r");` with:
     if (filename == "-") {
       file_.openFromFilePointer(stdin);
     } else {
       file_.open(filename, "r");
     }
   This allows `build-index` to accept `-` as the input file path and read data
   piped to stdin instead of failing with "No such device or address".

   src/QleverCliMain.cpp — in `executeWriteOrDelete`, do NOT convert "-" to
   "/dev/stdin". The line must be:
     std::string actualInputFile = inputFile;  // "-" is handled by ParallelBuffer
   (NOT: `(inputFile == "-") ? "/dev/stdin" : inputFile`)
   Without this, `write -` and `delete -` fail on Alpine Docker where
   /dev/stdin is not fopen()-able as a regular file.
```

## Build

Alpine image: `docker build -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .`
Alpine image staged: `docker compose -f docker-compose.cli-alpine.yml up`
Ubuntu image: `docker build -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu .`
Debian image: `docker build -f Dockerfiles/Dockerfile.cli-only.debian -t qlever-cli:debian .`

### Controlling resource usage

There are three levels of memory/CPU control: compile-time, Docker container, and CLI runtime.

```bash
# BUILD_JOBS — limit parallel compiler processes (default: all cores via nproc)
docker build --build-arg BUILD_JOBS=4 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .

# --memory / --cpu-quota — cap the Docker container's total RAM and CPU during build
docker build --build-arg BUILD_JOBS=4 --memory=4g --cpu-quota=200000 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .

# BUILD_JOBS via environment variable (docker compose)
BUILD_JOBS=4 docker compose -f docker-compose.cli-alpine.yml up

# --allocator-memory-gb — limit qlever-cli runtime working memory for queries/updates (default: 4 GB)
qlever-cli --allocator-memory-gb 2 query ./databases/myindex "SELECT * WHERE { ?s ?p ?o } LIMIT 10"

# QLEVER_MEMORY_LIMIT_GB — same as above but via environment variable (--allocator-memory-gb takes precedence)
QLEVER_MEMORY_LIMIT_GB=2 qlever-cli query ./databases/myindex "SELECT * WHERE { ?s ?p ?o } LIMIT 10"
```

| Parameter | When | What it controls | Default |
|---|---|---|---|
| `BUILD_JOBS` | `docker build` | Parallel compiler processes | All cores |
| `--memory` | `docker build/run` | Docker container RAM hard cap (OS kills if exceeded) | Unlimited |
| `--cpu-quota` | `docker build/run` | Docker container CPU cap | Unlimited |
| `--allocator-memory-gb` | `qlever-cli` runtime | Query/update working memory via `AllocatorWithLimit` (clean error if exceeded) | 4 GB |
| `QLEVER_MEMORY_LIMIT_GB` | `qlever-cli` runtime | Same as above, lower precedence | 4 GB |

**Note:** `--allocator-memory-gb` only caps heap allocations during query/update execution (joins, sorts, intermediate results, cache). It does **not** cap mmap'd index data or metadata loaded at startup. For full OOM prevention, combine with Docker's `--memory` as the hard ceiling. See [Troubleshooting](docs/troubleshooting.md#controlling-memory-usage) for details.

### Build and deploy

```bash
# alpine x86_64
docker buildx build --platform linux/amd64 \
  -f Dockerfiles/Dockerfile.cli-only.alpine \
  -t europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-x86_64 \
  --push .

# ubuntu x86_64
docker buildx build --platform linux/amd64 \
  -f Dockerfiles/Dockerfile.cli-only.ubuntu \
  -t europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:ubuntu-x86_64 \
  --push .

# alpine aarch64
docker buildx build \
  -f Dockerfiles/Dockerfile.cli-only.alpine \
  -t europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-aarch64 \
  --push .

# ubuntu arm64
docker buildx build --platform linux/arm64 \
  -f Dockerfiles/Dockerfile.cli-only.ubuntu \
  -t europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:ubuntu-aarch64 \
  --push .
```

### Use in your app

See [README_examples.md](README_examples.md) for a full list of CLI examples using the extracted binary, including a Python usage example.
