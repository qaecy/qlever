# QLever

This is a QAECY version of the Qlever quad store. It extends the existing work with a CLI tool that allows querying a dataset as an embedded database.

## Build
Ubuntu image: `docker build -f Dockerfiles/Dockerfile.cli-only.ubuntu -t qlever-cli:ubuntu .`
Alpine image: `docker build -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine .`

## Use
The CLI tool binary is built inside a Docker container for compatibility reasons. Therefore all commands are run through the container.

## Qlever CLI
The qlever CLI is what is added in this repo and it makes querying and serializing the index possible through a CLI + offers a more convenient way build the index. To see the available commands, run `docker run --rm qlever-cli:alpine --help`

### Build index
The index configuration is described in a JSON file that looks like the one you find in `misc/configs/build-test-index.json`. This config loads the very small nquads file `misc/test-simple.nq` (`test.nq contains RDF* and will currently fail`). 

Building an index from `misc/test-simple.nq` is then handled by the following CLI command:

```bash
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain build-index \"\$(cat misc/configs/build-test-index.json)\""
```

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

# Example 2 - 10 entity mentions:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?s qcy:mentions ?o . } LIMIT 10'"

# Example 3 - 10 resolved entities and the documents they are about:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?frag qcy:mentions ?em . ?em qcy:resolvesTo ?canonical } LIMIT 10'"

# Example 4 - CONSTRUCT as raw output
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query-to-file ./databases/OSTT 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt /workspace/res.nt"
```

**Note: UPDATE queries (INSERT, DELETE) are not supported in CLI mode. These require the QLever server interface for proper execution.**

### Serialize
The serialize command allows dumping the whole database in either nt or nq format.
In a test, a 25M triples file was serialized as .nt in 3:22.39 using a query approach
```bash
# As NTriples
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nt"

# As NTriples -> stream to file
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nt /workspace/test.nt"

# As NQuads
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain serialize ./databases/test nq"
```

### Qlever shortcomings
- Doesn't support RDF* or RDF 1.2 yet (https://github.com/ad-freiburg/qlever/issues/2169). Won't even load an NQuads file that has RDF* in it.


### Todo
- Build binary inside the container and save it so it can be used in any other container or build the image so it can be used as a base
- Try loading a zipped baseline
- Check if quads persist after round trip (and how about RDF*?)
- Add CLI command for dumping the entire store


### Deploy image


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