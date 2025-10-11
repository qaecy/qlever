# CLI utilities library
add_library(cliUtils
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