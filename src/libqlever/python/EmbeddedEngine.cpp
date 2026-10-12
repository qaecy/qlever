//  Copyright 2026 Qaecy.
//
//  You may not use this file except in compliance with the Apache 2.0 License,
//  which can be found in the `LICENSE` file at the root of the QLever project.

#include "libqlever/python/EmbeddedEngine.h"

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "cli-utils/LineChunker.h"
#include "cli-utils/QueryTypeDetect.h"
#include "engine/ExportQueryExecutionTrees.h"
#include "engine/KeepPreviousIndexDirs.h"
#include "index/DeltaTriples.h"
#include "index/InputFileSpecification.h"
#include "index/LocalVocab.h"
#include "index/TripleComponentConversions.h"
#include "parser/RdfParser.h"
#include "parser/SparqlParser.h"
#include "parser/TokenizerCtre.h"
#include "util/CancellationHandle.h"
#include "util/HashMap.h"
#include "util/MemorySize/MemorySize.h"
#include "util/Timer.h"
#include "util/http/MediaTypes.h"

namespace qlever::embedded {

namespace {

using json = nlohmann::json;

EngineConfig makeConfig(const std::string& baseName, double memoryLimitGb,
                        bool persistUpdates) {
  EngineConfig config;
  config.baseName_ = baseName;
  config.memoryLimit_ = ad_utility::MemorySize::gigabytes(memoryLimitGb);
  config.persistUpdates_ = persistUpdates;
  return config;
}

bool isGraphQuery(const std::string& type) {
  return type == "CONSTRUCT" || type == "DESCRIBE";
}

// The format `qlever-cli query` would pick, validated against the query type.
std::string resolveFormat(const std::string& type, std::string format) {
  if (format.empty()) {
    return isGraphQuery(type) ? "nt" : "sparql-json";
  }
  if (isGraphQuery(type) && format != "nt" && format != "nq") {
    throw std::invalid_argument("Unsupported format for " + type + ": " +
                                format + ". Use nt or nq.");
  }
  if (!isGraphQuery(type) && format != "sparql-json" && format != "csv" &&
      format != "tsv") {
    throw std::invalid_argument("Unsupported format for " + type + ": " +
                                format + ". Use sparql-json, csv, or tsv.");
  }
  return format;
}

ad_utility::MediaType mediaTypeOf(const std::string& format) {
  if (format == "nt" || format == "nq") return ad_utility::MediaType::turtle;
  if (format == "csv") return ad_utility::MediaType::csv;
  if (format == "tsv") return ad_utility::MediaType::tsv;
  return ad_utility::MediaType::sparqlJson;
}

json countsJson(const DeltaTriplesCount& count) {
  return json{{"inserted", count.triplesInserted_},
              {"deleted", count.triplesDeleted_}};
}

KeepPreviousIndexDirs keepPolicy(const std::string& name) {
  using E = detail::KeepPreviousIndexDirsEnum;
  if (name == "all") return KeepPreviousIndexDirs{E::All};
  if (name == "none") return KeepPreviousIndexDirs{E::None};
  if (name == "original-only") return KeepPreviousIndexDirs{E::OriginalOnly};
  if (name == "most-recent-only") {
    return KeepPreviousIndexDirs{E::MostRecentOnly};
  }
  if (name == "original-and-most-recent") {
    return KeepPreviousIndexDirs{E::OriginalAndMostRecent};
  }
  throw std::invalid_argument("Unknown keep-previous policy: " + name);
}

}  // namespace

// _____________________________________________________________________________
EmbeddedEngine::EmbeddedEngine(std::string baseName, double memoryLimitGb,
                               bool persistUpdates)
    : baseName_{std::move(baseName)},
      qlever_{makeConfig(baseName_, memoryLimitGb, persistUpdates)} {}

// _____________________________________________________________________________
void EmbeddedEngine::query(std::string sparql, std::string format,
                           const Sink& sink) const {
  auto type = cli_utils::detectQueryType(sparql);
  format = resolveFormat(type, std::move(format));
  // Planned against a snapshot of the index and its delta triples, which the
  // `PlannedQuery` keeps alive until the export below has finished.
  auto planned = qlever_.parseAndPlanQuery(std::move(sparql));
  const auto& handle =
      planned.queryExecutionTree().getRootOperation()->getCancellationHandle();
  ad_utility::Timer timer{ad_utility::Timer::Started};
  auto chunks = ExportQueryExecutionTrees::computeResult(
      planned.parsedQuery(), planned.queryExecutionTree(), mediaTypeOf(format),
      timer, handle);
  if (format != "nq") {
    for (const auto& chunk : chunks) {
      sink(chunk);
    }
    return;
  }
  // QLever exports a graph result as N-Triples; `nq` puts each triple in the
  // default graph, line by line, as `qlever-cli` does.
  std::string pending;
  cli_utils::LineChunker lines{[&pending](const std::string& line) {
    if (cli_utils::isCompleteTripleLine(line)) {
      pending += cli_utils::toDefaultGraphQuad(line);
    }
  }};
  for (const auto& chunk : chunks) {
    lines.feed(chunk);
    if (!pending.empty()) {
      sink(pending);
      pending.clear();
    }
  }
  lines.finish();
  if (!pending.empty()) sink(pending);
}

// _____________________________________________________________________________
json EmbeddedEngine::update(std::string sparql) {
  std::lock_guard lock{writeMutex_};
  auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
  auto indexAndViews = qlever_.indexAndViewsSnapshot();
  auto& index = indexAndViews->index_;
  auto qec = qlever_.createQueryExecutionContext(indexAndViews);
  auto updates =
      SparqlParser::parseUpdate(index.getBlankNodeManager(),
                                &index.encodedIriManager(), std::move(sparql));
  return index.deltaTriplesManager().modify<json>(
      [this, &handle, &qec, &updates](DeltaTriples& deltaTriples) {
        qec->setLocatedTriplesForEvaluation(
            deltaTriples.getLocatedTriplesSharedStateReference());
        std::optional<DeltaTriplesCount> before;
        std::optional<DeltaTriplesCount> after;
        for (size_t i = 0; i < updates.size(); ++i) {
          // An update with a WHERE clause reads the augmented block metadata,
          // which `modify` only refreshes at its end.
          if (i != 0 && !updates[i]._rootGraphPattern._graphPatterns.empty()) {
            deltaTriples.updateAugmentedMetadata();
          }
          auto planned = qlever_.planQuery(std::move(updates[i]), *qec, handle,
                                           std::nullopt);
          auto metadata = qlever_.applyUpdate(planned, handle, deltaTriples);
          if (!before) before = metadata.countBefore_;
          after = metadata.countAfter_;
        }
        auto current = deltaTriples.getCounts();
        return json{{"before", countsJson(before.value_or(current))},
                    {"after", countsJson(after.value_or(current))}};
      });
}

// _____________________________________________________________________________
json EmbeddedEngine::writeFile(const std::string& path,
                               const std::string& format,
                               const std::string& defaultGraph,
                               bool isDelete) {
  Filetype filetype;
  if (format == "nq") {
    filetype = Filetype::NQuad;
  } else if (format == "nt" || format == "ttl" || format == "turtle") {
    filetype = Filetype::Turtle;
  } else {
    throw std::invalid_argument("Unsupported format: " + format +
                                ". Use nq, nt or ttl.");
  }
  std::lock_guard lock{writeMutex_};
  auto indexAndViews = qlever_.indexAndViewsSnapshot();
  auto& index = indexAndViews->index_;
  TripleComponent graph{ad_utility::triple_component::Iri::fromIriref(
      defaultGraph.empty() ? std::string(DEFAULT_GRAPH_IRI)
                           : "<" + defaultGraph + ">")};
  InputFileSpecification spec;
  spec.source_ = path;
  spec.filetype_ = filetype;
  std::unique_ptr<RdfParserBase> parser;
  if (filetype == Filetype::NQuad) {
    parser = std::make_unique<RdfStreamParser<NQuadParser<TokenizerCtre>>>(
        spec, DEFAULT_PARSER_BUFFER_SIZE, &index.encodedIriManager(),
        std::move(graph));
  } else {
    parser = std::make_unique<RdfStreamParser<TurtleParser<TokenizerCtre>>>(
        spec, DEFAULT_PARSER_BUFFER_SIZE, &index.encodedIriManager(),
        std::move(graph));
  }
  const auto& indexImpl = index.getImpl();
  auto* blankNodeManager = index.getBlankNodeManager();
  size_t processed = 0;
  // One `modify` for the whole file, so it is persisted and published once,
  // after the last batch; a parse error leaves the delta as it was.
  index.deltaTriplesManager().modify<void>([&](DeltaTriples& deltaTriples) {
    auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
    LocalVocab localVocab;
    // A blank-node label keeps one `Id` across the batches of one file.
    ad_utility::HashMap<std::string, Id> blankNodes;
    auto toId = [&](TripleComponent component) -> Id {
      if (component.isString()) {
        auto [it, isNew] =
            blankNodes.try_emplace(component.getString(), Id::makeUndefined());
        if (isNew) {
          it->second = Id::makeFromBlankNodeIndex(
              localVocab.getBlankNodeIndex(blankNodeManager));
        }
        return it->second;
      }
      return toValueId(std::move(component), indexImpl, localVocab);
    };
    while (auto batch = parser->getBatch()) {
      if (batch->empty()) continue;
      std::vector<IdTriple<0>> triples;
      triples.reserve(batch->size());
      for (auto& triple : batch.value()) {
        triples.push_back(IdTriple<0>{std::array{
            toId(std::move(triple.subject_)), toId(std::move(triple.predicate_)),
            toId(std::move(triple.object_)), toId(std::move(triple.graphIri_))}});
      }
      if (isDelete) {
        deltaTriples.deleteTriples<DeltaTriples::Consolidate::No>(
            handle, std::move(triples));
      } else {
        deltaTriples.insertTriples<DeltaTriples::Consolidate::No>(
            handle, std::move(triples));
      }
      localVocab = LocalVocab{};
      processed += batch->size();
    }
    deltaTriples.consolidateAll();
  });
  return json{{"processed", processed}, {"counts", counts()["delta"]}};
}

// _____________________________________________________________________________
json EmbeddedEngine::counts() const {
  auto indexAndViews = qlever_.indexAndViewsSnapshot();
  auto& index = indexAndViews->index_;
  auto delta = index.deltaTriplesManager().modify<DeltaTriplesCount>(
      [](DeltaTriples& deltaTriples) { return deltaTriples.getCounts(); },
      /*writeToDiskAfterRequest=*/false,
      /*updateMetadataAfterRequest=*/false);
  return json{{"delta", countsJson(delta)},
              {"triples", index.numTriples().normal}};
}

// _____________________________________________________________________________
json EmbeddedEngine::rebuild(const std::string& keepPrevious) {
  std::unique_lock rebuilding{rebuildMutex_, std::try_to_lock};
  if (!rebuilding.owns_lock()) {
    throw std::runtime_error("A rebuild of " + baseName_ +
                             " is already in progress");
  }
  auto policy = keepPolicy(keepPrevious);
  auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
  auto indexAndViews = qlever_.indexAndViewsSnapshot();
  auto& [index, views] = *indexAndViews;
  // Explicit directories would be resolved against the host process's working
  // directory; the defaults lie beside the index.
  auto config =
      Qlever::makeIndexRebuildConfig(index, std::nullopt, std::nullopt);
  ad_utility::Timer timer{ad_utility::Timer::Started};
  auto built = qlever_.rebuildIndexToDisk(index, config, handle);
  auto buildMs = timer.msecs().count();
  {
    // No update may land between the diff of the delta triples and the swap.
    std::lock_guard lock{writeMutex_};
    views.retireOnDiskFiles();
    qlever_.swapInRebuiltIndex(index, std::move(built), handle, config,
                               policy);
  }
  return json{{"baseName", config.newIndexTarget()},
              {"previous", config.oldIndexTarget()},
              {"buildMs", buildMs},
              {"totalMs", timer.msecs().count()}};
}

// _____________________________________________________________________________
void EmbeddedEngine::writeView(const std::string& name, std::string sparql) {
  qlever_.writeMaterializedView(name, std::move(sparql));
}

// _____________________________________________________________________________
void EmbeddedEngine::loadView(const std::string& name) const {
  qlever_.loadMaterializedView(name);
}

}  // namespace qlever::embedded
