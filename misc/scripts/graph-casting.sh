DB_PATH="$1"
if [ -z "$DB_PATH" ]; then
    echo "Usage: $0 <db_index_path>"
    exit 1
fi

FILE_PATH=./CAST.nt

echo "Building graph cast of: $DB_PATH"
echo ""

echo "Getting original size of the graph"
echo ""
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query $DB_PATH 'SELECT (COUNT(*) AS ?count) WHERE {?s ?p ?o}'"

QUERY="CONSTRUCT {\n\
\t?s a <https://dev.qaecy.com/comp#Entity> ;\n\
\t\t<https://dev.qaecy.com/comp#entityType> ?cat ;\n\
\t\t<https://dev.qaecy.com/comp#value> ?val .\n\
\t?e1 ?rel ?e2 .\n\
\t?doc a <https://dev.qaecy.com/comp#Document> ;\n\
\t\t<https://dev.qaecy.com/comp#about> ?e .\n\
}\n\
WHERE {\n\
{\n\
\t?s a <https://dev.qaecy.com/ont#CanonicalEntity> ;\n\
\t\t<https://dev.qaecy.com/ont#hasEntityCategory> ?cat ;\n\
\t\t<https://dev.qaecy.com/ont#value> ?val .\n\
}\n\
UNION\n\
{\n\
\t?e1 a <https://dev.qaecy.com/ont#CanonicalEntity> ;\n\
\t\t?rel ?e2 .\n\
\t?e2 a <https://dev.qaecy.com/ont#CanonicalEntity>\n\
\tFILTER(?rel != <https://dev.qaecy.com/ont#relatedEntity>)\n\
}\n\
UNION\n\
{\n\
\t?doc a <https://dev.qaecy.com/ont#FileContent> ;\n\
\t\t<https://dev.qaecy.com/ont#about> ?e .\n\
}\n\
}"

echo "Executing graph cast query on dataset"
echo ""
echo $QUERY

echo ""
echo "COMPACT"
echo ""
COMPACT=$(echo "$QUERY" | tr '\t\n' ' ')
echo $COMPACT
echo ""

# Run query
docker run --rm --user root -v $(pwd):/workspace -w /workspace --entrypoint="" qlever-cli:alpine sh -c "/qlever/qlever-cli query-to-file $DB_PATH '$COMPACT' nt $FILE_PATH"

# 16,385,258

# 435,371
# 5664 ms

# I have tried doing a graph casting on the NEST project.

# Original is 16,385,258 triples. Casted is 435,371.

# For now I keep these things:

# <c> a qcy:CanonicalEntity ;
#    qcy:hasEntityCategory <cCat> ;
#    qcy:value "value" ;
#    qcy:* <c2> .
# <fc> a qcy:FileContent ;
#    qcy:about <c> .

# I do it by using the query-to-file function on the QLever CLI which directly stream reads to an nt-file. This takes around 5.6 seconds. Next I would build this as an index that you can directly query.

# I can of course change namespace etc. as part of the construct so we use the compact syntax. Personally I am not a great fan of this.

# A few things to consider:
# - Properties not included
# - Selectors not included