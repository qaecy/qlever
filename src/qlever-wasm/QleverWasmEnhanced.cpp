#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <regex>

/**
 * @brief Enhanced QLever WASM wrapper with simple RDF parsing and querying
 * 
 * This implementation provides a more realistic demonstration by actually parsing
 * RDF triples from input data and responding to basic SPARQL queries.
 * It uses lightweight C++ string processing instead of heavy dependencies.
 */

struct Triple {
    std::string subject;
    std::string predicate;
    std::string object;
    std::string objectType;  // "uri" or "literal"
};

class QleverWasmEnhanced {
private:
    bool isInitialized_;
    std::string indexBasename_;
    std::vector<Triple> triples_;
    std::unordered_map<std::string, std::string> prefixes_;

    // Utility functions
    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    std::string escapeJson(const std::string& str) {
        std::string result;
        for (char c : str) {
            switch (c) {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }

    // Simple RDF parsers
    bool parseNTriple(const std::string& line) {
        // Simple N-Triple parser: <subject> <predicate> <object> .
        std::regex ntPattern(R"(^\s*<([^>]+)>\s+<([^>]+)>\s+(.+)\s*\.\s*$)");
        std::smatch match;
        
        if (std::regex_match(line, match, ntPattern)) {
            Triple triple;
            triple.subject = match[1].str();
            triple.predicate = match[2].str();
            
            std::string objStr = match[3].str();
            
            // Check if object is URI or literal
            if (objStr.front() == '<' && objStr.back() == '>') {
                triple.object = objStr.substr(1, objStr.length() - 2);
                triple.objectType = "uri";
            } else if (objStr.front() == '"') {
                // Find the closing quote
                size_t closeQuote = objStr.find_last_of('"');
                if (closeQuote > 0) {
                    triple.object = objStr.substr(1, closeQuote - 1);
                    triple.objectType = "literal";
                } else {
                    return false;
                }
            } else {
                // Assume literal without quotes
                triple.object = objStr;
                triple.objectType = "literal";
            }
            
            triples_.push_back(triple);
            return true;
        }
        return false;
    }

    bool parseTurtleLine(const std::string& line) {
        // Very basic Turtle parsing - prefix declarations and simple triples
        std::string trimmedLine = trim(line);
        
        if (trimmedLine.empty() || trimmedLine[0] == '#') {
            return true; // Skip empty lines and comments
        }
        
        // Handle prefix declarations
        std::regex prefixPattern(R"(^@prefix\s+(\w+):\s+<([^>]+)>\s*\.\s*$)");
        std::smatch prefixMatch;
        if (std::regex_match(trimmedLine, prefixMatch, prefixPattern)) {
            prefixes_[prefixMatch[1].str()] = prefixMatch[2].str();
            return true;
        }
        
        // Try to parse as N-Triple first
        return parseNTriple(trimmedLine);
    }

    std::string expandPrefix(const std::string& prefixed) {
        size_t colonPos = prefixed.find(':');
        if (colonPos != std::string::npos) {
            std::string prefix = prefixed.substr(0, colonPos);
            std::string suffix = prefixed.substr(colonPos + 1);
            
            auto it = prefixes_.find(prefix);
            if (it != prefixes_.end()) {
                return it->second + suffix;
            }
        }
        return prefixed;
    }

    // Simple SPARQL query processing
    std::vector<std::unordered_map<std::string, std::pair<std::string, std::string>>> 
    executeSelectQuery(const std::string& query) {
        std::vector<std::unordered_map<std::string, std::pair<std::string, std::string>>> results;
        
        // Extract variables from SELECT clause
        std::regex selectPattern(R"(SELECT\s+(.*?)\s+WHERE)", std::regex_constants::icase);
        std::smatch selectMatch;
        std::vector<std::string> variables;
        
        if (std::regex_search(query, selectMatch, selectPattern)) {
            std::string varString = selectMatch[1].str();
            std::regex varPattern(R"(\?(\w+))");
            std::sregex_iterator iter(varString.begin(), varString.end(), varPattern);
            std::sregex_iterator end;
            
            for (; iter != end; ++iter) {
                variables.push_back(iter->str(1));
            }
        }
        
        // Simple pattern matching in WHERE clause
        std::regex wherePattern(R"(WHERE\s*\{([^}]+)\})", std::regex_constants::icase);
        std::smatch whereMatch;
        
        if (std::regex_search(query, whereMatch, wherePattern)) {
            std::string whereClause = whereMatch[1].str();
            
            // Extract triple patterns
            std::regex triplePattern(R"(([?:\w<>]+)\s+([?:\w<>]+)\s+([?:\w<>"]+))");
            std::sregex_iterator tripleIter(whereClause.begin(), whereClause.end(), triplePattern);
            std::sregex_iterator tripleEnd;
            
            // For each stored triple, check if it matches the pattern
            for (const auto& triple : triples_) {
                std::unordered_map<std::string, std::pair<std::string, std::string>> binding;
                bool matches = true;
                
                for (auto iter = tripleIter; iter != tripleEnd && matches; ++iter) {
                    std::string subjectPattern = trim(iter->str(1));
                    std::string predicatePattern = trim(iter->str(2));
                    std::string objectPattern = trim(iter->str(3));
                    
                    // Simple variable binding logic
                    if (subjectPattern[0] == '?') {
                        std::string varName = subjectPattern.substr(1);
                        binding[varName] = {triple.subject, "uri"};
                    } else if (subjectPattern != triple.subject && 
                               expandPrefix(subjectPattern) != triple.subject) {
                        matches = false;
                    }
                    
                    if (matches && predicatePattern[0] == '?') {
                        std::string varName = predicatePattern.substr(1);
                        binding[varName] = {triple.predicate, "uri"};
                    } else if (matches && predicatePattern != triple.predicate && 
                               expandPrefix(predicatePattern) != triple.predicate) {
                        matches = false;
                    }
                    
                    if (matches && objectPattern[0] == '?') {
                        std::string varName = objectPattern.substr(1);
                        binding[varName] = {triple.object, triple.objectType};
                    } else if (matches && objectPattern != triple.object && 
                               expandPrefix(objectPattern) != triple.object) {
                        matches = false;
                    }
                }
                
                if (matches && !binding.empty()) {
                    results.push_back(binding);
                }
            }
        }
        
        return results;
    }

