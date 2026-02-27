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
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli build-index \"\$(cat misc/configs/build-test-index.json)\""

# In-memory
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli build-index \"\$(cat misc/configs/build-test-index-mem.json)\""
```

### Build index from a gzipped stream (stdin)
You can build an index directly from a gzipped RDF file by unzipping and piping it to the index builder. Use `"path": "-"` in your JSON config to indicate stdin:

```bash
# Example: Build index from a gzipped NTriples file using stdin
gunzip -c misc/test-simple.nt.gz | \
  docker run --rm --user root -i -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine \
  sh -c "/qlever/qlever-cli build-index \"\$(cat misc/configs/build-test-index-stdin.json)\""
```

This will read the uncompressed RDF data from stdin and build the index as usual.

### Get index stats
```bash
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli stats ./databases/OSTT"
```

### Query the index
The query command takes a path to the index without suffixes (eg. `./databases/OSTT`) and a SPARQL 1.1 query.

*The supported response times for the server: application/sparql-results+json, application/sparql-results+xml, application/qlever-results+json, text/tab-separated-values, text/csv, text/turtle, application/n-triples, application/octet-stream*

```bash
# Example 1 - count all triples:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }'"

# Example 2 - count all triples - result as CSV:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }' csv"

# Example 3 - 10 entity mentions:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?s qcy:mentions ?o . } LIMIT 10'"

# Example 4 - 10 resolved entities and the documents they are about:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?frag qcy:mentions ?em . ?em qcy:resolvesTo ?canonical } LIMIT 10'"

# Example 5 - CONSTRUCT as raw output
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/OSTT 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt"

# Example 6 - CONSTRUCT to file (size beyond memory limits)
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file ./databases/OSTT 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt /workspace/res.nt"

# Example 7 - DESCRIBE as raw output
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/test 'DESCRIBE <http://example.org/subject1>' nt"

# Example 8 - ASK as raw output
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/test 'ASK WHERE { <http://example.org/subject1> ?p ?o }'"
```

### Named queries
A query can be tagged with a name for later use (NB! cache is not persisted so this doesn't make much sense in the given use case):
```bash
# 1. ADD QUERY NAMED "fc-mentions"
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/OSTT 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT ?fc ?m ?val WHERE { ?fc a qcy:FileContent ; qcy:containsFragment*/qcy:mentions ?m . ?m a qcy:EntityMention ; qcy:value ?val }' csv fc-mentions"

# 2. Execute "fc-mentions" query
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/OSTT 'SELECT ?fc ?m ?val WHERE { SERVICE ql:cached-result-with-name-fc-mentions {} } LIMIT 5' csv"
```

### Update queries
In order for this to work we had to extend `src/libqlever` with an update query function. I uses the same logic as the server and stores the delta triples in `<index_name>.update-triples`. These are then included in all query evaluations in the future. Therefore, it's not as fast to query over the delta triples as it is to query data in the original index. The difference is quite significant in the count query demonstrated below (almost 13 times slower to evaluate over the new index with 245k delta triples):

```bash
# 1. Count all (9,003,298 on NEST in 0.386)
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/NEST 'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'"

# 2. Run first update query (3.225 on NEST)
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli update ./databases/NEST 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> INSERT { ?a a ?sc } WHERE { ?a a ?cl . ?cl rdfs:subClassOf ?sc }'"

# 3. Count all (9,248,305 on NEST in 4.885)
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/NEST 'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'"
```

### Serialize
The serialize command allows dumping the whole database in either nt or nq format.
In a test, a 25M triples file was serialized as gzipped .nt in 3:38.74 (3:01.85 without gzipping).

```bash
# As NTriples
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli serialize ./databases/test nt"

# As NTriples -> stream to file
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli serialize ./databases/test nt /workspace/test.nt"

# As NTriples -> stream to file and gzip
docker run --rm --user root -gz $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli serialize ./databases/test nt /workspace/test.nt.gz"

# As NQuads
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli serialize ./databases/test nq"
```

### Qlever shortcomings
- Doesn't support RDF* or RDF 1.2 yet (https://github.com/ad-freiburg/qlever/issues/2169). Won't even load an NQuads file that has RDF* in it.

## Testing

Tests are compiled and executed as part of the normal Alpine build. The build fails if any test fails:

```bash
docker build -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .
```

## Merge main repo
```bash
git remote add upstream https://github.com/ad-freiburg/qlever.git
git fetch upstream
git merge upstream/master
```

## Build
Alpine image: `docker build -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .`
Ubuntu image: `docker build -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu .`
Debian image: `docker build -f Dockerfiles/Dockerfile.cli-only.debian -t qlever-cli:debian .`

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
```dockerfile
# In your application's Dockerfile
FROM your-app-base:latest

