#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "QleverCliContext.h"
#include "cli-utils/CliArgs.h"
#include "cli-utils/IndexBuilderUtils.h"
#include "cli-utils/IndexStatsUtils.h"
#include "cli-utils/QueryTypeDetect.h"
#include "cli-utils/QueryUtils.h"
#include "cli-utils/RdfOutputUtils.h"
#include "cli-utils/StreamSuppressor.h"
#include "engine/ExecuteUpdate.h"
#include "engine/LocalVocab.h"
#include "engine/QueryPlanner.h"
#include "index/InputFileSpecification.h"
#include "index/vocabulary/VocabularyType.h"
#include "parser/RdfParser.h"
#include "parser/SparqlParser.h"
#include "parser/TokenizerCtre.h"
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

void printUsage(const char* programName, std::ostream& out = std::cerr) {
  out << "Usage: " << programName
      << " [--allocator-memory-gb <GB>] <command> [options]\n\n";
  out << "Global options:\n";
  out << "  --allocator-memory-gb <GB>  Set memory limit (default: 4 GB, "
         "env: QLEVER_MEMORY_LIMIT_GB)\n\n";
  out << "Commands:\n";
  out << "  query       <index_basename> <sparql_query> [output_format] "
         "[name]  Execute SPARQL query (optionally pin result)\n";
  out << "  query-to-file <index_basename> <sparql_query> <format> "
         "<output_file>  Execute CONSTRUCT query to file\n";
  out << "  update      <index_basename> <sparql_update_query>  Execute "
         "SPARQL UPDATE query\n";
  out << "  query-json  <json_input>                      Execute query "
         "from JSON input\n";
  out << "  write       <index_basename> <format> <input_file> [--graph <uri>] "
         " Execute write from stream (use '-' for stdin)\n";
  out << "  delete      <index_basename> <format> <input_file> [--graph <uri>] "
         " Execute delete from stream (use '-' for stdin)\n";
  out << "  stats       <index_basename>                  Get index "
         "statistics\n";
  out << "  binary-rebuild <index_basename> [output_basename]  Merge "
         "delta triples into main index\n";
  out << "  build-index <json_input>                      Build index "
         "from RDF files\n";
  out << "  serialize   <index_basename> <format> [output_file]  Dump "
         "database content\n";
  out << "                                                Formats: nt, nq\n";
  out << "                                                Add .gz to "
         "output_file for compression\n";
  out << "\nJSON input format for query-json:\n";
  out << "{\n";
  out << "  \"indexBasename\": \"path/to/index\",\n";
  out << "  \"query\": \"SELECT * WHERE { ?s ?p ?o } LIMIT 10\",\n";
  out << "  \"format\": \"sparql-json\"  // optional: sparql-json, csv, tsv\n";
  out << "}\n";
  out << "\nJSON input format for build-index:\n";
  out << "{\n";
  out << "  \"index_name\": \"my-index\",\n";
  out << "  \"index_directory\": \"/path/to/indices\",  // optional, "
         "defaults to current dir\n";
  out << "  \"input_files\": [\n";
  out << "    \"data.ttl\",\n";
  out << "    {\"path\": \"data.nt\", \"format\": \"nt\"},\n";
  out << "    {\"path\": \"data.nq\", \"format\": \"nq\", "
         "\"default_graph\": \"http://example.org/graph\"}\n";
  out << "  ],\n";
  out << "  \"memory_limit_gb\": 4,        // optional\n";
  out << "  \"settings_file\": \"settings.json\",  // optional\n";
  out << "  \"keep_temp_files\": false,    // optional\n";
  out << "  \"vocabulary_type\": \"on-disk-compressed-geo-split\",  // "
         "optional, for GeoSPARQL support\n";
  out << "  \"add_words_from_literals\": true  // optional, for text index\n";
  out << "  \"prefixes_for_id_encoded_iris\": [\"http://example.org/prefix/\"] "
         " // optional, for text index\n";
  out << "}\n";
  out << "\nVocabulary types:\n";
  out << "  in-memory-uncompressed     - Fast but uses more memory\n";
  out << "  on-disk-uncompressed       - Slower but uses less memory\n";
  out << "  in-memory-compressed       - Good balance (compressed, in "
         "memory)\n";
  out << "  on-disk-compressed         - Default (compressed, on disk)\n";
  out << "  on-disk-compressed-geo-split - Required for GeoSPARQL support\n";
}

