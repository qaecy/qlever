# QLever

This is a QAECY version of the Qlever quad store. It extends the existing work with a CLI tool that allows querying a dataset as an embedded database.

### Build
`docker build -f Dockerfiles/Dockerfile.cli-only -t qlever-cli-only .`

### CLI
The CLI tool binary is built inside a Docker container for compatibility reasons. Therefore all commands are run through the container.

### Build index
How to use the index builder?
`docker run --entrypoint="" qlever-cli-only bash -c '/qlever/IndexBuilderMain --help'`

The files must be of same type and 

Example:
`docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli-only bash -c '/qlever/IndexBuilderMain -i ./databases/OSTT -f ostt.nt ontology.nt -F nt --vocabulary-type on-disk-compressed'`

### Query the index
The query command takes a path to the index without suffixes (eg. `./databases/OSTT`) and a SPARQL 1.1 query

```bash
# Example 1 - count all triples:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli-only bash -c "/qlever/QleverCliMain query ./databases/OSTT 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }'"

# Example 2 - 10 entity mentions:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli-only bash -c "/qlever/QleverCliMain query ./databases/OSTT 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?s qcy:mentions ?o . } LIMIT 10'"

# Example 3 - 10 resolved entities and the documents they are about:
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli-only bash -c "/qlever/QleverCliMain query ./databases/OSTT 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?frag qcy:mentions ?em . ?em qcy:resolvesTo ?canonical } LIMIT 10'"
```