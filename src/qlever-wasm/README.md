# QLever WebAssembly Integration

This directory contains the WebAssembly bindings for QLever, enabling SPARQL query execution directly in web browsers.

## 🚀 Quick Start

### Option 1: Pre-built WASM (Recommended for Testing)

If you just want to try the WASM functionality, download the pre-built files:

```bash
# Download pre-built WASM files (when available)
wget https://github.com/qaecy/qlever/releases/latest/download/qlever-wasm.js
wget https://github.com/qaecy/qlever/releases/latest/download/qlever-wasm.wasm

# Copy example files
cp examples/wasm/index.html .
cp examples/wasm/README.md .

# Serve with any web server
python3 -m http.server 8080
# Open http://localhost:8080
```

### Option 2: Build from Source

#### Prerequisites

- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) installed
- CMake 3.27+
- All QLever dependencies (ICU, Boost, OpenSSL, etc.)

#### Build Steps

```bash
# Install Emscripten (if not already installed)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
cd ..

# Configure build with Emscripten
mkdir build-wasm && cd build-wasm
emcmake cmake -DCMAKE_BUILD_TYPE=Release \
              -DLOGLEVEL=INFO \
              -GNinja \
              -DEMSCRIPTEN=ON \
              -DUSE_PRECOMPILED_HEADERS=OFF \
              ..

# Build WASM module
cmake --build . --target qlever-wasm-module

# Files will be generated as:
# - qlever-wasm.js    (JavaScript loader)
# - qlever-wasm.wasm  (WebAssembly binary)
```

### Option 3: Docker Build (Advanced)

For a complete containerized build:

```bash
# Build WASM in Docker (takes 30+ minutes)
./build-wasm.sh

# Or manually:
docker build -f Dockerfiles/Dockerfile.wasm.alpine -t qlever-wasm:alpine .
docker run -p 8080:80 qlever-wasm:alpine
```

## 📚 Usage

### JavaScript API

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

### Available Methods

- `initialize(indexBasename, memoryLimitMB)` - Load QLever index
- `query(queryString, format)` - Execute SPARQL query
- `parseAndPlan(queryString)` - Parse query without execution
- `isReady()` - Check initialization status
- `getStatus()` - Get current status information

### Supported Formats

- `sparql-json` - SPARQL JSON Results Format
- `csv` - Comma-separated values
- `tsv` - Tab-separated values  
- `sparql-xml` - SPARQL XML Results Format
- `qlever-json` - QLever JSON with timing information

## 🎯 Features

✅ **Full SPARQL Support** - All SPARQL 1.1 features supported by QLever  
✅ **Multiple Output Formats** - JSON, CSV, TSV, XML  
✅ **Memory Management** - Configurable memory limits  
✅ **Error Handling** - Comprehensive error reporting  
✅ **Status Monitoring** - Real-time status and timing information  
✅ **Browser Compatible** - Works in all modern browsers  

## 🔧 Technical Details

### Architecture

The WASM bindings wrap QLever's core `libqlever` library using Emscripten. The main components are:

- **QleverWasm.cpp** - C++ wrapper with Emscripten bindings
- **QleverWasm.h** - Header file defining the interface
- **CMakeLists.txt** - Build configuration for WASM target

### Memory Considerations

- **Virtual File System** - Indexes must be accessible via Emscripten's file system
- **Memory Limits** - Browser WASM memory is typically limited to 2-4GB
- **Loading Time** - Initial WASM compilation can take several seconds

### Browser Requirements

- **WebAssembly Support** - Available in all modern browsers
- **Sufficient Memory** - Depends on index size and query complexity
- **CORS Policy** - Files must be served via HTTP/HTTPS (not file://)

## 📁 File Structure

```
src/qlever-wasm/
├── QleverWasm.cpp      # Main WASM wrapper implementation
├── QleverWasm.h        # Header file
├── CMakeLists.txt      # Build configuration
└── README.md           # This file

examples/wasm/
├── index.html          # Complete demo application
└── README.md           # Example documentation

Dockerfiles/
└── Dockerfile.wasm.alpine  # Docker build configuration
```

## 🚨 Limitations

- **Large Indexes** - Browser memory limits restrict index size
- **File System** - Limited to virtual file system (no direct disk access)
- **Threading** - Limited threading support in browser WASM
- **Performance** - May be slower than native builds

## 🤝 Contributing

To contribute to the WASM bindings:

1. Make changes to `QleverWasm.cpp` or `QleverWasm.h`
2. Test with a local Emscripten build
3. Update the example in `examples/wasm/index.html` if needed
4. Submit a pull request

## 📄 License

Same as QLever main project - see LICENSE file in repository root.