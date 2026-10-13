# The `qlever_embedded` Python module (see `QleverEmbeddedModule.cpp`). Built
# only with `-DQLEVER_PYTHON_BINDINGS=ON`, against the Python found here (3.12,
# the version the consuming service runs). Every library it links must be
# position-independent, so configure the whole build with
# `-DCMAKE_POSITION_INDEPENDENT_CODE=ON`.
find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED)

FetchContent_Declare(
        nanobind
        GIT_REPOSITORY https://github.com/wjakob/nanobind.git
        GIT_TAG v2.4.0
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nanobind)

nanobind_add_module(qlever_embedded NB_STATIC
        EmbeddedEngine.cpp
        QleverEmbeddedModule.cpp)
qlever_target_link_libraries(qlever_embedded qlever cliUtils engine index
        rdfParser sparqlParser qlever_util)

# Export only the module's entry point. The libraries linked in statically
# (abseil, re2, boost, ...) otherwise export their symbols, and a host process
# that loads its own copies of them, as `pyarrow` does, ends up with the two
# versions mixed: a re2 regex compiled while parsing a query then called into
# the other abseil and crashed. Local symbols always bind within this module.
target_link_options(qlever_embedded PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/qlever_embedded.map"
        "LINKER:--exclude-libs,ALL")
set_property(TARGET qlever_embedded APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/qlever_embedded.map)
