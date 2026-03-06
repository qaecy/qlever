# Troubleshooting

If you have problems, try rebuilding QLever in debug mode (e.g. by changing the
`cmake` line in the `Dockerfile` to `cmake -DCMAKE_BUILD_TYPE=Debug
-DLOGLEVEL=DEBUG` or [building natively](docs/native_setup.md)),
Then rebuild your index with the newly build docker container and
`qlever-index` executable.
The release build assumes machine written words- and docsfiles and omits sanity
checks for the sake of speed.

## Run End-to-End Tests

QLever includes a simple mechanism for testing the entire system,
from building an index to running queries.

In fact the End-to-End Test is run on Travis CI for every commit and Pull
Request. It is also useful for local development, as it allows you to
quickly test if something is horribly broken.

Just like QLever itself the End-to-End Tests can be executed from a previously
built QLever docker container.

**Note**: If you encounter permission issues e.g. if your UID is not 1000 or you
are using docker with user namespaces, add the flag `-u root` to your `docker
run` command or do a `chmod -R o+rw e2e_data`

    docker build -t qlever .
    docker run -it --rm -v "$(pwd)/e2e_data:/app/e2e_data/" --name qlever-e2e --entrypoint e2e/e2e.sh qlever

## Converting old Indices For current QLever Versions

When we make changes to the way the index meta data (e.g. offsets of relations
within the permutations) is stored, old indices may become incompatible with new
executables.
For example, some old index builds with 6 permutations will not work directly
with newer QLever versions, while 2 permutation indices do work.
In either case incompatible versions are detected during startup.

For these cases, we provide a converter which only modifies the
meta data without having to rebuild the index. Run `MetaDataConverterMain
<index-prefix>` in the same way as as running `qlever-index`.

