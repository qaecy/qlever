//  Copyright 2026 Qaecy.
//
//  You may not use this file except in compliance with the Apache 2.0 License,
//  which can be found in the `LICENSE` file at the root of the QLever project.

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <optional>
#include <string>
#include <string_view>

#include "libqlever/python/EmbeddedEngine.h"

namespace nb = nanobind;
using namespace nb::literals;
using qlever::embedded::EmbeddedEngine;

namespace {

// The engine's JSON answers become Python dicts through the standard library,
// which keeps the module free of a JSON-to-Python converter of its own.
nb::object asPython(const std::string& json) {
  return nb::module_::import_("json").attr("loads")(json);
}

}  // namespace

// The `qlever_embedded` module: an index held open inside the Python process.
//
// Every call that does work releases the GIL, so queries on other threads run
// in parallel with it; a query re-acquires the GIL only to hand each result
// chunk (about 1 MB) to the caller's `sink`. A C++ exception becomes a Python
// exception (`ValueError` for an invalid argument, `RuntimeError` otherwise);
// an exception raised by `sink` stops the query and propagates unchanged.
NB_MODULE(qlever_embedded, m) {
  m.doc() = "QLever, embedded: one open index per Engine, many readers, one writer";

  nb::class_<EmbeddedEngine>(m, "Engine")
      .def(nb::init<std::string, double, bool>(), "base_name"_a,
           "memory_gb"_a = 4.0, "persist_updates"_a = true,
           nb::call_guard<nb::gil_scoped_release>(),
           "Open the index with this base name, replaying its update layer once.")
      .def(
          "query",
          [](const EmbeddedEngine& engine, std::string sparql,
             std::string format, nb::callable sink) {
            nb::gil_scoped_release released;
            engine.query(std::move(sparql), std::move(format),
                         [&sink](std::string_view chunk) {
                           nb::gil_scoped_acquire acquired;
                           sink(nb::bytes(chunk.data(), chunk.size()));
                         });
          },
          "sparql"_a, "format"_a = "", "sink"_a,
          "Run a query, passing the serialized result to sink(bytes) in order.")
      .def(
          "update",
          [](EmbeddedEngine& engine, std::string sparql) {
            std::string result;
            {
              nb::gil_scoped_release released;
              result = engine.update(std::move(sparql)).dump();
            }
            return asPython(result);
          },
          "sparql"_a, "Apply a SPARQL UPDATE atomically.")
      .def(
          "write_file",
          [](EmbeddedEngine& engine, const std::string& path,
             const std::string& format, const std::string& graph,
             bool delete_) {
            std::string result;
            {
              nb::gil_scoped_release released;
              result = engine.writeFile(path, format, graph, delete_).dump();
            }
            return asPython(result);
          },
          "path"_a, "format"_a = "nq", "graph"_a = "", "delete"_a = false,
          "Insert (or delete) every triple of an RDF file atomically.")
      .def(
          "counts",
          [](const EmbeddedEngine& engine) {
            std::string result;
            {
              nb::gil_scoped_release released;
              result = engine.counts().dump();
            }
            return asPython(result);
          },
          "Delta triple counts and the triples of the base index.")
      .def(
          "rebuild",
          [](EmbeddedEngine& engine, const std::string& keep_previous) {
            std::string result;
            {
              nb::gil_scoped_release released;
              result = engine.rebuild(keep_previous).dump();
            }
            return asPython(result);
          },
          "keep_previous"_a = "none",
          "Merge the update layer into a new index and swap it in; queries and "
          "writes continue meanwhile.")
      .def("write_view", &EmbeddedEngine::writeView, "name"_a, "sparql"_a,
           nb::call_guard<nb::gil_scoped_release>())
      .def("load_view", &EmbeddedEngine::loadView, "name"_a,
           nb::call_guard<nb::gil_scoped_release>())
      .def_prop_ro("base_name", &EmbeddedEngine::baseName);
}
