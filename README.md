# QLever

This is a QAECY version of the Qlever quad store. It extends the existing work with a CLI tool that allows querying a dataset as an embedded database.

## Use
The CLI tool binary is built inside a Docker container for compatibility reasons. Therefore all commands are run through the container.

## Qlever CLI
The qlever CLI is what is added in this repo and it makes querying and serializing the index possible through a CLI + offers a more convenient way build the index. To see the available commands, run `docker run --rm qlever-cli:alpine --help`

### Build index
The index configuration is described in a JSON file that looks like the one you find in `misc/configs/build-test-index.json`. This config loads the very small nquads file `misc/test-simple.nq` (`test.nq contains RDF* and will currently fail`).

An important setting is the vocabulary type. Here are the 5 available types:
- in-memory-uncompressed
- on-disk-uncompressed
- in-memory-compressed
- on-disk-compressed (default)
- on-disk-compressed-geo-split (needed for GeoSPARQL!)

Building an index from the file `misc/test-simple.nq` is handled by the following CLI command:

```bash
# Persistent
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain build-index \"\$(cat misc/configs/build-test-index.json)\""

# In-memory
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain build-index \"\$(cat misc/configs/build-test-index-mem.json)\""
```

### Build index from a gzipped stream (stdin)
You can build an index directly from a gzipped RDF file by unzipping and piping it to the index builder. Use `"path": "-"` in your JSON config to indicate stdin:

```bash
# Example: Build index from a gzipped NTriples file using stdin
gunzip -c misc/test-simple.nt.gz | \
  docker run --rm --user root -i -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine \
  sh -c "/qlever/QleverCliMain build-index \"\$(cat misc/configs/build-test-index-stdin.json)\""
```

This will read the uncompressed RDF data from stdin and build the index as usual.

### Get index stats
```bash
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain stats ./databases/OSTT"
```

### Query the index
The query command takes a path to the index without suffixes (eg. `./databases/OSTT`) and a SPARQL 1.1 query.

*The supported response times for the server: application/sparql-results+json, application/sparql-results+xml, application/qlever-results+json, text/tab-separated-values, text/csv, text/turtle, application/n-triples, application/octet-stream*

```bash
# Example 1 - count all triples:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }'"

# Example 2 - count all triples - result as CSV:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }' csv"

# Example 3 - using JSON input:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query-json \"\$(cat misc/configs/query-1.json)\""

# Example 4 - 10 entity mentions:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?s qcy:mentions ?o . } LIMIT 10'"

# Example 5 - 10 resolved entities and the documents they are about:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?frag qcy:mentions ?em . ?em qcy:resolvesTo ?canonical } LIMIT 10'"

# Example 6 - CONSTRUCT as raw output
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/OSTT 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt"

# Example 7 - CONSTRUCT to file (size beyond memory limits)
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query-to-file ./databases/OSTT 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt /workspace/res.nt"
```

**Note: UPDATE queries (INSERT, DELETE) are not supported in CLI mode. These require the QLever server interface for proper execution.**

### Serialize
The serialize command allows dumping the whole database in either nt or nq format.
In a test, a 25M triples file was serialized as gzipped .nt in 3:38.74 (3:01.85 without gzipping).

```bash
# As NTriples
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nt"

# As NTriples -> stream to file
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nt /workspace/test.nt"

# As NTriples -> stream to file and gzip
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nt /workspace/test.nt.gz"

# As NQuads
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nq"
```

### Qlever shortcomings
- Doesn't support RDF* or RDF 1.2 yet (https://github.com/ad-freiburg/qlever/issues/2169). Won't even load an NQuads file that has RDF* in it.

## Build
Ubuntu image: `docker build -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu .`
Alpine image: `docker build -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .`

### Build and deploy
```bash
# x86_64
docker buildx build --platform linux/amd64 \
  -f Dockerfiles/Dockerfile.cli-only.alpine \
  -t europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-x86_64 \
  --push .

# aarch64
docker buildx build \
  -f Dockerfiles/Dockerfile.cli-only.alpine \ 
  -t europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-aarch64 \
  --push .
```

### Use in your app
```dockerfile
# In your application's Dockerfile
FROM your-app-base:latest

# Copy just the binary from the QLever image
COPY --from=qlever-cli:alpine /qlever/QleverCliMain /usr/local/bin/qlever-cli

# Or from QAECY's artefact registry on GCP
# COPY --from=europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-x86_64 /qlever/QleverCliMain /usr/local/bin/qlever-cli

# Install only the runtime dependencies QLever needs
RUN apk add --no-cache \
    libstdc++ \
    libgcc \
    icu-libs \
    openssl \
    zstd-libs \
    zlib \
    jemalloc \
    boost-program_options \
    boost-iostreams \
    boost-system \
    boost-url

# Your app code
COPY . /app
WORKDIR /app

# Now you can use qlever-cli directly
RUN qlever-cli --help
```

Example use in app:
```python
import subprocess

# Execute QLever commands directly
result = subprocess.run([
    'qlever-cli', 'query', 
    './databases/mydb', 
    'SELECT * WHERE { ?s ?p ?o } LIMIT 10'
], capture_output=True, text=True)

data = json.loads(result.stdout)
```

### Todo
- Try loading a zipped baseline as a readable stream
- Check if quads persist after round trip (and how about RDF*?)
- Add CLI command for removing an index
- Test if loading an index with the same name will overwrite existing

### Thoughts on saturation
1. Build index for unsaturated database + schema
2. Do this for each saturation query (<n>) (CONSTRUCT)
    a. Run the query and append result to implicit_<n>.nt
    b. Cat implicit.nt + implicit_<n>.nt
    c. Load the triples in the store so new results are available for further saturation
    d. Continue with n++


### Image size
Ubuntu: 399 MB
Alpine: 267 MB