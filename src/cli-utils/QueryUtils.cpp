// Consolidated includes
#include "QueryUtils.h"

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

// =============================================================================
// QueryExecutor Implementation
// =============================================================================

std::string QueryExecutor::executeConstructQueryToString(
    const std::string& query, const std::string& outputFormat) {
  // Only nt and nq supported
  if (outputFormat != "nt" && outputFormat != "nq") {
    throw std::invalid_argument(
        "Only nt and nq formats are supported for CONSTRUCT queries");
  }
  // QLever always returns CONSTRUCT as Turtle (NT-compatible), so we can use
  // the result directly
  std::string rawResults;
  // Redirect QLever's verbose logging
  std::streambuf* clogBuf = std::clog.rdbuf();
  std::ofstream devNull("/dev/null");
  std::clog.rdbuf(devNull.rdbuf());
  try {
    rawResults = qlever_->query(query, ad_utility::MediaType::turtle);
    std::clog.rdbuf(clogBuf);
  } catch (...) {
    std::clog.rdbuf(clogBuf);
    throw;
  }
  // If nq is requested, we need to convert each line to N-Quads (add default
  // graph)
  if (outputFormat == "nq") {
    std::istringstream in(rawResults);
    std::ostringstream out;
    std::string line;
    // Use default graph: <http://default.graph/>
    const std::string defaultGraph = " <http://default.graph/> .\n";
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '.') {
        // Remove trailing dot and add default graph
        line.pop_back();
        out << line << defaultGraph;
      }
    }
    return out.str();
  }
  // Otherwise, just return as NT
  return rawResults;
}

QueryExecutor::QueryExecutor(std::shared_ptr<qlever::QleverCliContext> qlever)
    : qlever_(qlever) {}

std::string QueryExecutor::executeQuery(const std::string& query,
                                        const std::string& format) {
  // Convert format string to MediaType
  ad_utility::MediaType mediaType;
  if (format == "csv") {
    mediaType = ad_utility::MediaType::csv;
  } else if (format == "tsv") {
    mediaType = ad_utility::MediaType::tsv;
  } else if (format == "sparql-xml") {
    mediaType = ad_utility::MediaType::sparqlXml;
  } else if (format == "qlever-json") {
    mediaType = ad_utility::MediaType::qleverJson;
  } else {
    mediaType = ad_utility::MediaType::sparqlJson;  // default
  }

  // Redirect QLever's verbose logging to /dev/null during query execution
  std::streambuf* clogBuf = std::clog.rdbuf();
  std::ofstream devNull("/dev/null");
  std::clog.rdbuf(devNull.rdbuf());

  try {
    std::string result = qlever_->query(query, mediaType);
    std::clog.rdbuf(clogBuf);  // Restore logging
    return result;
  } catch (...) {
    std::clog.rdbuf(clogBuf);  // Restore logging even on exception
    throw;
  }
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

  // Execute query to get raw RDF results
  std::string rawResults;
  try {
    // Redirect QLever's verbose logging
    std::streambuf* clogBuf = std::clog.rdbuf();
    std::ofstream devNull("/dev/null");
    std::clog.rdbuf(devNull.rdbuf());

    // Use turtle format - QLever's native CONSTRUCT output format
    rawResults = qlever_->query(query, ad_utility::MediaType::turtle);

    std::clog.rdbuf(clogBuf);  // Restore logging
  } catch (const std::exception& e) {
    throw std::runtime_error("Query execution failed: " +
                             std::string(e.what()));
  }

  // Process results line by line
  std::istringstream resultStream(rawResults);
  std::string line;
  size_t tripleCount = 0;

  while (std::getline(resultStream, line)) {
    if (!line.empty() && line.back() == '.') {
      // Write the line directly as it's already in the correct format from
      // QLever
      writer.writeRawTriple(line + "\n");
      tripleCount++;

      // Progress logging
      if (progress.shouldLog()) {
        progress.logProgress(tripleCount, "triples");
      }
    }
  }

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