// Flush stdout/stderr and call _exit() to bypass QLever's destructors.
// QleverCliContext's background threads (DeltaTriplesManager etc.) do not
// join cleanly, causing hangs or crashes (including under Rosetta/emulation).
// The OS reclaims all resources on process exit, so this is safe once all
// output has been written.
[[noreturn]] static void flushAndExit(int code) {
  std::cout.flush();
  std::cerr.flush();
  _exit(code);
}


int executeQuery(const std::string& indexBasename, const std::string& queryStr,
                 ad_utility::MemorySize memLimit,
                 const std::string& userFormat = "",
                 const std::string& name = "") {
  try {
    // Detect query type
    std::string type = cli_utils::detectQueryType(queryStr);
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

    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::QueryExecutor executor(qlever);
    std::string result;

    {
      cli_utils::SuppressStreams suppress;
      if (type == "CONSTRUCT" || type == "DESCRIBE") {
        result = executor.executeConstructQueryToString(queryStr, format);
      } else {
        result = executor.executeQuery(queryStr, format);
      }

      if (!name.empty()) {
        qlever->queryAndPinResultWithName(name, queryStr);
      }
    }

    std::cout << result << std::endl;
    flushAndExit(0);
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what());
    std::cerr << errorResponse.dump(2) << std::endl;
    flushAndExit(1);
  }
}

// Execute a SPARQL UPDATE query on the given index
int executeUpdate(const std::string& indexBasename,
                  const std::string& updateQuery,
                  ad_utility::MemorySize memLimit) {
  try {
    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);

    // Execute the update
    qlever->update(updateQuery);

    json response =
        createSuccessResponse("Update applied successfully.", updateQuery, 0);
    std::cout << response.dump() << std::endl;
    flushAndExit(0);
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what(), updateQuery);
    std::cerr << errorResponse.dump() << std::endl;
    flushAndExit(1);
  }
}

int executeJsonQuery(const std::string& jsonInput,
                     ad_utility::MemorySize memLimit) {
  try {
    json input = json::parse(jsonInput);

    if (!input.contains("indexBasename") || !input.contains("query")) {
      json response =
          createErrorResponse("Missing required fields: indexBasename, query");
      std::cerr << response.dump() << std::endl;
      return 1;
    }

    std::string indexBasename = input["indexBasename"].get<std::string>();
    std::string queryStr = input["query"].get<std::string>();
    std::string format = input.value("format", "sparql-json");

    return executeQuery(indexBasename, queryStr, memLimit, format);

  } catch (const json::parse_error& e) {
    json response =
        createErrorResponse("Invalid JSON input: " + std::string(e.what()));
    std::cerr << response.dump() << std::endl;
    return 1;
  }
}

int executeWriteOrDelete(const std::string& indexBasename,
                         const std::string& format,
                         const std::string& inputFile, bool isDelete,
                         ad_utility::MemorySize memLimit,
                         const std::string& defaultGraph = "") {
  try {
    qlever::Filetype filetype;
    if (format == "ttl" || format == "turtle" || format == "nt") {
      filetype = qlever::Filetype::Turtle;
    } else if (format == "nq") {
      filetype = qlever::Filetype::NQuad;
    } else {
      throw std::runtime_error("Unsupported format for " +
                               std::string(isDelete ? "delete" : "write") +
                               ": " + format + ". Use ttl, nt, or nq.");
    }

    std::string actualInputFile = (inputFile == "-") ? "/dev/stdin" : inputFile;

    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);

    TripleComponent defaultGraphTc =
        TripleComponent{ad_utility::triple_component::Iri::fromIriref(
            std::string(DEFAULT_GRAPH_IRI))};
    if (!defaultGraph.empty() && defaultGraph != "-") {
      defaultGraphTc =
          TripleComponent{ad_utility::triple_component::Iri::fromIriref(
              "<" + defaultGraph + ">")};
    }

    std::unique_ptr<RdfParserBase> parser;
    if (filetype == qlever::Filetype::NQuad) {
      parser = std::make_unique<RdfStreamParser<NQuadParser<TokenizerCtre>>>(
          actualInputFile, &qlever->encodedIriManager(),
          DEFAULT_PARSER_BUFFER_SIZE, std::move(defaultGraphTc));
    } else {
      parser = std::make_unique<RdfStreamParser<TurtleParser<TokenizerCtre>>>(
          actualInputFile, &qlever->encodedIriManager(),
          DEFAULT_PARSER_BUFFER_SIZE, std::move(defaultGraphTc));
    }

    LocalVocab localVocab;
    const auto& vocab = qlever->getVocab();
    const auto& encodedIriMgr = qlever->encodedIriManager();

    size_t totalProcessed = 0;
    while (auto batchOpt = parser->getBatch()) {
      auto& batch = batchOpt.value();
      if (batch.empty()) continue;

      std::vector<IdTriple<0>> idTriples;
      idTriples.reserve(batch.size());

      for (auto& turtleTriple : batch) {
        Id sId = std::move(turtleTriple.subject_)
                     .toValueId(vocab, localVocab, encodedIriMgr);
        Id pId = std::move(turtleTriple.predicate_)
                     .toValueId(vocab, localVocab, encodedIriMgr);
        Id oId = std::move(turtleTriple.object_)
                     .toValueId(vocab, localVocab, encodedIriMgr);
        Id gId = std::move(turtleTriple.graphIri_)
                     .toValueId(vocab, localVocab, encodedIriMgr);

        idTriples.push_back(IdTriple<0>{std::array{sId, pId, oId, gId}});
      }

      if (isDelete) {
        qlever->deleteTriples(std::move(idTriples), std::move(localVocab));
      } else {
        qlever->insertTriples(std::move(idTriples), std::move(localVocab));
      }

      localVocab = LocalVocab{};
      totalProcessed += batch.size();
    }

    json response = createSuccessResponse(
        (isDelete ? std::string("Deleted ") : std::string("Inserted ")) +
        std::to_string(totalProcessed) + " triples successfully.");
    std::cout << response.dump() << std::endl;
    flushAndExit(0);
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what());
    std::cerr << errorResponse.dump() << std::endl;
    flushAndExit(1);
  }
}

