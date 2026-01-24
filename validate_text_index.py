#!/usr/bin/env python3
import sys
import os

def validate_file(path, expected_fields):
    with open(path, 'r') as f:
        for i, line in enumerate(f, 1):
            line = line.rstrip('\n')
            if not line:
                print(f"Empty line at {path}:{i}")
                continue
            fields = line.split('\t')
            if len(fields) < expected_fields:
                print(f"Malformed line at {path}:{i}: {line}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: validate_text_index.py <wordsfile> <docsfile>")
        sys.exit(1)
    wordsfile = sys.argv[1]
    docsfile = sys.argv[2]
    print(f"Validating {wordsfile} (should have at least 3 fields per line)")
    validate_file(wordsfile, 3)
    print(f"Validating {docsfile} (should have at least 2 fields per line)")
    validate_file(docsfile, 2)
    print("Validation complete.")
