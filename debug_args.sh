#!/bin/bash
echo "=== SHELL DEBUG ==="
echo "Script name: $0"
echo "Number of args: $#"
echo "All args: $@"
for i in $(seq 1 $#); do
    eval echo "Arg $i: \${$i}"
done
echo "================="