# Copy just the binary from the QLever image
COPY --from=qlever-cli:alpine /qlever/qlever-cli /usr/local/bin/qlever-cli

# Or from QAECY's artefact registry on GCP
# COPY --from=europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-x86_64 /qlever/qlever-cli /usr/local/bin/qlever-cli

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

### Load speed
NEST with raw texts (10,234,017 quads).

#### Comparing loading methods
Piping gzip and then loading: 1:02.52
Loading non gzipped: 1:11.90
Splitting in chunks of 1M lines (13 files) and loading them all: 1:11.09

#### Comparing loading vocabulary types (all with 1M line chunks)
on-disk-compressed: 1:11.09
in-memory-compressed: 1:10.77
in-memory-uncompressed: 1:16.13
on-disk-compressed-geo-split: 1:14.61

NEST without raw texts (10,201,499 quads).
This is faster, probably because the text processing is less demanding. But it's not so significant.

Loading gzipped: 59.145
Loading non gzipped: 54.311

#### Inference query

```bash
# 1. super-type through rdfs:subClassOf
# <a> a <Car> . <Car> rdfs:subClassOf <Vehicle>
# --> <a> a <Vehicle>
# NEST: executionTimeMs:8665, Total triples: 886,529
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file ./databases/NEST 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> CONSTRUCT { ?a a ?sc } WHERE { ?a a ?cl . ?cl rdfs:subClassOf ?sc }' nt /workspace/implicit1.nt"

# 2. super-relationship through rdfs:subPropertyOf
# <m1> qcy:hasAddress <m2> . qcy:hasAddress rdfs:subPropertyOf qcy:relatedEntity
# --> <m1> qcy:relatedEntity <m2>
# NEST: executionTimeMs:14161, Total triples: 1,298,508
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file ./databases/NEST 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> CONSTRUCT { ?s ?sp ?o } WHERE { ?s ?p ?o . ?p rdfs:subPropertyOf ?sp }' nt /workspace/implicit2.nt"

# 3. qcy:about through FileContent and Fragment mentionings
# <fileContent> qcy:about <canonical> 
# <fragment> qcy:about <canonical>
# NEST: executionTimeMs:2922, Total triples: 209,830
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file ./databases/NEST 'PREFIX qcy: <https://dev.qaecy.com/ont#> CONSTRUCT { ?fc qcy:about ?c . } WHERE { ?fc qcy:containsFragment*/qcy:mentions ?m . ?m qcy:resolvesTo ?c . }' nt /workspace/implicit3.nt"

# 4. mention relationships to canonical relationships
# <m1> qcy:relatedEntity <m2> . <m1> qcy:resolvesTo <c1> . <m2> qcy:resolvesTo <c2>
# --> <c1> qcy:relatedEntity <c1>
# NEST: executionTimeMs:255, Total triples: 6,994
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file ./databases/NEST 'PREFIX qcy: <https://dev.qaecy.com/ont#> CONSTRUCT { ?c1 ?p ?c2 } WHERE { ?m1 ?p ?m2 . ?m1 qcy:subPropertyOf*/qcy:relatedEntity ?m2 . ?m1 qcy:resolvesTo ?c1 . ?m2 qcy:resolvesTo ?c2 }' nt /workspace/implicit4.nt"

# 5. Implicitly contained fragments (fragments of fragments)
# <f1> qcy:containsFragment <f2> . <f2> qcy:containsFragment <f3>
# --> <f1> qcy:implicitlyContainsFragment <f3>
# NEST: executionTimeMs:6, Total triples: 0
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file ./databases/NEST 'PREFIX qcy: <https://dev.qaecy.com/ont#> CONSTRUCT { ?fc qcy:implicitlyContainsFragment ?f } WHERE { ?fc qcy:containsFragment ?f MINUS{?fc qcy:containsFragment ?f} }' nt /workspace/implicit5.nt"

# Implicit: 2,401,861
# Explicit: 9,003,298
# Total:   11,405,159
# Expansion: 27 %

# Total time: 26009 ms
# 92,3 triples/ms
# ~92,350 triples/s

docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query ./databases/NEST 'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'"
```
