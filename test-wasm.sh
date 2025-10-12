#!/bin/bash

# Simple test script to verify WASM build works

echo "🧪 Testing QLever WASM Build"
echo "============================="

# Check if wasm-build directory exists
if [ ! -d "./wasm-build" ]; then
    echo "❌ wasm-build directory not found. Run ./build-wasm-real.sh first"
    exit 1
fi

# Check if WASM files exist
if [ ! -f "./wasm-build/qlever-wasm.js" ]; then
    echo "❌ qlever-wasm.js not found"
    exit 1
fi

if [ ! -f "./wasm-build/qlever-wasm.wasm" ]; then
    echo "❌ qlever-wasm.wasm not found"
    exit 1
fi

if [ ! -f "./wasm-build/index.html" ]; then
    echo "❌ index.html not found"
    exit 1
fi

echo "✅ All required files found:"
echo "   • qlever-wasm.js ($(du -h ./wasm-build/qlever-wasm.js | cut -f1))"
echo "   • qlever-wasm.wasm ($(du -h ./wasm-build/qlever-wasm.wasm | cut -f1))"
echo "   • index.html ($(du -h ./wasm-build/index.html | cut -f1))"
echo ""

# Test if we can serve the files
echo "🌐 Testing web server..."
echo "Starting server on http://localhost:8080"
echo "Press Ctrl+C to stop"
echo ""

cd wasm-build
python3 -m http.server 8080