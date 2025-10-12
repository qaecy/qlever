# Simplified QLever WASM Build Instructions

Since building QLever WASM involves complex cross-compilation requirements, here are alternative approaches:

## Option 1: Local Build (if you have Emscripten installed)

```bash
# Install Emscripten first: https://emscripten.org/docs/getting_started/downloads.html
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Build QLever WASM
cd /path/to/qlever
mkdir build-wasm && cd build-wasm
emcmake cmake -DCMAKE_BUILD_TYPE=Release \
              -DLOGLEVEL=INFO \
              -GNinja \
              -DEMSCRIPTEN=ON \
              -DUSE_PRECOMPILED_HEADERS=OFF \
              -DREDUCED_FEATURE_SET_FOR_CPP17=ON \
              ..
cmake --build . --target qlever-wasm-module
```

## Option 2: Use Pre-built Binaries (Recommended)

We'll provide pre-built WASM binaries for common use cases. Check the releases page for:
- `qlever-wasm.js` - JavaScript loader
- `qlever-wasm.wasm` - WebAssembly binary

## Option 3: Simplified Demo

For immediate testing, use the demo with mock functionality:

```bash
# Serve the demo locally
cd examples/wasm
python3 -m http.server 8080
# Open http://localhost:8080
```

This shows the complete UI without requiring the WASM build.

## Why is WASM Building Complex?

Building QLever for WebAssembly involves:
1. **Cross-compilation**: Building for web target vs native
2. **Large dependency tree**: ICU, Boost, OpenSSL, ZSTD, etc.
3. **Memory constraints**: WASM has different memory model
4. **Threading limitations**: Limited threading in browsers
5. **CMake version compatibility**: Emscripten images often have older CMake

## Future Improvements

We're working on:
- Pre-built GitHub Actions workflow
- Simplified Docker build process  
- Reduced dependency version of QLever for WASM
- Online demo with pre-built binaries

## Contributing

If you successfully build QLever WASM locally, please:
1. Share your build configuration
2. Document any issues encountered
3. Consider contributing pre-built binaries