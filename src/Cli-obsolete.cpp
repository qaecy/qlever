// Copyright 2025, QLever JavaScript Integration
// Author: GitHub Copilot Assistant

#include <iostream>
#include <string>
#include <memory>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>
#include <nlohmann/json.hpp>

#include "libqlever/Qlever.h"
#include "util/http/MediaTypes.h"
#include "index/InputFileSpecification.h"
#include "util/Log.h"
#include "cli-utils/QueryUtils.h"
#include "cli-utils/RdfOutputUtils.h"

using json = nlohmann::json;

// Gzip output stream wrapper
class GzipOutputStream {
private:
    gzFile file_;
    
public:
    explicit GzipOutputStream(const std::string& filename) {
        file_ = gzopen(filename.c_str(), "wb");
        if (!file_) {
            throw std::runtime_error("Failed to open gzip file: " + filename);
        }
    }
    
    ~GzipOutputStream() {
        if (file_) {
            gzclose(file_);
        }
    }
    
    void write(const std::string& data) {
        if (gzwrite(file_, data.c_str(), data.length()) != static_cast<int>(data.length())) {
            throw std::runtime_error("Failed to write to gzip file");
        }
    }
    
// Gzip output stream wrapper
class GzipOutputStream {
private:
    gzFile file_;
    
public:
    explicit GzipOutputStream(const std::string& filename) {
        file_ = gzopen(filename.c_str(), "wb");
        if (!file_) {
            throw std::runtime_error("Failed to open gzip file: " + filename);
        }
    }
    
    ~GzipOutputStream() {
        if (file_) {
            gzclose(file_);
        }
    }
    
    void write(const std::string& data) {
        if (gzwrite(file_, data.c_str(), data.length()) != static_cast<int>(data.length())) {
            throw std::runtime_error("Failed to write to gzip file");
        }
    }
    
// Function declarations
void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " <command> [options]\n\n";
    std::cerr << "Commands:\n";
    std::cerr << "  query       <index_basename> <sparql_query>   Execute SPARQL query\n";
    std::cerr << "  query-to-file <index_basename> <sparql_query> <format> <output_file>  Execute CONSTRUCT query to file\n";
    std::cerr << "  update      <index_basename> <sparql_update>  Execute SPARQL update\n";
    std::cerr << "  query-json  <json_input>                      Execute query from JSON input\n";
    std::cerr << "  stats       <index_basename>                  Get index statistics\n";
    std::cerr << "  build-index <json_input>                      Build index from RDF files\n";
    std::cerr << "  serialize   <index_basename> <format> [output_file]  Dump database content\n";
    std::cerr << "                                                Formats: nt, ttl, nq\n";
    std::cerr << "                                                Add .gz to output_file for compression\n";
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
        
        auto qlever = std::make_shared<qlever::Qlever>(config);
        cli_utils::QueryExecutor executor(qlever);
        
        if (cli_utils::QueryExecutor::isConstructQuery(queryStr)) {
            // For CONSTRUCT queries, output RDF directly to stdout
            std::string rdfFormat = (format == "sparql-json" || format == "json") ? "nt" : format;
            if (!cli_utils::RdfFormatUtils::isValidFormat(rdfFormat)) {
                rdfFormat = "nt"; // Fallback
            }
            
            ad_utility::Timer timer{ad_utility::Timer::Started};
            executor.executeConstructQuery(queryStr, rdfFormat);
            auto executionTime = timer.msecs().count();
            
            std::cerr << "CONSTRUCT query executed successfully in " << executionTime << "ms" << std::endl;
        } else {
            // Regular SELECT/ASK queries
            ad_utility::Timer timer{ad_utility::Timer::Started};
            std::string result = executor.executeQuery(queryStr, format);
            auto executionTime = timer.msecs().count();
            
            json response = createSuccessResponse(result, queryStr, executionTime, format);
            std::cout << response.dump() << std::endl;
        }
        
