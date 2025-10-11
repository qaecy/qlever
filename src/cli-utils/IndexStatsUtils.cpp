#include "IndexStatsUtils.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include "util/Timer.h"

namespace cli_utils {

IndexStatsCollector::IndexStatsCollector(std::shared_ptr<qlever::Qlever> qlever) 
    : qlever_(std::move(qlever)) {}

void IndexStatsCollector::runStatsQuery(nlohmann::json& response, const std::string& name, 
                                       const std::string& query) const {
    // Suppress QLever's verbose logging for queries
    std::ofstream nullStream("/dev/null");
    std::streambuf* originalCerrBuf = std::cerr.rdbuf();
    
    try {
        std::cerr.rdbuf(nullStream.rdbuf()); // Suppress logs
        ad_utility::Timer timer{ad_utility::Timer::Started};
        std::string result = qlever_->query(query);
        auto executionTime = timer.msecs().count();
        std::cerr.rdbuf(originalCerrBuf); // Restore logs
        
        response[name] = {
            {"query", query},
            {"result", result},
            {"executionTimeMs", executionTime}
        };
    } catch (const std::exception& e) {
        std::cerr.rdbuf(originalCerrBuf); // Restore logs
        response[name] = {
            {"query", query},
            {"error", e.what()},
            {"executionTimeMs", 0}
        };
    }
}

nlohmann::json IndexStatsCollector::collectStats(const std::string& indexBasename) const {
    nlohmann::json response;
    response["success"] = true;
    response["indexBasename"] = indexBasename;
    response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::cerr << "Gathering index statistics for " << indexBasename << "..." << std::endl;

    // Basic triple count
    runStatsQuery(response, "tripleCount", 
        "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o }");
    
    // Distinct subjects, predicates, objects
    runStatsQuery(response, "distinctSubjects", 
        "SELECT (COUNT(DISTINCT ?s) AS ?count) WHERE { ?s ?p ?o }");
    runStatsQuery(response, "distinctPredicates", 
        "SELECT (COUNT(DISTINCT ?p) AS ?count) WHERE { ?s ?p ?o }");
    runStatsQuery(response, "distinctObjects", 
        "SELECT (COUNT(DISTINCT ?o) AS ?count) WHERE { ?s ?p ?o }");
    
    // Graph information (for nquads)
    runStatsQuery(response, "distinctGraphs", 
        "SELECT (COUNT(DISTINCT ?g) AS ?count) WHERE { GRAPH ?g { ?s ?p ?o } }");
    
    // Most frequent predicates
    runStatsQuery(response, "topPredicates", 
        "SELECT ?p (COUNT(*) AS ?count) WHERE { ?s ?p ?o } "
        "GROUP BY ?p ORDER BY DESC(?count) LIMIT 10");
        
    // Basic schema information
    runStatsQuery(response, "classesCount", 
        "SELECT (COUNT(DISTINCT ?s) AS ?count) WHERE { ?s a ?type }");
    
    // Literal vs IRI objects
    runStatsQuery(response, "literalObjects", 
        "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o . FILTER(isLiteral(?o)) }");
    runStatsQuery(response, "iriObjects", 
        "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o . FILTER(isIRI(?o)) }");
    
    // Blank node information
    runStatsQuery(response, "blankNodeSubjects", 
        "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o . FILTER(isBlank(?s)) }");
    runStatsQuery(response, "blankNodeObjects", 
        "SELECT (COUNT(*) AS ?count) WHERE { ?s ?p ?o . FILTER(isBlank(?o)) }");

    return response;
}

} // namespace cli_utils