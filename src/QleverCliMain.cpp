#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "QleverCliContext.h"
#include "cli-utils/IndexBuilderUtils.h"
#include "cli-utils/IndexStatsUtils.h"
#include "cli-utils/QueryUtils.h"
#include "cli-utils/RdfOutputUtils.h"
#include "engine/ExecuteUpdate.h"
#include "engine/QueryPlanner.h"
#include "index/InputFileSpecification.h"
#include "index/vocabulary/VocabularyType.h"
#include "parser/SparqlParser.h"
#include "util/Log.h"
#include "util/MemorySize/MemorySize.h"
#include "util/Timer.h"
#include "util/http/MediaTypes.h"
#include "util/json.h"

using json = nlohmann::json;

// JSON utility functions
json createErrorResponse(const std::string& error,
                         const std::string& query = "") {
  json response;
  response["success"] = false;
  response["error"] = error;
  if (!query.empty()) {
    response["query"] = query;
  }
  response["timestamp"] =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  return response;
}

json createSuccessResponse(const std::string& result, const std::string& query,
                           long executionTimeMs,
                           const std::string& format = "sparql-json") {
  json response;
  response["success"] = true;
  response["result"] = result;
  response["query"] = query;
  response["executionTimeMs"] = executionTimeMs;
  response["format"] = format;
  response["timestamp"] =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  return response;
}

json createSuccessResponse(const std::string& message) {
  json response;
  response["success"] = true;
  response["message"] = message;
  response["timestamp"] =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  return response;
}

ad_utility::MediaType getMediaType(const std::string& format) {
  if (format == "csv") return ad_utility::MediaType::csv;
  if (format == "tsv") return ad_utility::MediaType::tsv;
  if (format == "sparql-xml") return ad_utility::MediaType::sparqlXml;
  if (format == "qlever-json") return ad_utility::MediaType::qleverJson;
  return ad_utility::MediaType::sparqlJson;  // default
}

// Helper function to extract value from SPARQL JSON binding
std::string extractValue(const json& binding) {
  const auto& type = binding["type"];
  const auto& value = binding["value"];

  if (type == "uri") {
    return "<" + value.get<std::string>() + ">";
  } else if (type == "literal") {
    std::string result = "\"" + value.get<std::string>() + "\"";
    if (binding.contains("datatype")) {
      result += "^^<" + binding["datatype"].get<std::string>() + ">";
    } else if (binding.contains("xml:lang")) {
      result += "@" + binding["xml:lang"].get<std::string>();
    }
    return result;
  } else if (type == "bnode") {
    return "_:" + value.get<std::string>();
  }
  return value.get<std::string>();
}

void printUsage(const char* programName) {
  std::cerr << "Usage: " << programName << " <command> [options]\n\n";
  std::cerr << "Commands:\n";
  std::cerr << "  query       <index_basename> <sparql_query> [output_format] "
               "[name]  Execute SPARQL query (optionally pin result)\n";
  std::cerr << "  query-to-file <index_basename> <sparql_query> <format> "
               "<output_file>  Execute CONSTRUCT query to file\n";
  // std::cerr << "  query-json  <json_input>                      Execute query
  // from JSON input\n";
  std::cerr << "  update      <index_basename> <sparql_update_query>  Execute "
               "SPARQL UPDATE query\n";
  std::cerr << "  stats       <index_basename>                  Get index "
               "statistics\n";
  std::cerr << "  build-index <json_input>                      Build index "
               "from RDF files\n";
  std::cerr << "  serialize   <index_basename> <format> [output_file]  Dump "
               "database content\n";
  std::cerr
      << "                                                Formats: nt, nq\n";
  std::cerr << "                                                Add .gz to "
               "output_file for compression\n";
  std::cerr << "\nJSON input format for query-json:\n";
  std::cerr << "{\n";
  std::cerr << "  \"indexBasename\": \"path/to/index\",\n";
  std::cerr << "  \"query\": \"SELECT * WHERE { ?s ?p ?o } LIMIT 10\",\n";
  std::cerr
      << "  \"format\": \"sparql-json\"  // optional: sparql-json, csv, tsv\n";
  std::cerr << "}\n\n";
  std::cerr << "JSON input format for build-index:\n";
  std::cerr << "{\n";
  std::cerr << "  \"index_name\": \"my-index\",\n";
  std::cerr << "  \"index_directory\": \"/path/to/indices\",  // optional, "
               "defaults to current dir\n";
  std::cerr << "  \"input_files\": [\n";
  std::cerr << "    \"data.ttl\",\n";
  std::cerr << "    {\"path\": \"data.nt\", \"format\": \"nt\"},\n";
  std::cerr << "    {\"path\": \"data.nq\", \"format\": \"nq\", "
               "\"default_graph\": \"http://example.org/graph\"}\n";
  std::cerr << "  ],\n";
  std::cerr << "  \"memory_limit_gb\": 4,        // optional\n";
  std::cerr << "  \"settings_file\": \"settings.json\",  // optional\n";
  std::cerr << "  \"keep_temp_files\": false,    // optional\n";
  std::cerr << "  \"vocabulary_type\": \"on-disk-compressed-geo-split\",  // "
               "optional, for GeoSPARQL support\n";
  std::cerr
      << "  \"add_words_from_literals\": true  // optional, for text index\n";
  std::cerr
      << "  \"prefixes_for_id_encoded_iris\": [\"http://example.org/prefix/\"] "
         " // optional, for text index\n";
  std::cerr << "}\n";
  std::cerr << "\nVocabulary types:\n";
  std::cerr << "  in-memory-uncompressed     - Fast but uses more memory\n";
  std::cerr << "  on-disk-uncompressed       - Slower but uses less memory\n";
  std::cerr << "  in-memory-compressed       - Good balance (compressed, in "
               "memory)\n";
  std::cerr << "  on-disk-compressed         - Default (compressed, on disk)\n";
  std::cerr
      << "  on-disk-compressed-geo-split - Required for GeoSPARQL support\n";
}

