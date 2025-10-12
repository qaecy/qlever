#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <sstream>

/**
 * @brief Minimal QLever WASM wrapper for demonstration
 * 
 * This is a simplified version that doesn't require external dependencies.
 * It demonstrates the API structure and can be used for UI development.
 */
class QleverWasmDemo {
private:
    bool isInitialized_;
    std::string indexBasename_;

    // Simple JSON-like string building helper
    std::string buildJsonResponse(const std::string& success, 
                                  const std::string& message = "",
                                  const std::string& error = "",
                                  const std::string& extra = "") {
        std::stringstream ss;
        ss << "{";
        ss << "\"success\":" << success;
        
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

public:
    QleverWasmDemo() : isInitialized_(false) {}

    /**
     * @brief Mock initialize function
     */
    std::string initialize(const std::string& indexBasename, int memoryLimitMB = 1024) {
        indexBasename_ = indexBasename;
        isInitialized_ = true;
        
        std::stringstream extra;
        extra << "\"indexBasename\":\"" << indexBasename << "\",";
        extra << "\"memoryLimitMB\":" << memoryLimitMB << ",";
        extra << "\"note\":\"Demo QLever WASM - mock implementation\"";
        
        return buildJsonResponse("true", "Demo QLever initialized successfully", "", extra.str());
    }

    /**
     * @brief Mock query function
     */
    std::string query(const std::string& queryString, const std::string& format = "sparql-json") {
        if (!isInitialized_) {
            return buildJsonResponse("false", "", "QLever not initialized. Call initialize() first.");
        }
        
        // Create a mock SPARQL JSON result
        std::stringstream mockResult;
        mockResult << "\"{"
                  << "\\\"head\\\":{\\\"vars\\\":[\\\"s\\\",\\\"p\\\",\\\"o\\\"]},"
                  << "\\\"results\\\":{\\\"bindings\\\":["
                  << "{\\\"s\\\":{\\\"type\\\":\\\"uri\\\",\\\"value\\\":\\\"http://example.org/subject1\\\"},"
                  << "\\\"p\\\":{\\\"type\\\":\\\"uri\\\",\\\"value\\\":\\\"http://example.org/predicate1\\\"},"
                  << "\\\"o\\\":{\\\"type\\\":\\\"literal\\\",\\\"value\\\":\\\"Mock Object 1\\\"}},"
                  << "{\\\"s\\\":{\\\"type\\\":\\\"uri\\\",\\\"value\\\":\\\"http://example.org/subject2\\\"},"
                  << "\\\"p\\\":{\\\"type\\\":\\\"uri\\\",\\\"value\\\":\\\"http://example.org/predicate2\\\"},"
                  << "\\\"o\\\":{\\\"type\\\":\\\"literal\\\",\\\"value\\\":\\\"Mock Object 2\\\"}}"
                  << "]}"
                  << "}\"";
        
        std::stringstream extra;
        extra << "\"query\":\"" << queryString << "\",";
        extra << "\"format\":\"" << format << "\",";
        extra << "\"executionTimeMs\":42,";
        extra << "\"result\":" << mockResult.str() << ",";
        extra << "\"note\":\"Mock result - demo implementation\"";
        
        return buildJsonResponse("true", "Query executed successfully", "", extra.str());
    }

    /**
     * @brief Mock parse and plan function
     */
    std::string parseAndPlan(const std::string& queryString) {
        std::stringstream extra;
        extra << "\"query\":\"" << queryString << "\",";
        extra << "\"planningTimeMs\":5,";
        extra << "\"note\":\"Mock parse/plan - demo implementation\"";
        
        return buildJsonResponse("true", "Query parsed and planned successfully", "", extra.str());
    }

    /**
     * @brief Check if initialized
     */
    bool isReady() const {
        return isInitialized_;
    }

    /**
     * @brief Get status
     */
    std::string getStatus() {
        std::stringstream extra;
        extra << "\"initialized\":" << (isInitialized_ ? "true" : "false") << ",";
        extra << "\"indexBasename\":\"" << indexBasename_ << "\",";
        extra << "\"version\":\"QLever WASM Demo 1.0.0\",";
        extra << "\"note\":\"Demonstration build with mock functionality\"";
        
        return buildJsonResponse("true", "", "", extra.str());
    }
};

// Emscripten bindings
EMSCRIPTEN_BINDINGS(qlever_wasm_demo) {
    emscripten::class_<QleverWasmDemo>("QleverWasm")
        .constructor<>()
        .function("initialize", &QleverWasmDemo::initialize)
        .function("query", &QleverWasmDemo::query)
        .function("parseAndPlan", &QleverWasmDemo::parseAndPlan)
        .function("isReady", &QleverWasmDemo::isReady)
        .function("getStatus", &QleverWasmDemo::getStatus);
}