int executeBinaryRebuild(const std::string& indexBasename,
                         const std::string& outputBasename,
                         ad_utility::MemorySize memLimit) {
  try {
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);

    auto deltaCounts = qlever->getDeltaCounts();
    if (deltaCounts.triplesInserted_ == 0 &&
        deltaCounts.triplesDeleted_ == 0) {
      json response;
      response["success"] = true;
      response["skipped"] = true;
      response["message"] =
          "Binary rebuild not necessary: no delta triples to materialize.";
      response["indexBasename"] = indexBasename;
      response["timestamp"] =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count();
      std::cout << response.dump() << std::endl;
      flushAndExit(0);
    }

    qlever->binaryRebuild(outputBasename);

    json response;
    response["success"] = true;
    response["message"] = "Binary rebuild completed successfully.";
    response["indexBasename"] = indexBasename;
    response["timestamp"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    std::cout << response.dump() << std::endl;
    flushAndExit(0);
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what());
    errorResponse["command"] = "binary-rebuild";
    errorResponse["indexBasename"] = indexBasename;
    std::cerr << errorResponse.dump() << std::endl;
    flushAndExit(1);
  }
}

int serializeDatabase(const std::string& indexBasename,
                      const std::string& format,
                      ad_utility::MemorySize memLimit,
                      const std::string& outputFile = "") {
  try {
    if (format != "nt" && format != "nq") {
      throw std::runtime_error("Serialization only supports nt and nq formats");
    }

    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::DatabaseSerializer serializer(qlever);

    serializer.serialize(format, outputFile);

    if (!outputFile.empty()) {
      json response =
          createSuccessResponse("Database serialized to " + outputFile);
      std::cerr << response.dump(2) << std::endl;
    }

    flushAndExit(0);
  } catch (const std::exception& e) {
    json errorResponse = createErrorResponse(e.what());
    std::cerr << errorResponse.dump(2) << std::endl;
    flushAndExit(1);
  }
}

int buildIndex(const std::string& jsonInput) {
  try {
    json input = json::parse(jsonInput);
    json response = cli_utils::IndexBuilder::buildIndex(input);

    std::cout << response.dump() << std::endl;
    flushAndExit(response["success"].get<bool>() ? 0 : 1);

  } catch (const json::parse_error& e) {
    json response =
        createErrorResponse("Invalid JSON input: " + std::string(e.what()));
    std::cerr << response.dump() << std::endl;
    flushAndExit(1);
  } catch (const std::exception& e) {
    json response =
        createErrorResponse("Unexpected error: " + std::string(e.what()));
    std::cerr << response.dump() << std::endl;
    flushAndExit(1);
  }
}

int getIndexStats(const std::string& indexBasename,
                  ad_utility::MemorySize memLimit) {
  try {
    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

    auto qlever = std::make_shared<qlever::QleverCliContext>(config);
    cli_utils::IndexStatsCollector statsCollector(qlever);

    json response = statsCollector.collectStats(indexBasename);
    std::cout << response.dump(2) << std::endl;
    flushAndExit(0);

  } catch (const std::exception& e) {
    json response = createErrorResponse(e.what());
    response["indexBasename"] = indexBasename;
    std::cerr << response.dump(2) << std::endl;
    flushAndExit(1);
  }
}

