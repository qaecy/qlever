# QLever CLI — Examples

These examples assume you have built the Alpine image:

```bash
docker build --platform linux/amd64 -f Dockerfiles/Dockerfile.cli-only.alpine -t qlever-cli:alpine-test .
```

Define a shell function so all commands below stay readable:

```bash
qlever() {
  docker run --rm --user root \
    -v "$(pwd)":/workspace \
    -w /workspace \
    --entrypoint="" \
    qlever-cli:alpine-test \
    /qlever/qlever-cli "$@"
}
```

Source the function:
`source ./qlever-docker.sh`

All commands below use `qlever` as defined above.

---

## Build index

The index configuration is a JSON object. A sample config is in `misc/configs/build-test-index.json`, which loads the small NQuads file `misc/test-simple.nq`.

Available vocabulary types:
- `in-memory-uncompressed`
- `on-disk-uncompressed`
- `in-memory-compressed`
- `on-disk-compressed` (default)
- `on-disk-compressed-geo-split` (required for GeoSPARQL)

```bash
# Default (on-disk-compressed)
qlever build-index "$(cat misc/configs/build-test-index.json)"

# In-memory vocabulary
qlever build-index "$(cat misc/configs/build-test-index-mem.json)"

# Rebuild — merge delta triples into the main index
qlever binary-rebuild ./databases/test
```

From gunzipped file:
```bash
gunzip -c /Users/mads/Downloads/777e7ad2-3856-408b-a031-1a154f3f3414_no_star.nq.gz | \
  docker run --rm --user root -i \
    -v "$(pwd)":/workspace \
    -w /workspace \
    --entrypoint="" \
    qlever-cli:alpine-test \
    /qlever/qlever-cli build-index "$(cat misc/configs/build-index-stream.json)"
```

---

## Get index stats

```bash
qlever stats ./databases/OSTT
```

---

## Query the index

The query command takes a path to the index (no file suffix) and a SPARQL 1.1 query string.

Supported output formats: `sparql-json`, `sparql-xml`, `qlever-json`, `tsv`, `csv`, `turtle`, `nt`, `octet-stream`.

```bash
# Count all triples
qlever query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }'

# Count all triples — CSV output
qlever query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }' csv

# 10 entity mentions
qlever query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?s qcy:mentions ?o . } LIMIT 10'

# 10 resolved entities and the documents they are about
qlever query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?frag qcy:mentions ?em . ?em qcy:resolvesTo ?canonical } LIMIT 10'

# CONSTRUCT — raw N-Triples output
qlever query ./databases/xx 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt

# CONSTRUCT to file (useful when result exceeds available memory)
qlever query-to-file ./databases/xx 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt /workspace/res.nt

# DESCRIBE — raw N-Triples output
qlever query ./databases/test 'DESCRIBE <http://example.org/subject1>' nt

# ASK query
qlever query ./databases/test 'ASK WHERE { <http://example.org/subject1> ?p ?o }'
```

---

## Named queries

Tag a query with a name so the result is pinned in the cache for follow-up queries (note: the cache is not persisted across process restarts).

```bash
# Pin a query under the name "fc-mentions"
qlever query ./databases/OSTT \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   SELECT ?fc ?m ?val WHERE {
     ?fc a qcy:FileContent ;
         qcy:containsFragment*/qcy:mentions ?m .
     ?m a qcy:EntityMention ; qcy:value ?val
   }' csv fc-mentions

# Use the pinned result
qlever query ./databases/OSTT \
  'SELECT ?fc ?m ?val WHERE { SERVICE ql:cached-result-with-name-fc-mentions {} } LIMIT 5' csv
```

---

## Update queries (SPARQL UPDATE)

Delta triples are stored in `<index_name>.update-triples` and included in all subsequent query evaluations. Querying over delta triples is slower than querying the base index.

```bash
# Count all triples (9,003,298 on NEST — 0.386 s)
qlever query ./databases/NEST \
  'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'

# Insert inferred super-types via rdfs:subClassOf (3.225 s on NEST)
qlever update ./databases/NEST \
  'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
   INSERT { ?a a ?sc } WHERE { ?a a ?cl . ?cl rdfs:subClassOf ?sc }'

# Count again — includes delta triples (9,248,305 on NEST — 4.885 s)
qlever query ./databases/NEST \
  'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'
```

---

## Serialize

Dump the entire database in N-Triples or N-Quads format. A 25 M triple database serialized as gzipped `.nt` took 3:38 (3:02 uncompressed).

```bash
# N-Triples to stdout
qlever serialize ./databases/test nt

# N-Triples to file
qlever serialize ./databases/test nt /workspace/test.nt

# N-Triples to gzipped file
qlever serialize ./databases/test nt /workspace/test.nt.gz

# N-Quads to stdout
qlever serialize ./databases/test nq
```

---

## Inference / saturation queries

These CONSTRUCT queries derive implicit triples from the NEST database (10 M quads).

