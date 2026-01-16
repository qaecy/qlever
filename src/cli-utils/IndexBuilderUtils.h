#pragma once

#include <chrono>
#include <string>

#include "util/json.h"

// Forward declaration
namespace qlever {
struct IndexBuilderConfig;
}

namespace cli_utils {

/**
 * @brief Utility class for building QLever indexes
 */
class IndexBuilder {
 public:
  /**
   * @brief Build an index from JSON configuration
   *
   * @param jsonInput JSON configuration containing:
   *   - input_files: array of file paths or objects with path/format
   *   - index_name: base name for the index
   *   - index_directory: directory to create index in (optional, default: ".")
   *   - memory_limit_gb: memory limit in GB (optional)
   *   - settings_file: path to settings file (optional)
   *   - keep_temp_files: whether to keep temporary files (optional)
   *   - vocabulary_type: vocabulary type for GeoSPARQL support (optional)
   *   - prefixes_for_id_encoded_iris: array of IRI prefixes (optional)
   *
   * @return JSON response with build results or error information
   */
  static nlohmann::json buildIndex(const nlohmann::json& jsonInput);

 private:
  /**
   * @brief Validate and process input files configuration
   *
   * @param inputFiles JSON array of input files
   * @param config IndexBuilderConfig to populate
   * @return Error message if validation fails, empty string if successful
   */
  static std::string processInputFiles(const nlohmann::json& inputFiles,
                                       qlever::IndexBuilderConfig& config);

  /**
   * @brief Process optional configuration parameters
   *
   * @param input JSON input configuration
   * @param config IndexBuilderConfig to populate
   * @return Error message if validation fails, empty string if successful
   */
  static std::string processOptionalParameters(
      const nlohmann::json& input, qlever::IndexBuilderConfig& config);

  /**
   * @brief Create error response JSON
   *
   * @param message Error message
   * @return JSON error response
   */
  static nlohmann::json createErrorResponse(const std::string& message);
};

}  // namespace cli_utils
