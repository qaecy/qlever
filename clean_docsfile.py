#!/usr/bin/env python3
import sys
import re

def is_valid_docs_line(line):
    fields = line.rstrip('\n').split('\t', 1)
    if len(fields) != 2:
        return False
    context_id, text = fields
    return context_id.isdigit() and text.strip() != ''

def clean_docsfile(input_path, output_path):
    with open(input_path, 'r') as infile, open(output_path, 'w') as outfile:
        for i, line in enumerate(infile, 1):
            if is_valid_docs_line(line):
                outfile.write(line)
            else:
                # Optionally log or count skipped lines
                pass

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: clean_docsfile.py <input_docsfile> <output_docsfile>")
        sys.exit(1)
    clean_docsfile(sys.argv[1], sys.argv[2])
    print("Cleaned docsfile written to", sys.argv[2])
