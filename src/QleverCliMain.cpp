// Copyright 2025, QLever JavaScript Integration
// Author: GitHub Copilot Assistant

#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "libqlever/Qlever.h"
#include "util/http/MediaTypes.h"
#include "index/InputFileSpecification.h"
#include "util/Log.h"

using json = nlohmann::json;

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <command> [options]\n\n";
    std::cerr << "Commands:\n";
    std::cerr << "  query       <index_basename> <sparql_query>   Execute SPARQL query\n";
    std::cerr << "  update      <index_basename> <sparql_update>  Execute SPARQL update\n";
    std::cerr << "  query-json  <json_input>                      Execute query from JSON input\n";
    std::cerr << "  stats       <index_basename>                  Get index statistics\n";
    std::cerr << "  build-index <json_input>                      Build index from RDF files\n";
    std::cerr << "\nJSON input format for query-json:\n";
    std::cerr << "{\n";
    std::cerr << "  \"indexBasename\": \"path/to/index\",\n";
    std::cerr << "  \"query\": \"SELECT * WHERE { ?s ?p ?o } LIMIT 10\",\n";
    std::cerr << "  \"format\": \"sparql-json\"  // optional: sparql-json, csv, tsv\n";
    std::cerr << "}\n\n";
    std::cerr << "JSON input format for build-index:\n";
    std::cerr << "{\n";
    std::cerr << "  \"index_name\": \"my-index\",\n";
    std::cerr << "  \"index_directory\": \"/path/to/indices\",  // optional, defaults to current dir\n";
    std::cerr << "  \"input_files\": [\n";
    std::cerr << "    \"data.ttl\",\n";
    std::cerr << "    {\"path\": \"data.nt\", \"format\": \"nt\"},\n";
    std::cerr << "    {\"path\": \"data.nq\", \"format\": \"nq\", \"default_graph\": \"http://example.org/graph\"}\n";
    std::cerr << "  ],\n";
    std::cerr << "  \"memory_limit_gb\": 4,        // optional\n";
    std::cerr << "  \"settings_file\": \"settings.json\",  // optional\n";
    std::cerr << "  \"keep_temp_files\": false,    // optional\n";
    std::cerr << "  \"add_words_from_literals\": true  // optional, for text index\n";
    std::cerr << "}\n";
}

json createErrorResponse(const std::string& error, const std::string& query = "") {
    json response;
    response["success"] = false;
    response["error"] = error;
    if (!query.empty()) {
        response["query"] = query;
    }
    response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return response;
}

json createSuccessResponse(const std::string& result, const std::string& query, 
                          long executionTimeMs, const std::string& format = "sparql-json") {
    json response;
    response["success"] = true;
    response["result"] = result;
    response["query"] = query;
    response["executionTimeMs"] = executionTimeMs;
    response["format"] = format;
    response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return response;
}

ad_utility::MediaType getMediaType(const std::string& format) {
    if (format == "csv") return ad_utility::MediaType::csv;
    if (format == "tsv") return ad_utility::MediaType::tsv;
    if (format == "sparql-xml") return ad_utility::MediaType::sparqlXml;
    if (format == "qlever-json") return ad_utility::MediaType::qleverJson;
    return ad_utility::MediaType::sparqlJson; // default
}