    std::string buildJsonResponse(const std::string& success, 
                                  const std::string& message = "",
                                  const std::string& error = "",
                                  const std::string& extra = "") {
        std::stringstream ss;
        ss << "{";
        ss << "\"success\":" << success;
        
        if (!message.empty()) {
            ss << ",\"message\":\"" << escapeJson(message) << "\"";
        }
        
        if (!error.empty()) {
            ss << ",\"error\":\"" << escapeJson(error) << "\"";
        }
        
        if (!extra.empty()) {
            ss << "," << extra;
        }
        
        ss << ",\"timestamp\":" << std::time(nullptr);
        ss << "}";
        
        return ss.str();
    }

public:
    QleverWasmEnhanced() : isInitialized_(false) {}

    /**
     * @brief Initialize with memory-based index
     */
    std::string initialize(const std::string& indexBasename, int memoryLimitMB = 1024) {
        indexBasename_ = indexBasename;
        isInitialized_ = true;
        triples_.clear();
        prefixes_.clear();
        
        // Add some common prefixes
        prefixes_["rdf"] = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
        prefixes_["rdfs"] = "http://www.w3.org/2000/01/rdf-schema#";
        prefixes_["foaf"] = "http://xmlns.com/foaf/0.1/";
        prefixes_["ex"] = "http://example.org/";
        
        std::stringstream extra;
        extra << "\"indexBasename\":\"" << escapeJson(indexBasename) << "\",";
        extra << "\"memoryLimitMB\":" << memoryLimitMB << ",";
        extra << "\"note\":\"Enhanced QLever WASM with simple RDF parsing\"";
        
        return buildJsonResponse("true", "Enhanced QLever initialized successfully", "", extra.str());
    }

