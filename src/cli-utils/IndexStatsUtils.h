#pragma once

#include <memory>
#include <string>

#include "QleverCliContext.h"
#include "util/json.h"

namespace cli_utils {

/**
 * @brief Utility class for gathering comprehensive index statistics
 */
class IndexStatsCollector {
 private:
  std::shared_ptr<qlever::QleverCliContext> qlever_;

  // Helper method to run a stats query with timing and error handling
  void runStatsQuery(nlohmann::json& response, const std::string& name,
                     const std::string& query) const;

 public:
  explicit IndexStatsCollector(
      std::shared_ptr<qlever::QleverCliContext> qlever);

  /**
   * @brief Collect comprehensive statistics about the index
   * @param indexBasename The base name of the index
   * @return JSON object containing all statistics
   */
  nlohmann::json collectStats(const std::string& indexBasename) const;
};

}  // namespace cli_utils
