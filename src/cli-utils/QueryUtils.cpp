// Consolidated includes
#include "QueryUtils.h"
#include "StreamSuppressor.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "util/Log.h"
#include "util/http/MediaTypes.h"
#include "util/json.h"

namespace cli_utils {

namespace {

// Map a CLI format name to the media type the exporter understands. Anything
// unrecognised falls back to `sparqlJson`, matching the previous behaviour.
ad_utility::MediaType mediaTypeForFormat(const std::string& format) {
  if (format == "csv") return ad_utility::MediaType::csv;
  if (format == "tsv") return ad_utility::MediaType::tsv;
  if (format == "sparql-xml") return ad_utility::MediaType::sparqlXml;
  if (format == "qlever-json") return ad_utility::MediaType::qleverJson;
  return ad_utility::MediaType::sparqlJson;
}

}  // namespace

// =============================================================================
// QueryExecutor Implementation
// =============================================================================

void QueryExecutor::executeConstructQueryToSink(const std::string& query,
                                                const std::string& outputFormat,
                                                const OutputSink& sink) {
  // Only nt and nq supported
  if (outputFormat != "nt" && outputFormat != "nq") {
    throw std::invalid_argument(
        "Only nt and nq formats are supported for CONSTRUCT queries");
  }
  cli_utils::SuppressStreams suppress;
  // QLever always returns CONSTRUCT as Turtle (NT-compatible), so for `nt` the
  // chunks pass straight through untouched.
  if (outputFormat == "nt") {
    qlever_->queryToSink(query, sink, ad_utility::MediaType::turtle);
    return;
  }
  LineChunker chunker{[&sink](const std::string& line) {
    if (isCompleteTripleLine(line)) sink(toDefaultGraphQuad(line));
  }};
  qlever_->queryToSink(
      query, [&chunker](const std::string& chunk) { chunker.feed(chunk); },
      ad_utility::MediaType::turtle);
  chunker.finish();
}

std::string QueryExecutor::executeConstructQueryToString(
    const std::string& query, const std::string& outputFormat) {
  std::string result;
  executeConstructQueryToSink(
      query, outputFormat,
      [&result](const std::string& chunk) { result += chunk; });
  return result;
}

QueryExecutor::QueryExecutor(std::shared_ptr<qlever::QleverCliContext> qlever)
    : qlever_(std::move(qlever)) {}

void QueryExecutor::executeQueryToSink(const std::string& query,
                                       const std::string& format,
                                       const OutputSink& sink) {
  cli_utils::SuppressStreams suppress;
  qlever_->queryToSink(query, sink, mediaTypeForFormat(format));
}

std::string QueryExecutor::executeQuery(const std::string& query,
                                        const std::string& format) {
  cli_utils::SuppressStreams suppress;
  return qlever_->query(query, mediaTypeForFormat(format));
}

void QueryExecutor::executeConstructQuery(const std::string& query,
                                          const std::string& outputFormat,
                                          const std::string& outputFile) {
  // Let QLever handle query validation; do not pre-check for CONSTRUCT

  // Create output writer
  RdfOutputWriter writer(outputFormat, outputFile);
  writer.writePrefixes();

  ProgressTracker progress;
  progress.start();

  std::cerr << "Executing CONSTRUCT query";
  if (!outputFile.empty()) {
    std::cerr << ", output: " << outputFile;
    if (writer.isUsingGzip()) std::cerr << " (gzipped)";
  }
  std::cerr << std::endl;

  // Stream the result to the writer as it is produced. This used to buffer the
  // ENTIRE serialized result into a `std::string` first, which defeats the point
  // of a to-file export: the whole subgraph had to fit in memory before a single
  // byte reached the disk, and that buffer is outside `--allocator-memory-gb`.
  size_t tripleCount = 0;
  LineChunker chunker{[&](const std::string& line) {
    if (!isCompleteTripleLine(line)) return;
    // Already in the correct format from QLever, so write it as-is.
    writer.writeRawTriple(line + "\n");
    tripleCount++;
    if (progress.shouldLog()) {
      progress.logProgress(tripleCount, "triples");
    }
  }};
  {
    cli_utils::SuppressStreams suppress;
    qlever_->queryToSink(
        query, [&chunker](const std::string& chunk) { chunker.feed(chunk); },
        ad_utility::MediaType::turtle);
  }
  chunker.finish();

  writer.flush();

  std::cerr << "CONSTRUCT query completed. Total triples: " << tripleCount;
  if (progress.getElapsedTime().count() > 0) {
    std::cerr << " ("
              << static_cast<int>(progress.getItemsPerSecond(tripleCount))
              << "/sec)";
  }
  std::cerr << std::endl;
}

std::string QueryExecutor::extractValue(const std::string& json,
                                        const std::string& key) {
  try {
    auto parsed = nlohmann::json::parse(json);
    if (parsed.contains(key)) {
      return parsed[key].get<std::string>();
    }
  } catch (const std::exception&) {
    // Fall back to simple string search
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos != std::string::npos) {
      size_t colonPos = json.find(":", keyPos);
      if (colonPos != std::string::npos) {
        size_t valueStart = json.find("\"", colonPos);
        if (valueStart != std::string::npos) {
          valueStart++;
          size_t valueEnd = json.find("\"", valueStart);
          if (valueEnd != std::string::npos) {
            return json.substr(valueStart, valueEnd - valueStart);
          }
        }
      }
    }
  }
  return "";
}

}  // namespace cli_utils
