# QLever

This is a QAECY version of the Qlever quad store. It extends the existing work with a CLI tool that allows querying a dataset as an embedded database.

## Build and test

Whichever flow you use, passing means these are verified:

1. **Compilation** — `qlever-cli`, `qlever-index`, and the test binaries compile and link successfully.
2. **Unit tests** — `CliUtilsTest` and `CliUtilsRdfTest` pass (stream suppression, query type detection, RDF output utils, index builder utils).
3. **E2E integration tests** — the actual CLI binary is exercised end-to-end in Docker, covering:
   - `build-index` — create an index from RDF data (triples and quads), including from **stdin** (`"path": "-"`)
   - `query` — SELECT queries with CSV output to verify data
   - `write` / `write --graph` — insert triples/quads, optionally into a named graph
   - `delete` — delete triples/quads
   - `binary-rebuild` — merge delta triples into a new index, then query the rebuilt index
   - `update` — SPARQL UPDATE (INSERT DATA / DELETE DATA)
   - `write-view` / `load-view` — materialized views; `clone`; `serialize`; `stats`
   - RDF* input is rejected with a clear error
   - Correctness checks after every mutation

There are two flows. Use the **fast local flow** while developing; use the **release flow** when you need the deployable `linux/amd64` image and binary.

### Fast local flow (development)

Builds natively for your host architecture (no emulation) into `./build-alpine`, and is fully incremental — after the first build, a rebuild plus the whole test suite takes well under a minute. On Apple Silicon this is the difference between minutes and hours.

```bash
# 1. Toolchain image — compiler + dependencies only, no source built here (~30 s, once).
docker build --target deps -t qlever-builder:alpine -f Dockerfiles/Dockerfile.cli-only.alpine .

# 2. Compile into ./build-alpine (first run ~1 h; subsequent runs are incremental).
#    Builds qlever-cli, qlever-index, CliUtilsTest and CliUtilsRdfTest.
docker compose -f docker-compose.cli-alpine.yml run --rm builder

# 3. Unit tests.
docker compose -f docker-compose.cli-alpine.yml run --rm builder ./test/CliUtilsTest
docker compose -f docker-compose.cli-alpine.yml run --rm builder ./test/CliUtilsRdfTest

# 4. E2E suite against ./build-alpine/qlever-cli (~20 s, 81 tests).
docker compose -f docker-compose.cli-alpine.yml run --rm e2e-native

# 2., 3. and 4.
docker compose -f docker-compose.cli-alpine.yml run --rm builder && \
docker compose -f docker-compose.cli-alpine.yml run --rm builder ./test/CliUtilsTest && \
docker compose -f docker-compose.cli-alpine.yml run --rm builder ./test/CliUtilsRdfTest && \
docker compose -f docker-compose.cli-alpine.yml run --rm e2e-native
```

Limit parallelism with `BUILD_JOBS=4 docker compose -f docker-compose.cli-alpine.yml run --rm builder`.

The `e2e-native` service bind-mounts `./build-alpine/qlever-cli` over `/workspace/qlever-cli` (the path the spec files use) rather than overwriting the checked-in binary at the repo root, so this flow never touches it. `node_modules` is shadowed by an anonymous volume so an `npm install` here cannot clobber the architecture-specific tree a release run installed.

**This flow does not refresh the `qlever-cli` binary at the repo root** — that is a `linux/amd64` build and is produced by one of the two flows below.

### Incremental amd64 flow (a deployable binary without a full rebuild)

The release flow below recompiles everything inside the image, which takes hours under emulation. When only a few files changed and all that is needed is a fresh `linux/amd64` binary, this is the same incremental build as above but emulated, writing to its own `./build-alpine-amd64` directory so it never invalidates the native build's cache.

```bash
# 1. Toolchain image for amd64 (once). A separate tag from qlever-builder:alpine,
#    which is built for the host architecture.
docker build --platform linux/amd64 --target deps \
  -t qlever-builder:alpine-amd64 -f Dockerfiles/Dockerfile.cli-only.alpine .

# 2. Compile into ./build-alpine-amd64.
docker compose -f docker-compose.cli-alpine.yml run --rm builder-amd64

# 3. That binary is what `databases-qlever` copies as
#    data/binaries/qlever-cli_alpine-x86_64-<version>.
```

