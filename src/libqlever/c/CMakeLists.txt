# `libqlever_c.so`: the C ABI over `EmbeddedEngine` (see `qlever.h`), for hosts
# that load QLever through a C FFI (Rust's `qlever-sys`). Built only with
# `-DQLEVER_C_BINDINGS=ON`; every library it links must be position-independent,
# so configure the whole build with `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`.
add_library(qlever_c SHARED
        ../embedded/EmbeddedEngine.cpp
        QleverC.cpp)
qlever_target_link_libraries(qlever_c qlever cliUtils engine index
        rdfParser sparqlParser qlever_util)

# Export only `qlever_*`. The libraries linked in statically (abseil, re2,
# boost, ...) stay local, so a host that loads its own copies of them cannot
# end up calling into a mix of the two.
target_link_options(qlever_c PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qlever_c.map"
        "LINKER:--exclude-libs,ALL")
set_property(TARGET qlever_c APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever_c.map)

# A plain C program against the installed header: proves the ABI from C itself.
add_executable(qlever_c_test ${PROJECT_SOURCE_DIR}/test/c/qlever_c_test.c)
target_link_libraries(qlever_c_test qlever_c)
set_target_properties(qlever_c_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
