# CLI utilities library
add_library(cliUtils
    StreamSuppressor.cpp
    RdfOutputUtils.cpp
    QueryUtils.cpp
    IndexStatsUtils.cpp
    IndexBuilderUtils.cpp
)

target_link_libraries(cliUtils
    PUBLIC
        util
        qlever
        ${ZLIB_LIBRARIES}
)

target_include_directories(cliUtils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Lightweight library for unit tests that only exercise headers
# (StreamSuppressor, QleverCliContext struct layout) and MemorySize.
# Avoids pulling in qlever/engine/index and cuts the test build time
# dramatically.
add_library(cliUtilsLight STATIC StreamSuppressor.cpp)
target_link_libraries(cliUtilsLight PUBLIC memorySize)
target_include_directories(cliUtilsLight
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)