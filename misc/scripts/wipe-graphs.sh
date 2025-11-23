#!/bin/bash
# This script processes an nquads file to remove graph IRIs from each quad,
# effectively converting it to ntriples format.

NQ_PATH="$1"
if [ -z "$NQ_PATH" ]; then
    echo "Usage: $0 <nquads_file_path>"
    exit 1
fi

echo "Wiping IRIs from nquads file at path: $NQ_PATH"

# If the input file ends with .nq, write to .nt
if [[ "$NQ_PATH" == *.nq ]]; then
    NT_PATH="${NQ_PATH%.nq}.nt"
    if ! command -v gawk >/dev/null 2>&1; then
        echo "Error: gawk is required but not installed. Install it with 'brew install gawk'." >&2
        exit 2
    fi
    gawk 'match($0, /^(\S+)\s+(\S+)\s+(.+) <[^>]+> \.$/, arr) {print arr[1] " " arr[2] " " arr[3] " ."}' "$NQ_PATH" > "$NT_PATH"
    echo "Output written to $NT_PATH"
else
    sed -E 's/ <[^>]+> \.$/ ./g' "$NQ_PATH"
fi

riot --validate "$NT_PATH"