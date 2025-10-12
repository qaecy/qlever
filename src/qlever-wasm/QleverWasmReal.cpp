#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <chrono>
#include <fstream>

// Include QLever headers for real functionality
#include "index/Index.h"
#include "util/AllocatorWithLimit.h"
#include "util/MemorySize/MemorySize.h"
#include "util/IndexTestHelpers.h"
#include "engine/QueryExecutionContext.h"
#include "parser/SparqlParser.h"
#include "engine/QueryPlanner.h"
#include "engine/ExportQueryExecutionTrees.h"

class QleverWasmReal {
private:
    std::unique_ptr<QueryExecutionContext> qec_;
    bool isInitialized_;
    std::string lastError_;
    size_t memoryLimitMB_;
    std::string indexData_;  // Store the RDF data

    // Build JSON response helper
    std::string buildJsonResponse(bool success, 
                                  const std::string& message = "",
                                  const std::string& error = "",
                                  const std::string& extra = "") {
        std::stringstream ss;
        ss << "{";
        ss << "\"success\":" << (success ? "true" : "false");
        
        if (!message.empty()) {
            ss << ",\"message\":\"" << message << "\"";
        }
        
        if (!error.empty()) {
            ss << ",\"error\":\"" << error << "\"";
        }
        
        if (!extra.empty()) {
            ss << "," << extra;
        }
        
        ss << ",\"timestamp\":" << std::time(nullptr);
        ss << "}";
        
        return ss.str();
    }

    // Escape JSON string
    std::string escapeJson(const std::string& input) {
        std::ostringstream ss;
        for (auto iter = input.cbegin(); iter != input.cend(); iter++) {
            switch (*iter) {
                case '\\': ss << "\\\\"; break;
                case '"': ss << "\\\""; break;
                case '/': ss << "\\/"; break;
                case '\b': ss << "\\b"; break;
                case '\f': ss << "\\f"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default: ss << *iter; break;
            }
        }
        return ss.str();
    }

public:
    QleverWasmReal() : isInitialized_(false), memoryLimitMB_(1024) {}

    /**
     * @brief Initialize QLever with in-memory index from RDF data
     * @param rdfData RDF data in Turtle format
     * @param memoryLimitMB Memory limit in MB
     * @return JSON response with initialization result
     */
    std::string initializeFromRdf(const std::string& rdfData, int memoryLimitMB = 1024) {
        try {
            memoryLimitMB_ = memoryLimitMB;
            indexData_ = rdfData;
            
            // Use QLever's test helper to create an in-memory index directly from the Turtle string
            ad_utility::testing::TestIndexConfig config{rdfData};
            config.createTextIndex = false;  // Disable text index for simplicity
            config.loadAllPermutations = true;
            config.usePatterns = false;  // Disable patterns for simplicity
            
            // Create the QueryExecutionContext with the in-memory index
            qec_ = ad_utility::testing::getQec(std::move(config));
            isInitialized_ = true;
            
            std::stringstream extra;
            extra << "\"memoryLimitMB\":" << memoryLimitMB << ",";
            extra << "\"dataSize\":" << rdfData.length() << ",";
            extra << "\"method\":\"in-memory-index\",";
            extra << "\"note\":\"Real QLever WASM with in-memory index from test helpers\"";
            
            return buildJsonResponse(true, "QLever initialized successfully with RDF data", "", extra.str());
            
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return buildJsonResponse(false, "", "Failed to initialize QLever: " + std::string(e.what()));
        }
    }