This skips the unit and e2e tests that the release flow runs as part of the image build, so run them against the native build first (steps 3 and 4 above). It is a shortcut for producing a binary, not a substitute for the release flow.

### Release flow (linux/amd64 image + deployable binary)

Compiles the whole source inside the image and only produces the image if the unit tests pass. On Apple Silicon this runs under emulation and takes a couple of hours.

#### Alpine
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

#### Ubuntu
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

After extracting the binary (step 2 of the release flow above), run `./qlever-cli --help` to see all available commands.
For annotated usage examples see [README_examples.md](README_examples.md).

When developing with the fast local flow, the equivalent binary is `./build-alpine/qlever-cli`.

### Passing a large query or update

`query`, `update`, `query-to-file` and `query-json` accept **`-`** in place of the
query text, meaning "read it from stdin":

```bash
./qlever-cli update /mnt/qlever/proj/full - < big-insert.ru
./qlever-cli query  /mnt/qlever/proj/full - nt < big-construct.rq
```

This is not a convenience. `execve` caps a single argument at `MAX_ARG_STRLEN` —
32 pages, i.e. **131072 bytes** on Linux, regardless of how large `ARG_MAX` is —
so past that the process fails with `E2BIG` before `main` runs and the caller sees
only an opaque spawn failure. 128 KiB is roughly 2,400 triples in an
`INSERT DATA`, which is not a large write:

| `INSERT DATA` size | via argv | via stdin |
| --- | --- | --- |
| 105 KiB (2,000 triples) | ✅ | ✅ |
| 132 KiB (2,500 triples) | ❌ `E2BIG` | ✅ |
| 276 KiB (5,000 triples) | ❌ `Argument list too long` | ✅ 5,000 inserted |

It does **not** avoid holding the query in memory — `SparqlParser` needs the whole
string — it removes an arbitrary hard ceiling. Bound the size of an update at the
HTTP layer, where it can be reported properly.

`-` never collides with the `write`/`delete` stdin convention: those read RDF from
stdin and take no query, while `query`/`update` take a query and read no data.

## Troubleshooting the test flows

**`rm: can't remove '.../test-db-extended': Directory not empty`** — macOS writes a `.DS_Store` into the bind-mounted test directory in between `rm` unlinking the children and calling `rmdir()`. The `beforeAll` in `e2e-cli/extended-commands.spec.ts` retries the removal to absorb this; if it still bites, close the folder in Finder or run `find e2e-cli -name .DS_Store -delete` first.

**`sh: syntax error: unexpected "&&"` from the `builder` service** — the compose `command` must be a literal block (`|`), not a folded one (`>`). In a folded scalar YAML keeps the newlines on lines indented deeper than the first content line, so continuation lines starting with `&&` stay on their own line.

**`COPY build-alpine/qlever-cli` fails in `Dockerfile.cli-test-image`** — `.dockerignore` is an allowlist (`*` plus `!src`, `!test`, …) and does not include `build-alpine/`, so that directory is not in the build context. The `e2e-native` service avoids this by bind-mounting the binary instead of copying it.

