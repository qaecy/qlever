#!/bin/bash

# QLever WASM Builder - Build WASM files in Docker and extract them

set -e

echo "🚀 QLever WebAssembly Builder"
echo "==============================="

# Function to print colored output
print_status() {
    echo -e "\033[1;34m[INFO]\033[0m $1"
}

print_success() {
    echo -e "\033[1;32m[SUCCESS]\033[0m $1"
}

print_error() {
    echo -e "\033[1;31m[ERROR]\033[0m $1"
}

print_warning() {
    echo -e "\033[1;33m[WARNING]\033[0m $1"
}

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    print_error "Docker is not installed or not in PATH"
    exit 1
fi

# Parse command line arguments
DOCKERFILE="Dockerfile.wasm.build"
BUILD_ONLY=false
SERVE_DEMO=false
OUTPUT_DIR="./wasm-build"

while [[ $# -gt 0 ]]; do
    case $1 in
                --alpine)
            DOCKERFILE="Dockerfile.wasm.alpine"
            print_warning "Using Alpine build (slower, builds Emscripten from source)"
            shift
            ;;
        --emscripten|--build)
            DOCKERFILE="Dockerfile.wasm.build"
            print_status "Using pre-built Emscripten image (faster)"
            shift
            ;;
        --demo)
            DOCKERFILE="Dockerfile.wasm.demo"
            print_status "Using lightweight demo build (fastest)"
            shift
            ;;
        --enhanced)
            DOCKERFILE="Dockerfile.wasm.enhanced"
            print_status "Using enhanced demo build with RDF parsing (fast)"
            shift
            ;;
        --real)
            DOCKERFILE="Dockerfile.wasm.real"
            print_warning "Using real QLever implementation (experimental, very slow)"
            shift
            ;;
        --build-only)
            BUILD_ONLY=true
            print_status "Build-only mode enabled"
            shift
            ;;
        --serve)
            SERVE_DEMO=true
            print_status "Demo server mode enabled"
            shift
            ;;
        --output)
            OUTPUT_DIR="$2"
            print_status "Output directory: $OUTPUT_DIR"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Build Options:"
            echo "  --alpine           Use Alpine + Emscripten source build (slow)"
            echo "  --build            Use pre-built Emscripten image (default)"
            echo "  --demo             Use lightweight demo build (fast)"
            echo "  --enhanced         Use enhanced demo with RDF parsing (fast)"
            echo "  --real             Use real QLever implementation (experimental)"
            echo ""
            echo "Execution Options:"
            echo "  --build-only       Only build, don't extract files"
            echo "  --serve            Start demo server after build"
            echo "  --output DIR       Extract files to DIR (default: ./wasm-build)"
            echo ""
            echo "Examples:"
            echo "  $0                 # Default build with Emscripten image"
            echo "  $0 --demo          # Fast demo build"
            echo "  $0 --enhanced      # Enhanced demo with RDF parsing"
            echo "  $0 --real          # Real implementation (slow)"
            echo "  $0 --build-only    # Just build container"
            echo "  $0 --serve         # Build and start demo server"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Create output directory
mkdir -p "$OUTPUT_DIR"

print_status "Building QLever WASM files using $DOCKERFILE..."

# Estimate build time
if [ "$DOCKERFILE" = "Dockerfile.wasm.alpine" ]; then
    print_warning "This build will take 30-60 minutes (building Emscripten from source)"
    echo "Consider using --emscripten for faster builds"
    echo ""
    read -p "Continue with Alpine build? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        print_status "Build cancelled. Use --emscripten for faster builds."
        exit 0
    fi
else
    print_status "Estimated build time: 10-20 minutes (using pre-built Emscripten)"
fi

# Build the WASM files in Docker
print_status "Building WASM module in Docker container..."

# Create a temporary container to build and extract files
CONTAINER_NAME="qlever-wasm-builder-$(date +%s)"

# Build the Docker image
if ! docker build -f Dockerfiles/$DOCKERFILE -t qlever-wasm-builder .; then
    print_error "Failed to build Docker image"
    exit 1
