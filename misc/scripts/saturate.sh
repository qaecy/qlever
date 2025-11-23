DB_PATH="$1"
if [ -z "$DB_PATH" ]; then
    echo "Usage: $0 <db_index_path>"
    exit 1
fi

IMPLICIT_IRI=https://implicit

echo "Saturating database at path: $DB_PATH"

# 36554434

echo "Wiping implicit graph..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query $DB_PATH 'DUMP GRAPH <$IMPLICIT_IRI> ; DROP GRAPH <$IMPLICIT_IRI> ;'"

echo "=== INITIAL DB SIZE ==="
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query $DB_PATH 'SELECT (COUNT(*) as ?count) WHERE { ?s ?p ?o }'"

echo "rdfs:domain inference..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> INSERT { GRAPH <$IMPLICIT_IRI> { ?s a ?class } } WHERE { ?s ?p ?p . ?p rdfs:domain ?class }'"

echo "rdfs:range inference..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> INSERT { GRAPH <$IMPLICIT_IRI> { ?o a ?class } } WHERE { ?s ?p ?p . ?p rdfs:range ?class }'"

echo "rdfs:subClassOf inference..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> INSERT { GRAPH <$IMPLICIT_IRI> { ?o a ?class } } WHERE { ?s ?p ?p . ?p rdfs:range ?class }'"

echo "rdfs:subPropertyOf inference..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#> INSERT { GRAPH <$IMPLICIT_IRI> { ?s ?superProp ?o } } WHERE { ?s ?p ?p . ?p rdfs:subPropertyOf ?superProp }'"

echo "qcy:implicitlyContainsFragment from transivity..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX qcy: <https://dev.qaecy.com/ont#> INSERT { GRAPH <$IMPLICIT_IRI> { ?infoObj qcy:implicitlyContainsFragment ?otherFrag } } WHERE { ?infoObj qcy:containsFragment ?frag . ?frag qcy:containsFragment ?otherFrag . }'"

echo "qcy:mentions through file location..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX qcy: <https://dev.qaecy.com/ont#> INSERT { GRAPH <$IMPLICIT_IRI> { ?fc qcy:mentions ?m } } WHERE { ?fc qcy:hasFileLocation ?loc . ?loc qcy:mentions ?m . }'"

echo "qcy:about through direct entity mention..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX qcy: <https://dev.qaecy.com/ont#> INSERT { GRAPH <$IMPLICIT_IRI> { ?s qcy:about ?c } } WHERE { ?s qcy:mentions ?m . ?m qcy:resolvesTo ?c . }'"

echo "qcy:about through contained fragment entity mention..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX qcy: <https://dev.qaecy.com/ont#> INSERT { GRAPH <$IMPLICIT_IRI> { ?infoObj qcy:about ?canonicalEntity } } WHERE { ?infoObj qcy:containsFragment|qcy:implicitlyContainsFragment ?frag . ?frag qcy:about ?canonicalEntity }'"

echo "qcy:mentions through contained fragment..."
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain update $DB_PATH 'PREFIX qcy: <https://dev.qaecy.com/ont#> INSERT { GRAPH <$IMPLICIT_IRI> { ?infoObj qcy:about ?canonicalEntity } } WHERE { ?infoObj qcy:containsFragment|qcy:implicitlyContainsFragment ?frag . ?frag qcy:about ?canonicalEntity }'"


echo "=== TOTAL SIZE, IMPLICIT GRAPH ==="
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query $DB_PATH 'SELECT (COUNT(*) as ?count) WHERE { GRAPH <$IMPLICIT_IRI> { ?s ?p ?o } }'"

echo "=== DOWNLOAD IMPLICIT ==="
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query-to-file $DB_PATH 'CONSTRUCT { ?s ?p ?o } WHERE { GRAPH <$IMPLICIT_IRI> { ?s ?p ?o } }' nt /workspace/implicit.nt"