        return 0;
    } catch (const std::exception& e) {
        json response = createErrorResponse(e.what(), queryStr);
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int executeQueryToFile(const std::string& indexBasename, const std::string& queryStr, 
                      const std::string& format, const std::string& outputFile) {
    try {
        // Validate RDF format
        if (!cli_utils::RdfFormatUtils::isValidFormat(format)) {
            json response = createErrorResponse("Invalid format for query-to-file. Supported formats: nt, ttl, nq");
            std::cout << response.dump() << std::endl;
            return 1;
        }
        
        // Load index
        qlever::EngineConfig config;
        config.baseName_ = indexBasename;
        config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);
        
        auto qlever = std::make_shared<qlever::Qlever>(config);
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
        response["compressed"] = cli_utils::RdfFormatUtils::isGzipFile(outputFile);
        response["executionTimeMs"] = executionTime;
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        std::cout << response.dump() << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        json response = createErrorResponse("Query execution failed: " + std::string(e.what()));
        response["query"] = queryStr;
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

int executeUpdate(const std::string& indexBasename, const std::string& updateStr) {
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

int serializeDatabase(const std::string& indexBasename, const std::string& format, 
                     const std::string& outputFile = "") {
    try {
        // Validate format
        if (format != "nt" && format != "ttl" && format != "nq") {
            json response = createErrorResponse("Invalid format. Supported formats: nt, ttl, nq");
            std::cout << response.dump() << std::endl;
            return 1;
        }
        
        // Load index
        qlever::EngineConfig config;
        config.baseName_ = indexBasename;
        config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4); // Need memory for large dumps
        
        qlever::Qlever qlever{config};
        
        // Determine if we should use gzip compression
        bool useGzip = false;
        std::string actualOutputFile = outputFile;
        if (!outputFile.empty() && outputFile.size() > 3 && 
            outputFile.substr(outputFile.size() - 3) == ".gz") {
            useGzip = true;
        }
        
        // Setup output streams
        std::unique_ptr<std::ostream> stdOutputStream;
        std::unique_ptr<GzipOutputStream> gzipOutputStream;
        std::ofstream fileStream;
        
        if (!outputFile.empty()) {
            if (useGzip) {
                gzipOutputStream = std::make_unique<GzipOutputStream>(outputFile);
            } else {
                fileStream.open(outputFile);
                if (!fileStream) {
                    json response = createErrorResponse("Cannot write to output file: " + outputFile);
                    std::cout << response.dump() << std::endl;
                    return 1;
                }
                stdOutputStream = std::make_unique<std::ostream>(fileStream.rdbuf());
            }
        } else {
            stdOutputStream = std::make_unique<std::ostream>(std::cout.rdbuf());
        }
        
        // Helper lambda to write to either stream type
        auto writeOutput = [&](const std::string& data) {
            if (useGzip && gzipOutputStream) {
                gzipOutputStream->write(data);
            } else if (stdOutputStream) {
                *stdOutputStream << data;
            }
        };
        
        // Helper lambda to flush output
        auto flushOutput = [&]() {
            if (useGzip && gzipOutputStream) {
                gzipOutputStream->flush();
            } else if (stdOutputStream) {
                stdOutputStream->flush();
            }
        };
        
        // Write prefix declarations for Turtle format first
        if (format == "ttl") {
            std::string prefixes = 
                "@prefix rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#> .\n"
                "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n"
                "@prefix qcy: <https://dev.qaecy.com/ont#> .\n"
                "@prefix qcy-e: <https://dev.qaecy.com/enum#> .\n"
                "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n\n";
            writeOutput(prefixes);
        }
        
        // Streaming parameters - increased batch size for efficiency
        const size_t batchSize = 50000; // Process 50K triples at a time (increased from 10K)
        size_t offset = 0;
        size_t totalTriples = 0;
        ad_utility::Timer totalTimer{ad_utility::Timer::Started};
        auto startTime = std::chrono::steady_clock::now();
        
        // Progress logging setup
        auto lastProgressTime = std::chrono::steady_clock::now();
        const auto progressInterval = std::chrono::seconds(5); // Log progress every 5 seconds
        
        // Pre-allocate string buffer for better performance
        std::string batchBuffer;
        batchBuffer.reserve(batchSize * 200); // Estimate ~200 chars per triple
        
        std::cerr << "Starting serialization of " << indexBasename << " to " << format;
        if (!outputFile.empty()) {
            std::cerr << " format, output: " << outputFile;
            if (useGzip) std::cerr << " (gzipped)";
        }
        std::cerr << std::endl;
        
        while (true) {
            // Construct batch query with LIMIT and OFFSET
            std::string sparqlQuery;
            if (format == "nq") {
                sparqlQuery = "SELECT ?s ?p ?o ?g WHERE { GRAPH ?g { ?s ?p ?o } } LIMIT " + 
                             std::to_string(batchSize) + " OFFSET " + std::to_string(offset);
            } else {
                sparqlQuery = "SELECT ?s ?p ?o WHERE { ?s ?p ?o } LIMIT " + 
                             std::to_string(batchSize) + " OFFSET " + std::to_string(offset);
            }
            
            // Execute batch query (suppress QLever's verbose logging during execution)
            std::ofstream nullStream("/dev/null");
            auto* originalStream = &std::cerr; // Save the original stream
            ad_utility::setGlobalLoggingStream(&nullStream); // Redirect to /dev/null
            
            std::string result = qlever.query(sparqlQuery, ad_utility::MediaType::sparqlJson);
            
            // Restore original logging stream
            ad_utility::setGlobalLoggingStream(originalStream);
            
            // Parse the JSON result
            json queryResult = json::parse(result);
            
            if (!queryResult.contains("results") || !queryResult["results"].contains("bindings")) {
                break; // No more results
            }
            
            auto bindings = queryResult["results"]["bindings"];
            if (bindings.empty()) {
                break; // No more results
            }
            
            // Process and write this batch more efficiently
            batchBuffer.clear(); // Reuse the pre-allocated buffer
            
            for (const auto& binding : bindings) {
                std::string subject = extractValue(binding["s"]);
                std::string predicate = extractValue(binding["p"]);
                std::string object = extractValue(binding["o"]);
                
                // More efficient string concatenation using reserve and append
                if (format == "nt" || format == "ttl") {
                    batchBuffer.append(subject).append(" ").append(predicate).append(" ").append(object).append(" .\n");
                } else if (format == "nq") {
                    std::string graph = binding.contains("g") ? extractValue(binding["g"]) : "<>";
                    batchBuffer.append(subject).append(" ").append(predicate).append(" ").append(object).append(" ").append(graph).append(" .\n");
                }
                totalTriples++;
            }
            
            // Write batch to output
            writeOutput(batchBuffer);
            flushOutput();
            
            // Time-based progress logging (every 5 seconds) with better metrics
            auto currentTime = std::chrono::steady_clock::now();
            if (currentTime - lastProgressTime >= progressInterval) {
                auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                    currentTime - startTime).count();
                double triplesPerSecond = elapsedSeconds > 0 ? totalTriples / static_cast<double>(elapsedSeconds) : 0;
                
                // Estimate completion time
                std::string eta = "";
                if (triplesPerSecond > 0) {
                    // This is a rough estimate since we don't know total count ahead of time
                    double minutesElapsed = elapsedSeconds / 60.0;
                    eta = " (" + std::to_string(static_cast<int>(minutesElapsed)) + "min elapsed)";
                }
                
                std::cerr << "Progress: " << totalTriples << " triples serialized "
                          << "(" << std::fixed << std::setprecision(0) << triplesPerSecond << " triples/sec)" 
                          << eta << std::endl;
                lastProgressTime = currentTime;
            }
            
            // If we got fewer results than batch size, we're done
            if (bindings.size() < batchSize) {
                break;
            }
            
            offset += batchSize;
        }
        
        auto totalExecutionTime = totalTimer.msecs().count();
        
        // Close files if needed
        if (fileStream.is_open()) {
            fileStream.close();
        }
        if (gzipOutputStream) {
            gzipOutputStream.reset(); // This will call destructor and close gzip file
        }
        
        // Create response (either to stderr if file output, or as JSON if stdout)
        if (!outputFile.empty()) {
            json response;
            response["success"] = true;
            response["message"] = "Database serialized successfully";
            response["indexBasename"] = indexBasename;
            response["format"] = format;
            response["outputFile"] = outputFile;
            response["compressed"] = useGzip;
            response["executionTimeMs"] = totalExecutionTime;
            response["tripleCount"] = totalTriples;
            response["batchSize"] = batchSize;
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            std::cerr << response.dump() << std::endl; // Output JSON to stderr
        } else {
            // When outputting to stdout, we can't send JSON response as it would mix with RDF data
            // Log final count to stderr
            std::cerr << "Serialization complete. Total triples: " << totalTriples 
                      << ", Time: " << totalExecutionTime << "ms" << std::endl;
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        json response = createErrorResponse("Serialization failed: " + std::string(e.what()));
        response["indexBasename"] = indexBasename;
        response["format"] = format;
        std::cout << response.dump() << std::endl;
        return 1;
    }
}

// Helper function to extract value from SPARQL JSON binding (optimized)
std::string extractValue(const json& binding) {
    const auto& type = binding["type"];
    const auto& value = binding["value"];
    
    if (type == "uri") {
        std::string result;
        result.reserve(value.get<std::string>().length() + 2);
        result = "<" + value.get<std::string>() + ">";
        return result;
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
    
    // Handle help flags
    if (command == "--help" || command == "-h" || command == "help") {
        printUsage(argv[0]);
        return 0;
    }
    
    try {
        if (command == "query" && argc == 4) {
            return executeQuery(argv[2], argv[3]);
        }
        else if (command == "query-to-file" && argc == 6) {
            return executeQueryToFile(argv[2], argv[3], argv[4], argv[5]);
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
        else if (command == "serialize" && (argc == 4 || argc == 5)) {
            std::string outputFile = (argc == 5) ? argv[4] : "";
            return serializeDatabase(argv[2], argv[3], outputFile);
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