    /**
     * @brief Initialize from RDF data string
     */
    std::string initializeFromRdf(const std::string& rdfData, const std::string& format = "turtle") {
        if (!initialize("memory-index", 1024).find("\"success\":true") != std::string::npos) {
            return buildJsonResponse("false", "", "Failed to initialize base system");
        }

        std::stringstream ss(rdfData);
        std::string line;
        int parsedTriples = 0;
        int totalLines = 0;
        
        while (std::getline(ss, line)) {
            totalLines++;
            line = trim(line);
            
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            bool success = false;
            if (format == "turtle" || format == "ttl") {
                success = parseTurtleLine(line);
            } else if (format == "ntriples" || format == "nt") {
                success = parseNTriple(line);
            } else {
                // Try both formats
                success = parseTurtleLine(line) || parseNTriple(line);
            }
            
            if (success) {
                parsedTriples++;
            }
        }
        
        std::stringstream extra;
        extra << "\"triplesLoaded\":" << parsedTriples << ",";
        extra << "\"totalLines\":" << totalLines << ",";
        extra << "\"format\":\"" << format << "\",";
        extra << "\"prefixes\":" << prefixes_.size() << ",";
        extra << "\"note\":\"Enhanced QLever WASM with parsed RDF data\"";
        
        return buildJsonResponse("true", 
                                "RDF data parsed successfully. Loaded " + std::to_string(parsedTriples) + " triples.", 
                                "", 
                                extra.str());
    }

    /**
     * @brief Execute SPARQL query on loaded data
     */
    std::string query(const std::string& queryString, const std::string& format = "sparql-json") {
        if (!isInitialized_) {
            return buildJsonResponse("false", "", "QLever not initialized. Call initialize() first.");
        }
        
        try {
            auto results = executeSelectQuery(queryString);
            
            // Build SPARQL JSON response
            std::stringstream sparqlJson;
            sparqlJson << "{";
            sparqlJson << "\\\"head\\\":{\\\"vars\\\":[";
            
            // Extract variables from first result
            if (!results.empty()) {
                bool first = true;
                for (const auto& binding : results[0]) {
                    if (!first) sparqlJson << ",";
                    sparqlJson << "\\\"" << binding.first << "\\\"";
                    first = false;
                }
            }
            
            sparqlJson << "]},";
            sparqlJson << "\\\"results\\\":{\\\"bindings\\\":[";
            
            // Add result bindings
            for (size_t i = 0; i < results.size(); ++i) {
                if (i > 0) sparqlJson << ",";
                sparqlJson << "{";
                
                bool firstBinding = true;
                for (const auto& binding : results[i]) {
                    if (!firstBinding) sparqlJson << ",";
                    sparqlJson << "\\\"" << binding.first << "\\\":";
                    sparqlJson << "{\\\"type\\\":\\\"" << binding.second.second << "\\\",";
                    sparqlJson << "\\\"value\\\":\\\"" << escapeJson(binding.second.first) << "\\\"}";
                    firstBinding = false;
                }
                
                sparqlJson << "}";
            }
            
            sparqlJson << "]}}";
            
            std::stringstream extra;
            // Wrap the result in quotes and escape it for valid JSON
            extra << "\"result\":" << sparqlJson.str() << ",";
            extra << "\"resultCount\":" << results.size() << ",";
            extra << "\"triplesInIndex\":" << triples_.size() << ",";
            extra << "\"queryType\":\"SELECT\"";
            
            return buildJsonResponse("true", 
                                    "Query executed successfully. Found " + std::to_string(results.size()) + " results.", 
                                    "", 
                                    extra.str());
            
        } catch (const std::exception& e) {
            return buildJsonResponse("false", "", 
                                    "Query execution failed: " + std::string(e.what()));
        }
    }

    /**
     * @brief Get statistics about loaded data
     */
    std::string getStats() {
        if (!isInitialized_) {
            return buildJsonResponse("false", "", "QLever not initialized");
        }
        
        std::stringstream extra;
        extra << "\"triplesCount\":" << triples_.size() << ",";
        extra << "\"prefixesCount\":" << prefixes_.size() << ",";
        extra << "\"indexBasename\":\"" << escapeJson(indexBasename_) << "\"";
        
        return buildJsonResponse("true", 
                                "Statistics retrieved successfully", 
                                "", 
                                extra.str());
    }
};

// Emscripten bindings
EMSCRIPTEN_BINDINGS(qlever_wasm_enhanced) {
    emscripten::class_<QleverWasmEnhanced>("QleverWasmEnhanced")
        .constructor<>()
        .function("initialize", &QleverWasmEnhanced::initialize)
        .function("initializeFromRdf", &QleverWasmEnhanced::initializeFromRdf)
        .function("query", &QleverWasmEnhanced::query)
        .function("getStats", &QleverWasmEnhanced::getStats);
}