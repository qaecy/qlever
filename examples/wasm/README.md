# QLever WebAssembly Example

This directory contains a minimal example demonstrating how to use QLever's WebAssembly bindings in a web browser.

## Files

- `index.html` - Complete web application demonstrating QLever WASM functionality
- `README.md` - This file

## Features

The example web application provides:

- ✅ **Index Initialization** - Load QLever indices in the browser
- ✅ **SPARQL Query Execution** - Execute queries with multiple output formats
- ✅ **Query Planning** - Parse and plan queries without execution
- ✅ **Status Monitoring** - Check QLever initialization and readiness
- ✅ **Multiple Output Formats** - SPARQL JSON, CSV, TSV, XML, QLever JSON
- ✅ **Example Queries** - Pre-built queries to test functionality
- ✅ **Error Handling** - Comprehensive error reporting and status messages

## Usage

### Option 1: Using Docker (Recommended)

Build and run the WASM container:

```bash
# Build the WASM Docker image
docker build -f Dockerfiles/Dockerfile.wasm.alpine -t qlever-wasm:alpine .

# Run the container (serves on port 8080)
docker run -p 8080:80 --rm qlever-wasm:alpine

# Open browser to http://localhost:8080
```

### Option 2: Manual Build

If you have Emscripten installed locally:

```bash
# Create build directory
mkdir build-wasm && cd build-wasm

# Configure with Emscripten
emcmake cmake -DCMAKE_BUILD_TYPE=Release \
              -DLOGLEVEL=INFO \
              -GNinja \
              -DEMSCRIPTEN=ON \
              -DUSE_PRECOMPILED_HEADERS=OFF \
              ..

# Build WASM module
cmake --build . --target qlever-wasm-module

# Copy files to web server directory
cp qlever-wasm.js qlever-wasm.wasm ../examples/wasm/

# Serve with any web server that supports WASM
# For example, using Python:
cd ../examples/wasm
python3 -m http.server 8000 --bind 127.0.0.1
```

## Requirements

### For the Web Application

- Modern web browser with WebAssembly support
- Local or remote web server (due to CORS restrictions)

### For Building

- Emscripten SDK (latest version)
- CMake 3.27+
- Ninja build system
- All QLever dependencies (ICU, Boost, etc.)

## API Reference

The WASM module exposes the following JavaScript interface:

### QleverWasm Class

```javascript
const qlever = new QleverModule.QleverWasm();
```

#### Methods

**`initialize(indexBasename, memoryLimitMB)`**
- Initialize QLever with an index
- Returns: JSON string with success/error status

**`query(queryString, format)`**
- Execute a SPARQL query
- Formats: "sparql-json", "csv", "tsv", "sparql-xml", "qlever-json"
- Returns: JSON string with results or error

**`parseAndPlan(queryString)`**
- Parse and plan a query without execution
- Returns: JSON string with planning status

**`isReady()`**
- Check if QLever is initialized
- Returns: boolean

**`getStatus()`**
- Get current status information
- Returns: JSON string with status details

## Example Usage

```javascript
// Load the WASM module
const qleverModule = await QleverModule();
const qlever = new qleverModule.QleverWasm();

// Initialize with an index
const initResult = qlever.initialize("./databases/OSTT", 512);
console.log(JSON.parse(initResult));

// Execute a query
const queryResult = qlever.query(
    "SELECT * WHERE { ?s ?p ?o } LIMIT 10", 
    "sparql-json"
);
console.log(JSON.parse(queryResult));
```

## Troubleshooting

### Common Issues

1. **CORS Errors**: Ensure you're serving the files through a web server, not opening the HTML file directly
2. **Memory Issues**: Increase the memory limit parameter or use a smaller index
3. **Index Not Found**: Make sure the index path is correct and accessible from the web environment

### Browser Compatibility

The WASM module requires:
- WebAssembly support (available in all modern browsers)
- SharedArrayBuffer support (for threading, if enabled)
- Sufficient memory allocation (browsers typically limit WASM memory)

## Performance Notes

- Initial loading may take several seconds due to WASM compilation
- Query performance depends on index size and browser memory limits
- Large result sets may cause browser memory issues
- Consider using streaming or pagination for large queries

## Security Considerations

- The WASM module runs in the browser's sandbox
- File system access is limited to the virtual file system
- Network requests are subject to browser CORS policies
- Consider the security implications of loading index data in the browser