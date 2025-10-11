#pragma once

#include <string>
#include <memory>
#include "libqlever/Qlever.h"
#include "RdfOutputUtils.h"

namespace cli_utils {

/**
 * @brief Query execution utilities for CLI operations
 */
class QueryExecutor {
private:
    std::shared_ptr<qlever::Qlever> qlever_;
    
public:
    explicit QueryExecutor(std::shared_ptr<qlever::Qlever> qlever);
    
    // Execute regular SPARQL query (SELECT, ASK, etc.)
    std::string executeQuery(const std::string& query, const std::string& format = "sparql-json");
    
    // Execute CONSTRUCT query with streaming output to file
    void executeConstructQuery(const std::string& query, const std::string& outputFormat, 
                             const std::string& outputFile = "");
    
    // Check if query is a CONSTRUCT query
    static bool isConstructQuery(const std::string& query);
    
    // Extract value from JSON string (utility function)
    static std::string extractValue(const std::string& json, const std::string& key);
};

} // namespace cli_utils