```bash
# 1. Super-types via rdfs:subClassOf — 886,529 triples (8.665 s)
qlever query-to-file ./databases/NEST \
  'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
   CONSTRUCT { ?a a ?sc } WHERE { ?a a ?cl . ?cl rdfs:subClassOf ?sc }' \
  nt /workspace/implicit1.nt

# 2. Super-properties via rdfs:subPropertyOf — 1,298,508 triples (14.161 s)
qlever query-to-file ./databases/NEST \
  'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
   CONSTRUCT { ?s ?sp ?o } WHERE { ?s ?p ?o . ?p rdfs:subPropertyOf ?sp }' \
  nt /workspace/implicit2.nt

# 3. qcy:about via FileContent/Fragment mentions — 209,830 triples (2.922 s)
qlever query-to-file ./databases/NEST \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   CONSTRUCT { ?fc qcy:about ?c . }
   WHERE { ?fc qcy:containsFragment*/qcy:mentions ?m . ?m qcy:resolvesTo ?c . }' \
  nt /workspace/implicit3.nt

# 4. Canonical relationships from mention relationships — 6,994 triples (0.255 s)
qlever query-to-file ./databases/NEST \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   CONSTRUCT { ?c1 ?p ?c2 }
   WHERE { ?m1 ?p ?m2 . ?m1 qcy:subPropertyOf*/qcy:relatedEntity ?m2 .
           ?m1 qcy:resolvesTo ?c1 . ?m2 qcy:resolvesTo ?c2 }' \
  nt /workspace/implicit4.nt

# 5. Implicitly contained fragments — 0 triples (0.006 s)
qlever query-to-file ./databases/NEST \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   CONSTRUCT { ?fc qcy:implicitlyContainsFragment ?f }
   WHERE { ?fc qcy:containsFragment ?f MINUS { ?fc qcy:containsFragment ?f } }' \
  nt /workspace/implicit5.nt

# Summary: explicit 9,003,298 + implicit 2,401,861 = 11,405,159 (+27 %)
# Total derivation time: ~26 s (~92,350 triples/s)

# Count all (including delta triples)
qlever query ./databases/NEST \
  'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'
```

---

## Use in your app

If you need to call `qlever-cli` from application code, copy the binary from the image at build time:

```dockerfile
# In your application's Dockerfile (Alpine-based)
COPY --from=qlever-cli:alpine-test /qlever/qlever-cli /usr/local/bin/qlever-cli
```

Or from QAECY's artifact registry:

```dockerfile
COPY --from=europe-west6-docker.pkg.dev/qaecy-mvp-406413/databases/qlever-cli:alpine-x86_64 /qlever/qlever-cli /usr/local/bin/qlever-cli
```

Runtime dependencies needed on Alpine:

```dockerfile
RUN apk add --no-cache \
    libstdc++ libgcc icu-libs openssl zstd-libs zlib jemalloc \
    boost-program_options boost-iostreams boost-system boost-url
```

Example Python usage once the binary is on `PATH`:

```python
import subprocess, json

result = subprocess.run(
    ['qlever-cli', 'query', './databases/mydb',
     'SELECT * WHERE { ?s ?p ?o } LIMIT 10'],
    capture_output=True, text=True,
)
data = json.loads(result.stdout)
```

---

## Build index

The index configuration is a JSON object. A sample config is in `misc/configs/build-test-index.json`, which loads the small NQuads file `misc/test-simple.nq`.

Available vocabulary types:
- `in-memory-uncompressed`
- `on-disk-uncompressed`
- `in-memory-compressed`
- `on-disk-compressed` (default)
- `on-disk-compressed-geo-split` (required for GeoSPARQL)

```bash
# Default (on-disk-compressed)
./qlever-cli build-index "$(cat misc/configs/build-test-index.json)"

# In-memory vocabulary
./qlever-cli build-index "$(cat misc/configs/build-test-index-mem.json)"

# Rebuild — merge delta triples into the main index
./qlever-cli binary-rebuild ./databases/test
```

---

## Get index stats

```bash
./qlever-cli stats ./databases/OSTT
```

---

## Query the index

The query command takes a path to the index (no file suffix) and a SPARQL 1.1 query string.

Supported output formats: `sparql-json`, `sparql-xml`, `qlever-json`, `tsv`, `csv`, `turtle`, `nt`, `octet-stream`.

```bash
# Count all triples
./qlever-cli query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }'

# Count all triples — CSV output
./qlever-cli query ./databases/test 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o . }' csv

# 10 entity mentions
./qlever-cli query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?s qcy:mentions ?o . } LIMIT 10'

# 10 resolved entities and the documents they are about
./qlever-cli query ./databases/test 'PREFIX qcy: <https://dev.qaecy.com/ont#> SELECT * WHERE { ?frag qcy:mentions ?em . ?em qcy:resolvesTo ?canonical } LIMIT 10'

# CONSTRUCT — raw N-Triples output
./qlever-cli query ./databases/xx 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt

# CONSTRUCT to file (useful when result exceeds available memory)
./qlever-cli query-to-file ./databases/xx 'CONSTRUCT WHERE { ?s ?p ?o } LIMIT 10' nt ./res.nt

# DESCRIBE — raw N-Triples output
./qlever-cli query ./databases/test 'DESCRIBE <http://example.org/subject1>' nt

# ASK query
./qlever-cli query ./databases/test 'ASK WHERE { <http://example.org/subject1> ?p ?o }'
```