int executeQuery(const std::string& indexBasename, const std::string& queryStr, 
                const std::string& format = "sparql-json") {
    try {
        // Load index
        qlever::EngineConfig config;
        config.baseName_ = indexBasename;
        config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4); // Reasonable default
        
        qlever::Qlever qlever{config};
        
        // Execute query with timing
        ad_utility::Timer timer{ad_utility::Timer::Started};
        auto mediaType = getMediaType(format);
        std::string result = qlever.query(queryStr, mediaType);
        auto executionTime = timer.msecs().count();
        
        // Output JSON response
        json response = createSuccessResponse(result, queryStr, executionTime, format);
        std::cout << response.dump() << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        json response = createErrorResponse(e.what(), queryStr);
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int executeUpdate(const std::string& indexBasename, const std::string& updateStr) {
    try {
        // Load index
        qlever::EngineConfig config;
        config.baseName_ = indexBasename;
        config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);
        
        qlever::Qlever qlever{config};
        
        // Execute update with timing
        ad_utility::Timer timer{ad_utility::Timer::Started};
        std::string result = qlever.query(updateStr); // Updates go through query interface
        auto executionTime = timer.msecs().count();
        
        // Output JSON response
        json response = createSuccessResponse("Update executed successfully", updateStr, executionTime);
        std::cout << response.dump() << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        json response = createErrorResponse(e.what(), updateStr);
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int executeJsonQuery(const std::string& jsonInput) {
    try {
        json input = json::parse(jsonInput);
        
        // Validate required fields
        if (!input.contains("indexBasename") || !input.contains("query")) {
            json response = createErrorResponse("Missing required fields: indexBasename, query");
            std::cout << response.dump() << std::endl;
            return 1;
        }
        
        std::string indexBasename = input["indexBasename"];
        std::string queryStr = input["query"];
        std::string format = input.value("format", "sparql-json");
        
        return executeQuery(indexBasename, queryStr, format);
        
    } catch (const json::parse_error& e) {
        json response = createErrorResponse("Invalid JSON input: " + std::string(e.what()));
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int getIndexStats(const std::string& indexBasename) {
    try {
        // Load index
        qlever::EngineConfig config;
        config.baseName_ = indexBasename;
        config.memoryLimit_ = ad_utility::MemorySize::gigabytes(1); // Minimal for stats
        
        qlever::Qlever qlever{config};
        
        // Get basic stats by querying
        std::string statsQuery = "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o }";
        ad_utility::Timer timer{ad_utility::Timer::Started};
        std::string result = qlever.query(statsQuery);
        auto executionTime = timer.msecs().count();
        
        // Create stats response
        json response;
        response["success"] = true;
        response["indexBasename"] = indexBasename;
        response["tripleCountQuery"] = result;
        response["executionTimeMs"] = executionTime;
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::cout << response.dump() << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        json response = createErrorResponse(e.what());
        response["indexBasename"] = indexBasename;
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int buildIndex(const std::string& jsonInput) {
    try {
        json input = json::parse(jsonInput);
        
        // Validate required parameters
        if (!input.contains("input_files") || !input["input_files"].is_array() || 
            input["input_files"].empty()) {
            json response = createErrorResponse("Missing or invalid 'input_files' parameter (must be non-empty array)");
            std::cout << response.dump() << std::endl;
            return 1;
        }
        if (!input.contains("index_name") || !input["index_name"].is_string()) {
            json response = createErrorResponse("Missing or invalid 'index_name' parameter");
            std::cout << response.dump() << std::endl;
            return 1;
        }

        std::string indexName = input["index_name"];
        
        // Optional index directory (defaults to current directory)
        std::string indexDirectory = input.value("index_directory", ".");
        
        // Ensure directory exists
        if (!std::filesystem::exists(indexDirectory)) {
            try {
                std::filesystem::create_directories(indexDirectory);
            } catch (const std::filesystem::filesystem_error& e) {
                json response = createErrorResponse("Failed to create index directory: " + std::string(e.what()));
                std::cout << response.dump() << std::endl;
                return 1;
            }
        }
        
        // Create full path for index files
        std::filesystem::path indexPath = std::filesystem::path(indexDirectory) / indexName;
        std::string fullIndexPath = indexPath.string();
        
        // Create IndexBuilderConfig
        qlever::IndexBuilderConfig config;
        config.baseName_ = fullIndexPath;  // Use full path as basename
        config.kbIndexName_ = indexName;   // Keep original name for metadata

        // Process input files
        for (const auto& inputFile : input["input_files"]) {
            std::string filepath;
            qlever::Filetype filetype = qlever::Filetype::Turtle; // default
            std::string defaultGraph = "";

            if (inputFile.is_string()) {
                filepath = inputFile.get<std::string>();
            } else if (inputFile.is_object()) {
                if (!inputFile.contains("path") || !inputFile["path"].is_string()) {
                    json response = createErrorResponse("Input file object must contain 'path' string");
                    std::cout << response.dump() << std::endl;
                    return 1;
                }
                filepath = inputFile["path"];

                // Optional format specification
                if (inputFile.contains("format") && inputFile["format"].is_string()) {
                    std::string format = inputFile["format"];
                    if (format == "ttl" || format == "turtle") {
                        filetype = qlever::Filetype::Turtle;
                    } else if (format == "nt") {
                        filetype = qlever::Filetype::Turtle; // NT is parsed as Turtle
                    } else if (format == "nq") {
                        filetype = qlever::Filetype::NQuad;
                    } else {
                        json response = createErrorResponse("Unsupported format: " + format + ". Use 'ttl', 'nt', or 'nq'");
                        std::cout << response.dump() << std::endl;
                        return 1;
                    }
                }

                // Optional default graph
                if (inputFile.contains("default_graph") && inputFile["default_graph"].is_string()) {
                    defaultGraph = inputFile["default_graph"];
                }
            } else {
                json response = createErrorResponse("Input file must be string path or object with 'path' field");
                std::cout << response.dump() << std::endl;
                return 1;
            }

            // Check if file exists
            if (!std::filesystem::exists(filepath)) {
                json response = createErrorResponse("Input file does not exist: " + filepath);
                std::cout << response.dump() << std::endl;
                return 1;
            }

            // Add to config
            if (defaultGraph.empty()) {
                config.inputFiles_.emplace_back(filepath, filetype);
            } else {
                config.inputFiles_.emplace_back(filepath, filetype, defaultGraph);
            }
        }

        // Optional parameters
        if (input.contains("memory_limit_gb") && input["memory_limit_gb"].is_number()) {
            double memoryLimitGb = input["memory_limit_gb"];
            config.memoryLimit_ = ad_utility::MemorySize::gigabytes(static_cast<size_t>(memoryLimitGb));
        }

        if (input.contains("settings_file") && input["settings_file"].is_string()) {
            config.settingsFile_ = input["settings_file"];
        }

        if (input.contains("keep_temp_files") && input["keep_temp_files"].is_boolean()) {
            config.keepTemporaryFiles_ = input["keep_temp_files"];
        }

        if (input.contains("only_pso_and_pos") && input["only_pso_and_pos"].is_boolean()) {
            config.onlyPsoAndPos_ = input["only_pso_and_pos"];
        }

        // Text index options
        if (input.contains("add_words_from_literals") && input["add_words_from_literals"].is_boolean()) {
            config.addWordsFromLiterals_ = input["add_words_from_literals"];
        }

        if (input.contains("words_file") && input["words_file"].is_string()) {
            config.wordsfile_ = input["words_file"];
        }

        if (input.contains("docs_file") && input["docs_file"].is_string()) {
            config.docsfile_ = input["docs_file"];
        }

        if (input.contains("text_index_name") && input["text_index_name"].is_string()) {
            config.textIndexName_ = input["text_index_name"];
        }

        // Validate and build index
        config.validate();
        
        auto startTime = std::chrono::steady_clock::now();
        qlever::Qlever::buildIndex(config);
        auto endTime = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

        json response;
        response["success"] = true;
        response["indexName"] = indexName;
        response["indexDirectory"] = indexDirectory;
        response["fullIndexPath"] = fullIndexPath;
        response["numInputFiles"] = config.inputFiles_.size();
        response["buildTimeMs"] = duration.count();
        response["message"] = "Index built successfully";
        response["indexFiles"] = {
            fullIndexPath + ".index.pso",
            fullIndexPath + ".index.pos", 
            fullIndexPath + ".vocabulary.external",
            fullIndexPath + ".meta-data.json"
        };

        std::cout << response.dump() << std::endl;
        return 0;

    } catch (const json::parse_error& e) {
        json response = createErrorResponse("Invalid JSON input: " + std::string(e.what()));
        std::cout << response.dump() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        json response = createErrorResponse("Index building failed: " + std::string(e.what()));
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int main(int argc, char** argv) {
    // Redirect QLever logging to stderr so only JSON goes to stdout
    ad_utility::setGlobalLoggingStream(&std::cerr);
    
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string command = argv[1];
    
    try {
        if (command == "query" && argc == 4) {
            return executeQuery(argv[2], argv[3]);
        }
        else if (command == "update" && argc == 4) {
            return executeUpdate(argv[2], argv[3]);
        }
        else if (command == "query-json" && argc == 3) {
            return executeJsonQuery(argv[2]);
        }
        else if (command == "stats" && argc == 3) {
            return getIndexStats(argv[2]);
        }
        else if (command == "build-index" && argc == 3) {
            return buildIndex(argv[2]);
        }
        else {
            printUsage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        json response = createErrorResponse("Unexpected error: " + std::string(e.what()));
        std::cout << response.dump() << std::endl;
        return 1;
    }
}