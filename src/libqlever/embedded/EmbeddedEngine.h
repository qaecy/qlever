//  Copyright 2026 Qaecy.
//
//  You may not use this file except in compliance with the Apache 2.0 License,
//  which can be found in the `LICENSE` file at the root of the QLever project.

#ifndef QLEVER_SRC_LIBQLEVER_EMBEDDED_EMBEDDEDENGINE_H
#define QLEVER_SRC_LIBQLEVER_EMBEDDED_EMBEDDEDENGINE_H

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "libqlever/Qlever.h"
#include "util/json.h"

namespace qlever::embedded {

// One index held open in the host process, for any number of concurrent
// readers and one writer at a time. It is the in-process counterpart of
// `qlever-cli`, which opens the index (and replays its `.update-triples`) on
// every call:
//
//  - Queries run against a snapshot of the index and its delta triples taken
//    when they start, so they never wait for an update or a rebuild.
//  - Updates and quad writes are serialized by `writeMutex_`, the role the
//    server's single-threaded update pool plays. Each one is applied in a
//    single `DeltaTriplesManager::modify` call, so it is atomic and persisted
//    once (with `persistUpdates`).
//  - A rebuild builds the new index from a snapshot while queries and updates
//    continue, then swaps it in under `writeMutex_`; the updates made in the
//    meantime are carried over by `Qlever::swapInRebuiltIndex`.
//
// Every method reports a failure by throwing; nothing here aborts the process.
class EmbeddedEngine {
 public:
  // Receives the serialized result in chunks of about 1 MB, in order.
  using Sink = std::function<void(std::string_view)>;

  EmbeddedEngine(std::string baseName, double memoryLimitGb,
                 bool persistUpdates);

  // Formats: `sparql-json`, `csv`, `tsv` (SELECT/ASK) and `nt`, `nq`
  // (CONSTRUCT/DESCRIBE). An empty format picks `sparql-json` or `nt` by the
  // query type, as `qlever-cli query` does. A positive `timeout` cancels the
  // query once it has passed, planning and export included, by throwing
  // `ad_utility::CancellationException`; zero means no limit.
  void query(std::string sparql, std::string format, const Sink& sink,
             std::chrono::milliseconds timeout = {}) const;

  // A SPARQL UPDATE, possibly several operations separated by `;`, applied
  // atomically. Returns the delta triple counts before and after.
  nlohmann::json update(std::string sparql);

  // Insert (or, with `isDelete`, delete) every triple of an N-Quads, N-Triples
  // or Turtle file atomically: a parse error leaves the delta untouched.
  // `defaultGraph` is the graph of triples that carry none (empty: QLever's
  // default graph).
  nlohmann::json writeFile(const std::string& path, const std::string& format,
                           const std::string& defaultGraph, bool isDelete);

  // Inserted and deleted delta triples, and the triples of the base index.
  nlohmann::json counts() const;

  // Rebuild the index with its delta triples merged in, then swap it in place.
  // The new index is built in `rebuild.<datetime>.tmp` beside the current one.
  // Blocks the caller for the duration of the build, but neither queries nor
  // writes. `keepPrevious` is one of `all`, `none`, `original-only`,
  // `most-recent-only`, `original-and-most-recent`. Throws if a rebuild is
  // already running.
  nlohmann::json rebuild(const std::string& keepPrevious);

  void writeView(const std::string& name, std::string sparql);
  void loadView(const std::string& name) const;

  const std::string& baseName() const { return baseName_; }

 private:
  std::string baseName_;
  Qlever qlever_;
  std::mutex writeMutex_;
  std::mutex rebuildMutex_;
};

}  // namespace qlever::embedded

#endif  // QLEVER_SRC_LIBQLEVER_EMBEDDED_EMBEDDEDENGINE_H