    /**
     * @brief Execute a SPARQL query
     * @param queryString SPARQL query to execute
     * @param format Output format (sparql-json, csv, tsv, etc.)
     * @return JSON response with query results
     */
    std::string query(const std::string& queryString, const std::string& format = "sparql-json") {
        if (!isInitialized_) {
            return buildJsonResponse(false, "", "QLever not initialized. Call initializeFromRdf() first.");
        }
        
        try {
            // Map format string to MediaType
            ad_utility::MediaType mediaType;
            if (format == "sparql-json") {
                mediaType = ad_utility::MediaType::sparqlJson;
            } else if (format == "csv") {
                mediaType = ad_utility::MediaType::csv;
            } else if (format == "tsv") {
                mediaType = ad_utility::MediaType::tsv;
            } else if (format == "sparql-xml") {
                mediaType = ad_utility::MediaType::sparqlXml;
            } else if (format == "qlever-json") {
                mediaType = ad_utility::MediaType::qleverJson;
            } else {
                return buildJsonResponse(false, "", "Unsupported format: " + format);
            }
            
            // Parse the query
            auto startTime = std::chrono::high_resolution_clock::now();
            SparqlParser sparqlParser;
            auto parsedQuery = sparqlParser.parseQuery(queryString);
            
            // Create query planner and execution tree
            auto cancellationHandle = std::make_shared<ad_utility::CancellationHandle<>>();
            QueryPlanner queryPlanner{qec_.get(), cancellationHandle};
            auto queryExecutionTree = queryPlanner.createExecutionTree(parsedQuery);
            
            // Execute query and get result
            ad_utility::Timer timer{ad_utility::Timer::Started};
            std::string result;
            auto generator = ExportQueryExecutionTrees::computeResult(
                parsedQuery, queryExecutionTree, mediaType, timer, std::move(cancellationHandle));
            
            for (const auto& chunk : generator) {
                result += chunk;
            }
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            std::stringstream extra;
            extra << "\"query\":\"" << escapeJson(queryString) << "\",";
            extra << "\"format\":\"" << format << "\",";
            extra << "\"executionTimeMs\":" << duration.count() << ",";
            extra << "\"result\":\"" << escapeJson(result) << "\",";
            extra << "\"note\":\"Real QLever query execution\"";
            
            return buildJsonResponse(true, "Query executed successfully", "", extra.str());
            
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return buildJsonResponse(false, "", "Query execution failed: " + std::string(e.what()));
        }
    }

    /**
     * @brief Parse and plan a query without executing it
     * @param queryString SPARQL query to parse and plan
     * @return JSON response with parsing result
     */
    std::string parseAndPlan(const std::string& queryString) {
        if (!isInitialized_) {
            return buildJsonResponse(false, "", "QLever not initialized. Call initializeFromRdf() first.");
        }
        
        try {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            // Parse the query
            SparqlParser sparqlParser;
            auto parsedQuery = sparqlParser.parseQuery(queryString);
            
            // Create execution tree (planning)
            auto cancellationHandle = std::make_shared<ad_utility::CancellationHandle<>>();
            QueryPlanner queryPlanner{qec_.get(), cancellationHandle};
            auto queryExecutionTree = queryPlanner.createExecutionTree(parsedQuery);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            std::stringstream extra;
            extra << "\"query\":\"" << escapeJson(queryString) << "\",";
            extra << "\"planningTimeMs\":" << duration.count() << ",";
            extra << "\"note\":\"Real QLever query parsing and planning\"";
            
            return buildJsonResponse(true, "Query parsed and planned successfully", "", extra.str());
            
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return buildJsonResponse(false, "", "Query parsing failed: " + std::string(e.what()));
        }
    }

    /**
     * @brief Check if QLever is initialized and ready
     * @return true if ready for queries
     */
    bool isReady() const {
        return isInitialized_ && qec_ != nullptr;
    }

    /**
     * @brief Get current status and statistics
     * @return JSON response with status information
     */
    std::string getStatus() {
        try {
            std::stringstream extra;
            extra << "\"initialized\":" << (isInitialized_ ? "true" : "false") << ",";
            extra << "\"memoryLimitMB\":" << memoryLimitMB_ << ",";
            extra << "\"dataSize\":" << indexData_.length() << ",";
            extra << "\"version\":\"QLever WASM Real 1.0.0\",";
            
            if (!lastError_.empty()) {
                extra << "\"lastError\":\"" << escapeJson(lastError_) << "\",";
            }
            
            if (isInitialized_ && qec_) {
                try {
                    // Get basic index statistics
                    auto& index = qec_->getIndex();
                    auto numTriples = index.numTriples();
                    extra << "\"indexLoaded\":true,";
                    extra << "\"numTriples\":" << numTriples.normal << ",";
                } catch (...) {
                    extra << "\"indexLoaded\":false,";
                }
            }
            
            extra << "\"note\":\"Real QLever WASM implementation using test helpers\"";
            
            return buildJsonResponse(true, "", "", extra.str());
            
        } catch (const std::exception& e) {
            return buildJsonResponse(false, "", "Status check failed: " + std::string(e.what()));
        }
    }

    /**
     * @brief Get the last error message
     * @return Last error that occurred
     */
    std::string getLastError() const {
        return lastError_;
    }
};

// Emscripten bindings
EMSCRIPTEN_BINDINGS(qlever_wasm_real) {
    emscripten::class_<QleverWasmReal>("QleverWasm")
        .constructor<>()
        .function("initializeFromRdf", &QleverWasmReal::initializeFromRdf)
        .function("query", &QleverWasmReal::query)
        .function("parseAndPlan", &QleverWasmReal::parseAndPlan)
        .function("isReady", &QleverWasmReal::isReady)
        .function("getStatus", &QleverWasmReal::getStatus)
        .function("getLastError", &QleverWasmReal::getLastError);
}