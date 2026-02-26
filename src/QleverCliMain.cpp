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

// Helper functions for PREFIX handling workaround
#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>

// Parse PREFIX declarations and build a map of prefix -> IRI
std::map<std::string, std::string> parsePrefixes(const std::string& query) {
  std::map<std::string, std::string> prefixes;
  std::istringstream stream(query);
  std::string line;

  while (std::getline(stream, line)) {
    // Trim leading whitespace
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) continue;

    std::string trimmed = line.substr(start);

    // Check if line starts with PREFIX (case-insensitive)
    if (trimmed.length() >= 6) {
      std::string prefix_keyword = trimmed.substr(0, 6);
      std::transform(prefix_keyword.begin(), prefix_keyword.end(),
                     prefix_keyword.begin(), ::toupper);

      if (prefix_keyword == "PREFIX") {
        // Extract prefix name and IRI
        // Format: PREFIX name: <iri>
        size_t colonPos = trimmed.find(':', 6);
        size_t iriStart = trimmed.find('<', colonPos);
        size_t iriEnd = trimmed.find('>', iriStart);

        if (colonPos != std::string::npos && iriStart != std::string::npos &&
            iriEnd != std::string::npos) {
          // Extract prefix name (between PREFIX and :)
          std::string prefixName = trimmed.substr(6, colonPos - 6);
          // Trim whitespace
          prefixName.erase(0, prefixName.find_first_not_of(" \t"));
          prefixName.erase(prefixName.find_last_not_of(" \t") + 1);

          // Extract IRI (between < and >)
          std::string iri = trimmed.substr(iriStart + 1, iriEnd - iriStart - 1);

          prefixes[prefixName] = iri;
        }
      }
    }
  }

  return prefixes;
}

// Expand prefixed terms in a query using the prefix map
std::string expandPrefixedTerms(
    const std::string& query,
    const std::map<std::string, std::string>& prefixes) {
  std::string result = query;

  // For each prefix, replace all occurrences of prefix:localPart with
  // <iri/localPart>
  for (const auto& [prefix, iri] : prefixes) {
    // Match prefix:word (where word is alphanumeric or underscore)
    // Use negative lookbehind to avoid matching inside IRIs
    std::string pattern = prefix + ":([a-zA-Z0-9_-]+)";
    std::regex prefixRegex(pattern);

    std::string replacement = "<" + iri + "$1>";
    result = std::regex_replace(result, prefixRegex, replacement);
  }

  return result;
}

// Strip PREFIX declarations from a query
std::string stripPrefixDeclarations(const std::string& query) {
  std::istringstream stream(query);
  std::ostringstream result;
  std::string line;

  while (std::getline(stream, line)) {
    // Trim and check if line starts with PREFIX
    size_t start = line.find_first_not_of(" \t");
    if (start != std::string::npos) {
      std::string trimmed = line.substr(start);
      if (trimmed.length() >= 6) {
        std::string prefix_keyword = trimmed.substr(0, 6);
        std::transform(prefix_keyword.begin(), prefix_keyword.end(),
                       prefix_keyword.begin(), ::toupper);

        if (prefix_keyword == "PREFIX") {
          // Skip this line
          continue;
        }
      }
    }

    result << line << "\n";
  }

  return result.str();
}

// Complete PREFIX workaround: parse, expand, and strip
std::string stripPrefixesAndExpand(const std::string& query) {
  // 1. Parse PREFIX declarations
  auto prefixes = parsePrefixes(query);

  // 2. Expand prefixed terms
  std::string expanded = expandPrefixedTerms(query, prefixes);

  // 3. Strip PREFIX declarations
  std::string cleaned = stripPrefixDeclarations(expanded);

  return cleaned;
}

// Helper to detect query type, skipping PREFIX declarations
std::string detectQueryType(const std::string& query) {
  std::istringstream stream(query);
  std::string word;

  while (stream >> word) {
    // Convert to uppercase for comparison
    std::string upperWord = word;
    std::transform(upperWord.begin(), upperWord.end(), upperWord.begin(),
                   ::toupper);

    // Skip PREFIX declarations
    if (upperWord == "PREFIX") {
      // Skip the rest of this PREFIX line (prefix name and IRI)
      std::string restOfLine;
      std::getline(stream, restOfLine);
      continue;
    }

    // Return first non-PREFIX keyword (SELECT, CONSTRUCT, DESCRIBE, etc.)
    return upperWord;
  }

  return "";
}

int executeQuery(const std::string& indexBasename, const std::string& queryStr,
                 const std::string& userFormat = "",
                 const std::string& name = "") {
  try {
    // Detect query type
    std::string type = detectQueryType(queryStr);
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
      // Workaround for QLever PREFIX bug: strip PREFIX declarations and expand
      // prefixed terms before execution

      // Debug: log before processing (to stdout, since stderr is redirected)
      std::cout << "DEBUG: About to process query" << std::endl;

      std::string processedQuery = stripPrefixesAndExpand(queryStr);

      // Debug: log the processed query to stdout
      std::cout << "DEBUG: Original query:\n" << queryStr << std::endl;
      std::cout << "DEBUG: Processed query:\n" << processedQuery << std::endl;

      // For CONSTRUCT and DESCRIBE, use the construct executor
      // (no file output, just return as string)
      result = executor.executeConstructQueryToString(processedQuery, format);
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

    // Workaround for QLever PREFIX bug: strip PREFIX declarations and expand
    // prefixed terms before execution
    std::string processedQuery = stripPrefixesAndExpand(queryStr);

    // Execute query to file
    ad_utility::Timer timer{ad_utility::Timer::Started};
    executor.executeConstructQuery(processedQuery, format, outputFile);
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
  // DEBUG: Very early output to confirm execution
  std::cout << "DEBUG: main() called with " << argc << " arguments"
            << std::endl;
  std::cout << "DEBUG: command line: ";
  for (int i = 0; i < argc; ++i) {
    std::cout << argv[i] << " ";
  }
  std::cout << std::endl;

  // Redirect QLever logging to stderr so only JSON goes to stdout
  ad_utility::setGlobalLoggingStream(&std::cerr);

  if (argc < 2) {
    printUsage(argv[0]);
    return 1;
  }

  std::string command = argv[1];
  std::cout << "DEBUG: command = " << command << std::endl;

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
