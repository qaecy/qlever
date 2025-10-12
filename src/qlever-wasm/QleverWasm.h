#ifndef QLEVER_SRC_QLEVER_WASM_QLEVERWASM_H
#define QLEVER_SRC_QLEVER_WASM_QLEVERWASM_H

#include <string>
#include <memory>
#include "libqlever/Qlever.h"

/**
 * @brief WebAssembly wrapper for QLever query engine
 * 
 * This header defines the WebAssembly interface for QLever.
 * The implementation provides JavaScript-friendly methods for
 * initializing QLever, executing queries, and managing results.
 */
class QleverWasm {
private:
    std::shared_ptr<qlever::Qlever> qlever_;
    std::string indexBasename_;
    bool isInitialized_;

public:
    QleverWasm();
    
    /**
     * @brief Initialize QLever with an index
     * @param indexBasename Path to the index files (without extension)
     * @param memoryLimitMB Memory limit in megabytes (default: 1024)
     * @return JSON string with success/error status
     */
    std::string initialize(const std::string& indexBasename, int memoryLimitMB = 1024);

    /**
     * @brief Execute a SPARQL query
     * @param queryString The SPARQL query to execute
     * @param format Output format: "sparql-json", "csv", "tsv", "sparql-xml", "qlever-json"
     * @return JSON string with query results or error
     */
    std::string query(const std::string& queryString, const std::string& format = "sparql-json");

    /**
     * @brief Parse and plan a query without executing it
     * @param queryString The SPARQL query to parse and plan
     * @return JSON string with parsing success status
     */
    std::string parseAndPlan(const std::string& queryString);

    /**
     * @brief Check if QLever is initialized
     * @return true if initialized, false otherwise
     */
    bool isReady() const;

    /**
     * @brief Get current status information
     * @return JSON string with status information
     */
    std::string getStatus();
};

#endif // QLEVER_SRC_QLEVER_WASM_QLEVERWASM_H