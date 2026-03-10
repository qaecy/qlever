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

# 2. Run e2e integration tests
cd e2e-cli && npm install && npx vitest run && cd ..

# 1. and 2.
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine-test . && \
cd e2e-cli && npm install && npx vitest run && cd ..

# 3. Extract the binary
docker run --rm --entrypoint="" qlever-cli:alpine-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli

# 1., 2. and 3.
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine-test . && \
cd e2e-cli && npm install && npx vitest run && cd .. && \
docker run --rm --entrypoint="" qlever-cli:alpine-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli
```

### Ubuntu
```bash
# 1. Build + unit tests + produce runtime image
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu-test .

# 2. Run e2e integration tests
cd e2e-cli && npm install && npx vitest run && cd ..

# 1. and 2.
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu-test . && \
cd e2e-cli && npm install && npx vitest run && cd ..

# 3. Extract the binary
docker run --rm --entrypoint="" qlever-cli:ubuntu-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli

# 1., 2. and 3.
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu-test . && \
cd e2e-cli && npm install && npx vitest run && cd .. && \
docker run --rm --entrypoint="" qlever-cli:ubuntu-test cat /qlever/qlever-cli > qlever-cli && chmod +x qlever-cli
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