// Helper to trim and uppercase a string (for query type detection)
#include <algorithm>
#include <cctype>
std::string trimAndUpper(const std::string& s) {
  auto start = s.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  auto end = s.find_first_of(" \t\n\r", start);
  std::string firstWord = s.substr(start, end - start);
  std::transform(firstWord.begin(), firstWord.end(), firstWord.begin(),
                 ::toupper);
  return firstWord;
}

int executeQuery(const std::string& indexBasename, const std::string& queryStr,
                 const std::string& userFormat = "",
                 const std::string& name = "") {
  try {
    // Detect query type
    std::string type = trimAndUpper(queryStr);
    std::string format = userFormat;
    // Set default format if not overridden
    if (format.empty()) {
      if (type == "SELECT" || type == "ASK") {
        format = "sparql-json";
      } else if (type == "CONSTRUCT" || type == "DESCRIBE") {
        format = "nt";
      } else {
        format = "sparql-json";  // fallback
      }
    }

    // Validate/normalize format for each query type
    if (type == "SELECT" || type == "ASK") {
      if (format != "sparql-json" && format != "csv" && format != "tsv") {
        throw std::runtime_error("Unsupported format for SELECT/ASK: " +
                                 format + ". Use sparql-json, csv, or tsv.");
      }
    } else if (type == "CONSTRUCT" || type == "DESCRIBE") {
      if (format != "nt" && format != "nq") {
        throw std::runtime_error("Unsupported format for CONSTRUCT/DESCRIBE: " +
                                 format + ". Use nt or nq.");
      }
    }

    // Redirect QLever's verbose logging to /dev/null during query execution
    std::streambuf* clogBuf = std::clog.rdbuf();
    std::streambuf* cerrBuf = std::cerr.rdbuf();
    std::ofstream devNull("/dev/null");
    std::clog.rdbuf(devNull.rdbuf());
    std::cerr.rdbuf(devNull.rdbuf());

    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::QueryExecutor executor(qlever);
    std::string result;

    if (type == "CONSTRUCT" || type == "DESCRIBE") {
      // For CONSTRUCT and DESCRIBE, use the construct executor
      // (no file output, just return as string)
      result = executor.executeConstructQueryToString(queryStr, format);
    } else {
      // For SELECT, ASK, use the standard executor
      result = executor.executeQuery(queryStr, format);
    }

    // If a name is provided, pin the result
    if (!name.empty()) {
      qlever->queryAndPinResultWithName(name, queryStr);
    }

    // Restore logging
    std::clog.rdbuf(clogBuf);
    std::cerr.rdbuf(cerrBuf);

    std::cout << result << std::endl;
    return 0;
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what());
    std::cerr << errorResponse.dump(2) << std::endl;
    return 1;
  }
}

// Execute a SPARQL UPDATE query on the given index
int executeUpdate(const std::string& indexBasename,
                  const std::string& updateQuery) {
  try {
    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);

    // Execute the update
    qlever->update(updateQuery);

    json response =
        createSuccessResponse("Update applied successfully.", updateQuery, 0);
    std::cout << response.dump() << std::endl;
    return 0;
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what(), updateQuery);
    std::cout << errorResponse.dump() << std::endl;
    return 1;
  }
}