**`write -` / `delete -` and stdin** — with `QLEVER_DIRECT_EXEC=1` the harness rewrites a trailing ` -` into a temp file, because an anonymous pipe cannot be reopened via `/proc/self/fd/0` on Alpine. Those two commands therefore do **not** exercise the stdin path in e2e; `build-index` does, since its `-` sits inside the JSON config. Test `write -` against real stdin manually when touching the parser's file source.

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
   - e2e-cli/test-db-extended/ and e2e-cli/test-db-stdin/
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
      `if (byteVec_.size() > RDF_PARSER_MAX_TOTAL_BUFFER_SIZE().getBytes())`
      block, before the generic AD_LOG_ERROR, add:
        std::string_view unparsed = tok_.view();
        if (unparsed.size() > 1 && unparsed[0] == '<' && unparsed[1] == '<') {
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

   src/parser/AsyncBlockSource.cpp — the `FileBlockSource` constructor must
   handle "-" as stdin. Replace `file_.open(filename, "r");` with:
     if (filename == "-") {
       file_.openFromFilePointer(stdin);
     } else {
       file_.open(filename, "r");
     }
   This allows `build-index` to accept `-` as the input file path and read data
   piped to stdin instead of failing with "No such device or address".
   NOTE: this patch has moved before. Upstream replaced `ParallelBuffer` with
   `AsyncBlockSource` (boost::asio) in #3023; before that merge the branch lived
   in `ParallelFileBuffer` in `src/parser/ParallelBuffer.cpp`. If that file is
   gone, the "-" branch belongs wherever the parser's leaf file source now opens
   its file — do not resurrect the deleted file.

   src/QleverCliMain.cpp — in `executeWriteOrDelete`, do NOT convert "-" to
   "/dev/stdin". The line must be:
     std::string actualInputFile = inputFile;  // "-" handled by FileBlockSource
   (NOT: `(inputFile == "-") ? "/dev/stdin" : inputFile`)
   Without this, `write -` and `delete -` fail on Alpine Docker where
   /dev/stdin is not fopen()-able as a regular file.

   src/index/DeltaTriples.{h,cpp} — two hardening patches that make concurrent
   delta writes safe even when advisory locking is unavailable. Applying a delta
   is a read-modify-write of the *whole* file, so without these a second writer
   silently destroys the first one's triples:
   a) `writeToDisk()` must serialize to a UNIQUE temporary path
      (`<file>.tmp.<pid>.<counter>`), never a shared `<file>.tmp`. With a shared
      name, concurrent writers interleave bytes in one file and rename it into
      place, corrupting the delta outright.
   b) `DeltaTriples` must keep the `(st_dev, st_ino)` of the delta file as of
      the last read/write (`persistedFileIdentity_`, set in `readFromDisk()` and
      refreshed in `writeToDisk()`), and `writeToDisk()` must throw rather than
      clobber a file that no longer matches. Every write renames a fresh temp
      file over the target, so the inode changes on any write and the token is
      exact.
   Keep `DeltaTriplesManager::setFilenameForPersistentUpdates` passing `false`
   for `writeToDiskAfterRequest`; otherwise merely opening an index rewrites the
   delta and every reader becomes a writer.

3. Upstream also drifts its engine APIs between merges. These broke previously
   and are likely to move again — fix them at the call site rather than
   reverting upstream:

   - `RdfStreamParser` takes a `qlever::InputFileSpecification` plus an
     `ad_utility::MemorySize` blocksize (it creates its own I/O thread and block
     source). It used to take an already-opened buffer.
   - `TripleComponent::toValueId(...)` is now a free function in
     `index/TripleComponentConversions.h`: `toValueId(std::move(tc), indexImpl,
     localVocab)`.
   - `qlever::QueryPlan` (a tuple) was unified into the `qlever::PlannedQuery`
     class in `libqlever/QleverTypes.h`; `MaterializedViewsManager::
     writeViewToDisk` takes the latter.

   After resolving, verify with the fast local flow in "Build and test" above —
   it compiles and runs all 81 e2e tests in well under a minute once warm.
```

## Other image variants

See "Build and test" above for the two flows you normally want. These build a
CLI image for the host architecture, without running the tests:

Alpine image: `docker build -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .`
Ubuntu image: `docker build -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu .`

For an incremental build into `./build-alpine` instead of a self-contained
image, use the `builder` service (see the fast local flow above).

### Controlling resource usage

There are three levels of memory/CPU control: compile-time, Docker container, and CLI runtime.

```bash
# BUILD_JOBS — limit parallel compiler processes (default: all cores via nproc)
docker build --build-arg BUILD_JOBS=4 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .

# --memory / --cpu-quota — cap the Docker container's total RAM and CPU during build
docker build --build-arg BUILD_JOBS=4 --memory=4g --cpu-quota=200000 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .

# BUILD_JOBS via environment variable (docker compose incremental builder)
BUILD_JOBS=4 docker compose -f docker-compose.cli-alpine.yml run --rm builder

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