---

## Named queries

Tag a query with a name so the result is pinned in the cache for follow-up queries (note: the cache is not persisted across process restarts).

```bash
# Pin a query under the name "fc-mentions"
./qlever-cli query ./databases/OSTT \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   SELECT ?fc ?m ?val WHERE {
     ?fc a qcy:FileContent ;
         qcy:containsFragment*/qcy:mentions ?m .
     ?m a qcy:EntityMention ; qcy:value ?val
   }' csv fc-mentions

# Use the pinned result
./qlever-cli query ./databases/OSTT \
  'SELECT ?fc ?m ?val WHERE { SERVICE ql:cached-result-with-name-fc-mentions {} } LIMIT 5' csv
```

---

## Update queries (SPARQL UPDATE)

Delta triples are stored in `<index_name>.update-triples` and included in all subsequent query evaluations. Querying over delta triples is slower than querying the base index.

```bash
# Count all triples (9,003,298 on NEST — 0.386 s)
./qlever-cli query ./databases/NEST \
  'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'

# Insert inferred super-types via rdfs:subClassOf (3.225 s on NEST)
./qlever-cli update ./databases/NEST \
  'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
   INSERT { ?a a ?sc } WHERE { ?a a ?cl . ?cl rdfs:subClassOf ?sc }'

# Count again — includes delta triples (9,248,305 on NEST — 4.885 s)
./qlever-cli query ./databases/NEST \
  'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'
```

---

## Serialize

Dump the entire database in N-Triples or N-Quads format. A 25 M triple database serialized as gzipped `.nt` took 3:38 (3:02 uncompressed).

```bash
# N-Triples to stdout
./qlever-cli serialize ./databases/test nt

# N-Triples to file
./qlever-cli serialize ./databases/test nt ./test.nt

# N-Triples to gzipped file
./qlever-cli serialize ./databases/test nt ./test.nt.gz

# N-Quads to stdout
./qlever-cli serialize ./databases/test nq
```

---

## Inference / saturation queries

These CONSTRUCT queries derive implicit triples from the NEST database (10 M quads).

```bash
# 1. Super-types via rdfs:subClassOf — 886,529 triples (8.665 s)
./qlever-cli query-to-file ./databases/NEST \
  'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
   CONSTRUCT { ?a a ?sc } WHERE { ?a a ?cl . ?cl rdfs:subClassOf ?sc }' \
  nt ./implicit1.nt

# 2. Super-properties via rdfs:subPropertyOf — 1,298,508 triples (14.161 s)
./qlever-cli query-to-file ./databases/NEST \
  'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>
   CONSTRUCT { ?s ?sp ?o } WHERE { ?s ?p ?o . ?p rdfs:subPropertyOf ?sp }' \
  nt ./implicit2.nt

# 3. qcy:about via FileContent/Fragment mentions — 209,830 triples (2.922 s)
./qlever-cli query-to-file ./databases/NEST \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   CONSTRUCT { ?fc qcy:about ?c . }
   WHERE { ?fc qcy:containsFragment*/qcy:mentions ?m . ?m qcy:resolvesTo ?c . }' \
  nt ./implicit3.nt

# 4. Canonical relationships from mention relationships — 6,994 triples (0.255 s)
./qlever-cli query-to-file ./databases/NEST \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   CONSTRUCT { ?c1 ?p ?c2 }
   WHERE { ?m1 ?p ?m2 . ?m1 qcy:subPropertyOf*/qcy:relatedEntity ?m2 .
           ?m1 qcy:resolvesTo ?c1 . ?m2 qcy:resolvesTo ?c2 }' \
  nt ./implicit4.nt

# 5. Implicitly contained fragments — 0 triples (0.006 s)
./qlever-cli query-to-file ./databases/NEST \
  'PREFIX qcy: <https://dev.qaecy.com/ont#>
   CONSTRUCT { ?fc qcy:implicitlyContainsFragment ?f }
   WHERE { ?fc qcy:containsFragment ?f MINUS { ?fc qcy:containsFragment ?f } }' \
  nt ./implicit5.nt

# Summary: explicit 9,003,298 + implicit 2,401,861 = 11,405,159 (+27 %)
# Total derivation time: ~26 s (~92,350 triples/s)

# Count all (including delta triples)
./qlever-cli query ./databases/NEST \
  'SELECT (COUNT(*) AS ?count) WHERE { { ?s ?p ?o } UNION { GRAPH ?g {?s ?p ?o} } }'
```

---

## Use in your app

```python
import subprocess, json

result = subprocess.run(
    ['./qlever-cli', 'query', './databases/mydb',
     'SELECT * WHERE { ?s ?p ?o } LIMIT 10'],
    capture_output=True, text=True,
)
data = json.loads(result.stdout)
```