This will not automatically overwrite the old index but copy the permutations
and create new files with the suffix `.converted` (e.g.
`<index-prefix>.index.ops.converted` These suffixes have to be removed manually
in order to use the converted index (rename to `<index-prefix>.index.ops` in our
example).

**Please consider creating backups of the "original" index files before
overwriting them**.

Please note that for 6 permutations the converter also builds new files
`<index-prefix>.index.xxx.meta-mmap` where parts of the meta data of OPS and OSP
permutations will be stored.


## High RAM Usage During Runtime

QLever uses an on-disk index and usually requires only a moderate amount of RAM.
For example, it can handle the full Wikidata KB + Wikipedia, which is about 1.5 TB
of index with less than 46 GB of RAM

However, there are data layouts that currently lead to an excessive
amount of memory being used:

* **The size of the KB vocabulary**. Using the -l flag while building the index
causes long and rarely used strings to be externalized to
disk. This saves a significant amount of memory at little to no time cost for
typical queries. The strategy can be modified to be more aggressive.

* **Building all 6 permutations over large KBs** (or generally having a
permutation, where the primary ordering element takes many different values).
To handle this issue, the meta data of OPS and OSP are not loaded into RAM but
read from disk. This saves a lot of RAM with only little impact on the speed of
the query execution. We will evaluate if it's  worth also externalizing SPO and
SOP permutations in this way to further reduce the RAM usage, or to let the user
decide which permutations shall be stored in which format.

## QLever CLI: Build, Test & Deploy

### Build & Test Commands

```bash
# 1. Run unit tests (lightweight + RDF, builds full qlever-cli binary)
docker build -f Dockerfiles/Dockerfile.cli-test -t qlever-cli-test . && docker run --rm qlever-cli-test

# 2. Build production CLI image (Alpine — required for Alpine-based downstream containers)
docker build --platform linux/amd64 --no-cache -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:local .

# 3. Build downstream app image (example)
cd ~/dev/qaecy-monorepo && npm run services:build accessors-qlever
```

### Alpine vs Ubuntu: Which Dockerfile to Use

The CLI has two production Dockerfiles:

- `Dockerfiles/Dockerfile.cli-only.alpine` — builds against **musl** (Alpine libc)
- `Dockerfiles/Dockerfile.cli-only.ubuntu` — builds against **glibc** (Ubuntu libc)

**You must match the CLI binary to the downstream container's base image.**
If the downstream container uses Alpine (e.g. `node:22-alpine3.22`), you **must**
use `Dockerfile.cli-only.alpine`. A glibc-linked binary copied into an Alpine
container will fail with:

```
rosetta error: failed to open elf at /lib64/ld-linux-x86-64.so.2
```

This is because Alpine uses musl and does not have the glibc dynamic linker.

### `_exit()` and QLever Destructor Hangs

The CLI uses `_exit(0)` (via `flushAndExit()`) instead of `return` in all
functions that create a `QleverCliContext`. This is intentional.

QLever's `QleverCliContext` destructor triggers background thread joins
(DeltaTriplesManager, etc.) that do not complete cleanly. On native Linux this
causes a hang; under Rosetta emulation (Docker on Apple Silicon) it causes a
crash. Since the CLI is a short-lived process and all output has been flushed
before `_exit()`, the OS safely reclaims all resources.

**Do not replace `flushAndExit()` with `return`** without first confirming
that QLever's destructors complete cleanly.

### `update-triples` Version Mismatch

If you see an error like:

```
Assertion `version == formatVersion` failed.
The format version for serialized triples [...] is 1 but you tried to read
serialized triples with version 0
```

This means the `full.update-triples` file on the mounted volume was written by
an older QLever version. The file lives on the **persistent volume**, not inside
the Docker image — rebuilding the image does not remove it.

**Fix:** Delete the stale file and re-apply any SPARQL UPDATEs:

```bash
rm /mnt/qlever/<index-path>/full.update-triples
```

### Query Type Detection (`detectQueryType`)

The CLI uses `cli_utils::detectQueryType()` (in `src/cli-utils/QueryTypeDetect.h`)
to determine the SPARQL verb (SELECT, CONSTRUCT, DESCRIBE, ASK, etc.). This
function correctly skips PREFIX, BASE declarations and comment lines.

**Do not replace this with a naive "first word" parser** (e.g. `trimAndUpper`).
Queries from the downstream API typically carry PREFIX declarations, and a naive
parser would return "PREFIX" instead of the actual verb, breaking
CONSTRUCT/DESCRIBE dispatch.

---

## Controlling memory usage

### At build time (Docker image compilation)

By default, compilation uses all CPU cores (`nproc`), which can cause the OOM killer to terminate sibling containers.

```bash
# BUILD_JOBS — limit parallel compiler processes (each can use ~2 GB; set to available RAM / 2)
docker build --build-arg BUILD_JOBS=4 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .

# --memory — hard cap on Docker container RAM during build
docker build --build-arg BUILD_JOBS=4 --memory=8g -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .

# docker compose — set BUILD_JOBS via environment variable
BUILD_JOBS=4 docker compose -f docker-compose.cli-alpine.yml up
```

All Dockerfiles (`Dockerfile`, `Dockerfile.cli-only.alpine`, `Dockerfile.cli-only.ubuntu`) support `BUILD_JOBS`.

### At runtime (CLI query/update execution)

`--allocator-memory-gb` sets a budget on QLever's internal `AllocatorWithLimit`, which tracks heap allocations made during query/update execution (joins, sorts, intermediate results, cache). When the budget is exceeded, the operation fails with a clean `AllocationExceedsLimitException` error instead of the OS killing the process.

```bash
# --allocator-memory-gb — set working memory limit (default: 4 GB)
qlever-cli --allocator-memory-gb 2 query ./databases/myindex "SELECT ..."
qlever-cli --allocator-memory-gb 8 update ./databases/myindex "INSERT DATA { ... }"

# QLEVER_MEMORY_LIMIT_GB — same, via environment variable (--allocator-memory-gb takes precedence)
QLEVER_MEMORY_LIMIT_GB=2 qlever-cli query ./databases/myindex "SELECT ..."
```

**What this does and does not prevent:**

| Controlled by `--allocator-memory-gb` | NOT controlled (outside this flag) |
|---|---|
| Query/update working memory (joins, sorts, intermediate results) | Index data (mmap'd from disk, managed by OS page cache) |
| Cache entries | Fixed structures (vocabulary, metadata) loaded at startup |
| Sort spill-to-disk decisions | Total process RSS |

This means `--allocator-memory-gb` alone does **not** fully prevent OOM. If the index itself is large, the mmap'd pages plus fixed structures can exhaust available RAM before any query runs. For full OOM prevention, combine it with Docker's `--memory` flag as a hard ceiling:

```bash
# --memory is the hard cap (OS kills if exceeded), --allocator-memory-gb is the soft cap (clean error)
docker run --memory=4g ... qlever-cli --allocator-memory-gb 2 query ./databases/myindex "SELECT ..."
```

A good rule of thumb: set `--allocator-memory-gb` to about half the container's `--memory` to leave room for index data and OS overhead.

## Note when using Docker on Mac or Windows

When building an index, QLever will create scratch space for on-disc sorting.
This space is allocated as a large 1 TB sparse file. On standard Linux file
systems such as Ext4 sparse files are optimized and this will only take as much
disk space as is actually used.

With some systems such as Docker on Mac or when using unsupported file
systems such as NTFS or APFS, this may lead to problems as these do not
properly support sparse files.