int serializeDatabase(const std::string& indexBasename,
                      const std::string& format,
                      const std::string& outputFile = "") {
  try {
    if (format != "nt" && format != "nq") {
      throw std::runtime_error("Serialization only supports nt and nq formats");
    }

    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::DatabaseSerializer serializer(qlever);

    serializer.serialize(format, outputFile);

    if (!outputFile.empty()) {
      json response =
          createSuccessResponse("Database serialized to " + outputFile);
      std::cerr << response.dump(2) << std::endl;
    }

    return 0;
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what());
    std::cerr << errorResponse.dump(2) << std::endl;
    return 1;
  }
}

int buildIndex(const std::string& jsonInput) {
  try {
    json input = json::parse(jsonInput);
    json response = cli_utils::IndexBuilder::buildIndex(input);

    std::cout << response.dump() << std::endl;
    return response["success"].get<bool>() ? 0 : 1;

  } catch (const json::parse_error& e) {
    json response =
        createErrorResponse("Invalid JSON input: " + std::string(e.what()));
    std::cout << response.dump() << std::endl;
    return 1;
  } catch (const std::exception& e) {
    json response =
        createErrorResponse("Unexpected error: " + std::string(e.what()));
    std::cout << response.dump() << std::endl;
    return 1;
  }
}

int getIndexStats(const std::string& indexBasename) {
  try {
    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ =
        ad_utility::MemorySize::gigabytes(1);  // Minimal for stats

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::IndexStatsCollector statsCollector(qlever);

    json response = statsCollector.collectStats(indexBasename);
    std::cout << response.dump(2) << std::endl;
    return 0;

  } catch (const std::exception& e) {
    json response = createErrorResponse(e.what());
    response["indexBasename"] = indexBasename;
    std::cout << response.dump(2) << std::endl;
    return 1;
  }
}

int executeQueryToFile(const std::string& indexBasename,
                       const std::string& queryStr, const std::string& format,
                       const std::string& outputFile) {
  try {
    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::QueryExecutor executor(qlever);

    // Execute query to file
    ad_utility::Timer timer{ad_utility::Timer::Started};
    executor.executeConstructQuery(queryStr, format, outputFile);
    auto executionTime = timer.msecs().count();

    // Create success response
    json response;
    response["success"] = true;
    response["message"] = "Query executed and result written to file";
    response["indexBasename"] = indexBasename;
    response["query"] = queryStr;
    response["format"] = format;
    response["outputFile"] = outputFile;
    response["executionTimeMs"] = executionTime;
    response["timestamp"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    std::cout << response.dump() << std::endl;
    return 0;

  } catch (const std::exception& e) {
    json response =
        createErrorResponse("Query execution failed: " + std::string(e.what()));
    response["query"] = queryStr;
    std::cout << response.dump() << std::endl;
    return 1;
  }
}

int main(int argc, char* argv[]) {
  // Redirect QLever logging to stderr so only JSON goes to stdout
  ad_utility::setGlobalLoggingStream(&std::cerr);

  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  std::string command = argv[1];

  // Handle help flags
  if (command == "--help" || command == "-h" || command == "help") {
    printUsage(argv[0]);
    return 0;
  }

  try {
    if (command == "query" && (argc == 4 || argc == 5 || argc == 6)) {
      // query <index_basename> <sparql_query> [format] [name]
      std::string format = (argc >= 5) ? argv[4] : "";
      std::string name = (argc == 6) ? argv[5] : "";
      return executeQuery(argv[2], argv[3], format, name);
    } else if (command == "update" && argc == 4) {
      // update <index_basename> <sparql_update_query>
      return executeUpdate(argv[2], argv[3]);
    } else if (command == "query-to-file" && argc == 6) {
      return executeQueryToFile(argv[2], argv[3], argv[4], argv[5]);
    }
    // else if (command == "query-json" && argc == 3) {
    //     return executeJsonQuery(argv[2]);
    // }
    else if (command == "stats" && argc == 3) {
      return getIndexStats(argv[2]);
    } else if (command == "build-index" && argc == 3) {
      return buildIndex(argv[2]);
    } else if (command == "serialize" && (argc == 4 || argc == 5)) {
      std::string outputFile = (argc == 5) ? argv[4] : "";
      return serializeDatabase(argv[2], argv[3], outputFile);
    } else {
      printUsage(argv[0]);
      return 1;
    }
  } catch (const std::exception& e) {
    json errorResponse =
        createErrorResponse("Unexpected error: " + std::string(e.what()));
    std::cout << errorResponse.dump() << std::endl;
    return 1;
  }
}
