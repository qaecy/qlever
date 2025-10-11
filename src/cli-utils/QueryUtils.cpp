#include "QueryUtils.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iostream>
#include "util/Log.h"
#include "util/json.h"
#include "util/http/MediaTypes.h"

namespace cli_utils {

// =============================================================================
// QueryExecutor Implementation
// =============================================================================

QueryExecutor::QueryExecutor(std::shared_ptr<qlever::Qlever> qlever) : qlever_(qlever) {}

std::string QueryExecutor::executeQuery(const std::string& query, const std::string& format) {
    if (isConstructQuery(query)) {
        throw std::invalid_argument("Use executeConstructQuery for CONSTRUCT queries");
    }
    
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
        mediaType = ad_utility::MediaType::sparqlJson; // default
    }
    
    // Redirect QLever's verbose logging to /dev/null during query execution
    std::streambuf* clogBuf = std::clog.rdbuf();
    std::ofstream devNull("/dev/null");
    std::clog.rdbuf(devNull.rdbuf());
    
    try {
        std::string result = qlever_->query(query, mediaType);
        std::clog.rdbuf(clogBuf); // Restore logging
        return result;
    } catch (...) {
        std::clog.rdbuf(clogBuf); // Restore logging even on exception
        throw;
    }
}

void QueryExecutor::executeConstructQuery(const std::string& query, const std::string& outputFormat, 
                                        const std::string& outputFile) {
    if (!isConstructQuery(query)) {
        throw std::invalid_argument("Query is not a CONSTRUCT query");
    }
    
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
        
        std::clog.rdbuf(clogBuf); // Restore logging
    } catch (const std::exception& e) {
        throw std::runtime_error("Query execution failed: " + std::string(e.what()));
    }
    
    // Process results line by line
    std::istringstream resultStream(rawResults);
    std::string line;
    size_t tripleCount = 0;
    
    while (std::getline(resultStream, line)) {
        if (!line.empty() && line.back() == '.') {
            // Write the line directly as it's already in the correct format from QLever
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
        std::cerr << " (" << static_cast<int>(progress.getItemsPerSecond(tripleCount)) << "/sec)";
    }
    std::cerr << std::endl;
}

bool QueryExecutor::isConstructQuery(const std::string& query) {
    std::string upperQuery = query;
    std::transform(upperQuery.begin(), upperQuery.end(), upperQuery.begin(), ::toupper);
    
    // Remove leading whitespace and comments
    size_t start = 0;
    while (start < upperQuery.length() && (std::isspace(upperQuery[start]) || upperQuery[start] == '#')) {
        if (upperQuery[start] == '#') {
            // Skip to end of line
            while (start < upperQuery.length() && upperQuery[start] != '\n') {
                start++;
            }
        }
        start++;
    }
    
    return start < upperQuery.length() && upperQuery.substr(start, 9) == "CONSTRUCT";
}

std::string QueryExecutor::extractValue(const std::string& json, const std::string& key) {
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

// =============================================================================
// DatabaseSerializer Implementation
// =============================================================================

DatabaseSerializer::DatabaseSerializer(std::shared_ptr<qlever::Qlever> qlever) : qlever_(qlever) {}

void DatabaseSerializer::serialize(const std::string& format, const std::string& outputFile) {
    RdfOutputWriter writer(format, outputFile);
    writer.writePrefixes();
    
    std::cerr << "Starting serialization to " << format;
    if (!outputFile.empty()) {
        std::cerr << " format, output: " << outputFile;
        if (writer.isUsingGzip()) std::cerr << " (gzipped)";
    }
    std::cerr << std::endl;
    
    streamBatches(writer, format);
    
    std::cerr << "Serialization completed." << std::endl;
}

void DatabaseSerializer::streamBatches(RdfOutputWriter& writer, const std::string& format, size_t batchSize) {
    ProgressTracker progress;
    progress.start();
    
    size_t offset = 0;
    size_t totalTriples = 0;
    std::string batchBuffer;
    batchBuffer.reserve(batchSize * 200); // Estimate ~200 chars per triple
    
    // Redirect QLever's verbose logging
    std::streambuf* clogBuf = std::clog.rdbuf();
    std::ofstream devNull("/dev/null");
    
    while (true) {
        std::clog.rdbuf(devNull.rdbuf()); // Silence QLever logging
        
        std::string sparqlQuery = "SELECT * WHERE { ?s ?p ?o } OFFSET " + 
                                std::to_string(offset) + " LIMIT " + std::to_string(batchSize);
        std::string result;
        
        try {
            result = qlever_->query(sparqlQuery, ad_utility::MediaType::sparqlJson);
        } catch (const std::exception& e) {
            std::clog.rdbuf(clogBuf); // Restore logging
            throw std::runtime_error("Query failed: " + std::string(e.what()));
        }
        
        std::clog.rdbuf(clogBuf); // Restore logging
        
        // Parse JSON result
        try {
            auto jsonResult = nlohmann::json::parse(result);
            if (!jsonResult.contains("results") || !jsonResult["results"].contains("bindings")) {
                break; // No more results
            }
            
            auto bindings = jsonResult["results"]["bindings"];
            if (bindings.empty()) {
                break; // No more results
            }
            
            batchBuffer.clear();
            
            // Process each binding
            for (const auto& binding : bindings) {
                if (binding.contains("s") && binding.contains("p") && binding.contains("o")) {
                    std::string subject = QueryExecutor::extractValue(binding["s"].dump(), "value");
                    std::string predicate = QueryExecutor::extractValue(binding["p"].dump(), "value");
                    std::string object = QueryExecutor::extractValue(binding["o"].dump(), "value");
                    
                    if (!subject.empty() && !predicate.empty() && !object.empty()) {
                        // Add angle brackets for URIs if not already present
                        if (subject.front() != '<' && subject.find(':') != std::string::npos) {
                            subject = "<" + subject + ">";
                        }
                        if (predicate.front() != '<' && predicate.find(':') != std::string::npos) {
                            predicate = "<" + predicate + ">";
                        }
                        if (object.front() != '<' && object.front() != '"' && object.find(':') != std::string::npos) {
                            object = "<" + object + ">";
                        }
                        
                        std::string formattedTriple = RdfFormatUtils::formatTriple(subject, predicate, object, format);
                        batchBuffer += formattedTriple;
                        totalTriples++;
                    }
                }
            }
            
            // Write batch to output
            writer.writeRawTriple(batchBuffer);
            writer.flush();
            
            // Progress logging
            if (progress.shouldLog()) {
                progress.logProgress(totalTriples, "triples");
            }
            
            offset += batchSize;
            
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to parse query results: " + std::string(e.what()));
        }
    }
    
    std::cerr << "Serialization completed. Total triples: " << totalTriples;
    if (progress.getElapsedTime().count() > 0) {
        std::cerr << " (" << static_cast<int>(progress.getItemsPerSecond(totalTriples)) << "/sec)";
    }
    std::cerr << std::endl;
}

} // namespace cli_utils