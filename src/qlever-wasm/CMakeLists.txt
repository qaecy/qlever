# Only build the library if not using Emscripten (for linking tests, etc.)
if(NOT EMSCRIPTEN)
    add_library(qlever-wasm QleverWasm.cpp)
    qlever_target_link_libraries(qlever-wasm qlever parser engine util index absl::strings)
endif()

# WASM target - only build if using Emscripten
if(EMSCRIPTEN)
    add_executable(qlever-wasm-module QleverWasm.cpp)
    
    qlever_target_link_libraries(qlever-wasm-module qlever parser engine util index absl::strings)
    
    # Emscripten-specific settings
    set_target_properties(qlever-wasm-module PROPERTIES
        COMPILE_FLAGS "-s USE_PTHREADS=0 -s WASM=1 -s EXPORT_ALL=1 -O3"
        LINK_FLAGS "-s USE_PTHREADS=0 -s WASM=1 -s EXPORT_ALL=1 -s MODULARIZE=1 -s EXPORT_NAME='QleverModule' -s ENVIRONMENT='web,worker' -s ALLOW_MEMORY_GROWTH=1 -s MAXIMUM_MEMORY=2GB --bind -O3"
        OUTPUT_NAME "qlever-wasm"
        SUFFIX ".js"
    )
    
    # Copy WASM files to a convenient location after build
    add_custom_command(TARGET qlever-wasm-module POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_CURRENT_BINARY_DIR}/qlever-wasm.js 
            ${CMAKE_BINARY_DIR}/qlever-wasm.js
        COMMAND ${CMAKE_COMMAND} -E copy 
            ${CMAKE_CURRENT_BINARY_DIR}/qlever-wasm.wasm 
            ${CMAKE_BINARY_DIR}/qlever-wasm.wasm
        COMMENT "Copying WASM files to build root"
    )
endif()