int executeQueryToFile(const std::string& indexBasename,
                       const std::string& queryStr, const std::string& format,
                       const std::string& outputFile,
                       ad_utility::MemorySize memLimit) {
  try {
    // Load index
    qlever::EngineConfig config;
    config.baseName_ = indexBasename;
    config.memoryLimit_ = memLimit;

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
    flushAndExit(0);

  } catch (const std::exception& e) {
    json response =
        createErrorResponse("Query execution failed: " + std::string(e.what()));
    response["query"] = queryStr;
    std::cerr << response.dump() << std::endl;
    flushAndExit(1);
  }
}

int main(int argc, char* argv[]) {
  // Redirect QLever logging to stderr so only JSON goes to stdout
  ad_utility::setGlobalLoggingStream(&std::cerr);

  // Parse global flags (e.g. --allocator-memory-gb) before dispatching commands.
  cli_utils::ParsedCliArgs parsed;
  try {
    parsed = cli_utils::parseGlobalFlags(argc, argv);
  } catch (const std::runtime_error& e) {
    std::cerr << "Error: " << e.what() << "\n";
    printUsage(argv[0]);
    return 1;
  }

  const auto& args = parsed.remaining;
  // args[0] is the program name, args[1] is the command.
  int nargs = static_cast<int>(args.size());

  if (nargs < 2) {
    printUsage(argv[0]);
    return 1;
  }

  std::string command = args[1];

  // Handle help flags - print to stdout when explicitly requested
  if (command == "--help" || command == "-h" || command == "help") {
    printUsage(argv[0], std::cout);
    return 0;
  }

  try {
    // Resolve memory limits (stats uses a smaller default).
    auto memLimit = cli_utils::resolveMemoryLimit(parsed.maxMemoryGb);
    auto statsMemLimit = cli_utils::resolveMemoryLimit(parsed.maxMemoryGb, 1.0);

    if (command == "query" && (nargs == 4 || nargs == 5 || nargs == 6)) {
      // query <index_basename> <sparql_query> [format] [name]
      std::string format = (nargs >= 5) ? args[4] : "";
      std::string name = (nargs == 6) ? args[5] : "";
      return executeQuery(args[2], args[3], memLimit, format, name);
    } else if (command == "update" && nargs == 4) {
      // update <index_basename> <sparql_update_query>
      return executeUpdate(args[2], args[3], memLimit);
    } else if (command == "query-json" && nargs == 3) {
      // query-json <json_input>
      return executeJsonQuery(args[2], memLimit);
    } else if (command == "write" && nargs >= 5) {
      // write <index_basename> <format> <input_file> [default_graph]
      // write <index_basename> <format> <input_file> --graph <uri>
      std::string defaultGraph;
      if (nargs >= 7 && args[5] == "--graph") {
        defaultGraph = args[6];
      } else if (nargs == 6) {
        defaultGraph = args[5];
      }
      return executeWriteOrDelete(args[2], args[3], args[4], false, memLimit,
                                  defaultGraph);
    } else if (command == "delete" && nargs >= 5) {
      // delete <index_basename> <format> <input_file> [default_graph]
      // delete <index_basename> <format> <input_file> --graph <uri>
      std::string defaultGraph;
      if (nargs >= 7 && args[5] == "--graph") {
        defaultGraph = args[6];
      } else if (nargs == 6) {
        defaultGraph = args[5];
      }
      return executeWriteOrDelete(args[2], args[3], args[4], true, memLimit,
                                  defaultGraph);
    } else if (command == "query-to-file" && nargs == 6) {
      return executeQueryToFile(args[2], args[3], args[4], args[5], memLimit);
    } else if (command == "stats" && nargs == 3) {
      return getIndexStats(args[2], statsMemLimit);
    } else if (command == "build-index" && nargs == 3) {
      return buildIndex(args[2]);
    } else if (command == "binary-rebuild" && (nargs == 3 || nargs == 4)) {
      // binary-rebuild <index_basename> [output_basename]
      std::string outputBasename = (nargs == 4) ? args[3] : args[2];
      return executeBinaryRebuild(args[2], outputBasename, memLimit);
    } else if (command == "serialize" && (nargs == 4 || nargs == 5)) {
      std::string outputFile = (nargs == 5) ? args[4] : "";
      return serializeDatabase(args[2], args[3], memLimit, outputFile);
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
