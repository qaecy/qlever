#pragma once

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
  explicit QueryExecutor(std::shared_ptr<qlever::QleverCliContext> qlever);

  // Execute regular SPARQL query (SELECT, ASK, etc.)
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
