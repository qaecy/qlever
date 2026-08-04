#pragma once

#include <functional>
#include <string>

#include "LineChunker.h"
#include "QleverCliContext.h"
#include "RdfOutputUtils.h"
#include "util/json.h"

namespace cli_utils {

/**
 * @brief Query execution utilities for CLI operations
 */
class QueryExecutor {
 private:
  std::shared_ptr<qlever::QleverCliContext> qlever_;

 public:
  // Consumer of serialized output chunks, in the order they are produced.
  using OutputSink = std::function<void(const std::string&)>;

  explicit QueryExecutor(std::shared_ptr<qlever::QleverCliContext> qlever);

  // Execute a SELECT/ASK query, handing each chunk to `sink` as it is produced.
  //
  // Prefer this over `executeQuery` for anything whose result goes to a client or
  // a file: the result is serialized lazily in ~1MB chunks, so buffering it whole
  // makes peak memory scale with the response size, on the default allocator and
  // therefore outside `--allocator-memory-gb`. See
  // `QleverCliContext::queryToSink`.
  void executeQueryToSink(const std::string& query, const std::string& format,
                          const OutputSink& sink);

  // Execute a CONSTRUCT/DESCRIBE query, handing each chunk to `sink`.
  // For `nq`, the default graph is appended per line as chunks arrive.
  void executeConstructQueryToSink(const std::string& query,
                                   const std::string& outputFormat,
                                   const OutputSink& sink);

  // Execute regular SPARQL query (SELECT, ASK, etc.), buffering the whole result.
  std::string executeQuery(const std::string& query,
                           const std::string& format = "sparql-json");

  // Execute CONSTRUCT query with streaming output to file
  void executeConstructQuery(const std::string& query,
                             const std::string& outputFormat,
                             const std::string& outputFile = "");

  // Execute CONSTRUCT query and return result as string (for CLI output)
  std::string executeConstructQueryToString(const std::string& query,
                                            const std::string& outputFormat);

  // Extract value from JSON string (utility function)
  static std::string extractValue(const std::string& json,
                                  const std::string& key);
};

}  // namespace cli_utils
