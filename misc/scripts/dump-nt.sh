DB_PATH="$1"
if [ -z "$DB_PATH" ]; then
    echo "Usage: $0 <db_index_path>"
    exit 1
fi

echo "Dumping graph $DB_PATH in NTriples"

docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/QleverCliMain query-to-file $DB_PATH 'CONSTRUCT { ?s ?p ?o } WHERE { GRAPH ?g { ?s ?p ?o } }' nt /workspace/dump.nt"