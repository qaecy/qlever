#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <memory>
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "libqlever/Qlever.h"
#include "util/http/MediaTypes.h"
#include "util/Log.h"

using json = nlohmann::json;

/**
 * @brief WebAssembly wrapper for QLever query engine
 * 
 * This class provides a JavaScript-friendly interface to QLever's query functionality.
 * It handles index loading, query execution, and result formatting for web environments.
 */
class QleverWasm {
private:
    std::shared_ptr<qlever::Qlever> qlever_;
    std::string indexBasename_;
    bool isInitialized_;

public:
    QleverWasm() : isInitialized_(false) {}

    /**
     * @brief Initialize QLever with an index
     * @param indexBasename Path to the index files (without extension)
     * @param memoryLimitMB Memory limit in megabytes (default: 1024)
     * @return JSON string with success/error status
     */
    std::string initialize(const std::string& indexBasename, int memoryLimitMB = 1024) {
        json response;
        try {
            indexBasename_ = indexBasename;
            
            qlever::EngineConfig config;
            config.baseName_ = indexBasename;
            config.memoryLimit_ = ad_utility::MemorySize::megabytes(memoryLimitMB);
            
            qlever_ = std::make_shared<qlever::Qlever>(config);
            isInitialized_ = true;
            
            response["success"] = true;
            response["message"] = "QLever initialized successfully";
            response["indexBasename"] = indexBasename;
            response["memoryLimitMB"] = memoryLimitMB;
            
        } catch (const std::exception& e) {
            isInitialized_ = false;
            response["success"] = false;
            response["error"] = e.what();
        }
        
        return response.dump();
    }

    /**
     * @brief Execute a SPARQL query
     * @param queryString The SPARQL query to execute
     * @param format Output format: "sparql-json", "csv", "tsv", "sparql-xml", "qlever-json"
     * @return JSON string with query results or error
     */
    std::string query(const std::string& queryString, const std::string& format = "sparql-json") {
        json response;
        
        if (!isInitialized_) {
            response["success"] = false;
            response["error"] = "QLever not initialized. Call initialize() first.";
            return response.dump();
        }
        
        try {
            ad_utility::MediaType mediaType = getMediaType(format);
            
            auto start = std::chrono::high_resolution_clock::now();
            std::string result = qlever_->query(queryString, mediaType);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            response["success"] = true;
            response["result"] = result;
            response["query"] = queryString;
            response["format"] = format;
            response["executionTimeMs"] = duration.count();
            response["indexBasename"] = indexBasename_;
            
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            response["query"] = queryString;
        }
        
        return response.dump();
    }

    /**
     * @brief Parse and plan a query without executing it
     * @param queryString The SPARQL query to parse and plan
     * @return JSON string with parsing success status
     */
    std::string parseAndPlan(const std::string& queryString) {
        json response;
        
        if (!isInitialized_) {
            response["success"] = false;
            response["error"] = "QLever not initialized. Call initialize() first.";
            return response.dump();
        }
        
        try {
            auto start = std::chrono::high_resolution_clock::now();
            auto queryPlan = qlever_->parseAndPlanQuery(queryString);
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            
            response["success"] = true;
            response["message"] = "Query parsed and planned successfully";
            response["query"] = queryString;
            response["planningTimeMs"] = duration.count();
            
        } catch (const std::exception& e) {
            response["success"] = false;
            response["error"] = e.what();
            response["query"] = queryString;
        }
        
        return response.dump();
    }

    /**
     * @brief Check if QLever is initialized
     * @return true if initialized, false otherwise
     */
    bool isReady() const {
        return isInitialized_;
    }

    /**
     * @brief Get current status information
     * @return JSON string with status information
     */
    std::string getStatus() {
        json response;
        response["initialized"] = isInitialized_;
        response["indexBasename"] = indexBasename_;
        response["version"] = "QLever WASM 1.0.0";
        return response.dump();
    }

private:
    ad_utility::MediaType getMediaType(const std::string& format) {
        if (format == "csv") return ad_utility::MediaType::csv;
        if (format == "tsv") return ad_utility::MediaType::tsv;
        if (format == "sparql-xml") return ad_utility::MediaType::sparqlXml;
        if (format == "qlever-json") return ad_utility::MediaType::qleverJson;
        return ad_utility::MediaType::sparqlJson; // default
    }
};

// Emscripten bindings
EMSCRIPTEN_BINDINGS(qlever_wasm) {
    emscripten::class_<QleverWasm>("QleverWasm")
        .constructor<>()
        .function("initialize", &QleverWasm::initialize)
        .function("query", &QleverWasm::query)
        .function("parseAndPlan", &QleverWasm::parseAndPlan)
        .function("isReady", &QleverWasm::isReady)
        .function("getStatus", &QleverWasm::getStatus);
}