fi

print_success "Docker build completed!"

# Run container to extract the built files
print_status "Extracting WASM files from container..."

# Create a temporary container and copy files out
docker create --name "$CONTAINER_NAME" qlever-wasm-builder > /dev/null

# Copy the WASM files
if [ "$DOCKERFILE" = "Dockerfile.wasm.enhanced" ]; then
    # Enhanced build stores files in /output/
    if docker cp "$CONTAINER_NAME":/output/qlever-wasm-enhanced.js "$OUTPUT_DIR/qlever-wasm.js"; then
        print_success "Copied qlever-wasm-enhanced.js as qlever-wasm.js"
    else
        print_error "Failed to copy qlever-wasm-enhanced.js"
        docker rm "$CONTAINER_NAME" > /dev/null
        exit 1
    fi

    if docker cp "$CONTAINER_NAME":/output/qlever-wasm-enhanced.wasm "$OUTPUT_DIR/qlever-wasm.wasm"; then
        print_success "Copied qlever-wasm-enhanced.wasm as qlever-wasm.wasm"
    else
        print_error "Failed to copy qlever-wasm-enhanced.wasm"
        docker rm "$CONTAINER_NAME" > /dev/null
        exit 1
    fi
else
    # Standard build stores files in /qlever/build-wasm/
    if docker cp "$CONTAINER_NAME":/qlever/build-wasm/qlever-wasm.js "$OUTPUT_DIR/"; then
        print_success "Copied qlever-wasm.js"
    else
        print_error "Failed to copy qlever-wasm.js"
        docker rm "$CONTAINER_NAME" > /dev/null
        exit 1
    fi

    if docker cp "$CONTAINER_NAME":/qlever/build-wasm/qlever-wasm.wasm "$OUTPUT_DIR/"; then
        print_success "Copied qlever-wasm.wasm"
    else
        print_error "Failed to copy qlever-wasm.wasm"
        docker rm "$CONTAINER_NAME" > /dev/null
        exit 1
    fi
fi

# Copy example files
cp examples/wasm/index.html "$OUTPUT_DIR/"
cp examples/wasm/README.md "$OUTPUT_DIR/"

# Clean up container
docker rm "$CONTAINER_NAME" > /dev/null

print_success "WASM files successfully built and extracted!"
echo ""
echo "Files available in: $OUTPUT_DIR"
echo "  • qlever-wasm.js    - JavaScript loader"
echo "  • qlever-wasm.wasm  - WebAssembly binary" 
echo "  • index.html        - Demo application"
echo "  • README.md         - Documentation"
echo ""

# Get file sizes
JS_SIZE=$(du -h "$OUTPUT_DIR/qlever-wasm.js" | cut -f1)
WASM_SIZE=$(du -h "$OUTPUT_DIR/qlever-wasm.wasm" | cut -f1)

echo "File sizes:"
echo "  • JavaScript: $JS_SIZE"
echo "  • WASM: $WASM_SIZE"
echo ""

if [ "$BUILD_ONLY" = true ]; then
    print_status "Build complete. You can now serve the files with any web server."
    echo ""
    echo "To test locally:"
    echo "  cd $OUTPUT_DIR"
    echo "  python3 -m http.server 8080"
    echo "  # Open http://localhost:8080"
    exit 0
fi

# Serve the demo
if [ "$SERVE_DEMO" = true ] || [ "$BUILD_ONLY" = false ]; then
    print_status "Starting demo server on port 8080..."
    echo "You can access the QLever WASM demo at: http://localhost:8080"
    echo ""
    echo "The demo includes:"
    echo "  • Real QLever WASM functionality"
    echo "  • Interactive SPARQL query interface"
    echo "  • Multiple output formats (JSON, CSV, TSV, XML)"
    echo "  • Example queries to get started"
    echo ""
    echo "Press Ctrl+C to stop the server"
    echo ""
    
    cd "$OUTPUT_DIR"
    python3 -m http.server 8080
fi