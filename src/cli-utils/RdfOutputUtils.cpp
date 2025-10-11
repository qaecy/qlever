#include "RdfOutputUtils.h"
#include <zlib.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>
#include "util/Log.h"
#include "util/http/MediaTypes.h"
#include "libqlever/Qlever.h"

namespace cli_utils {

// =============================================================================
// GzipOutputStream Implementation
// =============================================================================

GzipOutputStream::GzipOutputStream(const std::string& filename) {
    gzFile_ = gzopen(filename.c_str(), "wb");
    if (!gzFile_) {
        throw std::runtime_error("Failed to open gzip file: " + filename);
    }
}

GzipOutputStream::~GzipOutputStream() {
    if (gzFile_) {
        gzclose(static_cast<gzFile>(gzFile_));
    }
}

void GzipOutputStream::write(const std::string& data) {
    if (!gzFile_) {
        throw std::runtime_error("Gzip file not open");
    }
    int result = gzwrite(static_cast<gzFile>(gzFile_), data.c_str(), data.length());
    if (result <= 0) {
        throw std::runtime_error("Failed to write to gzip file");
    }
}

void GzipOutputStream::flush() {
    if (gzFile_) {
        gzflush(static_cast<gzFile>(gzFile_), Z_SYNC_FLUSH);
    }
}

bool GzipOutputStream::isOpen() const {
    return gzFile_ != nullptr;
}

// =============================================================================
// ProgressTracker Implementation
// =============================================================================

ProgressTracker::ProgressTracker(std::chrono::seconds interval) 
    : progressInterval_(interval), totalItems_(0) {}

void ProgressTracker::start() {
    startTime_ = std::chrono::steady_clock::now();
    lastProgressTime_ = startTime_;
    totalItems_ = 0;
}

bool ProgressTracker::shouldLog() const {
    auto currentTime = std::chrono::steady_clock::now();
    return currentTime - lastProgressTime_ >= progressInterval_;
}

void ProgressTracker::logProgress(size_t currentItems, const std::string& itemType) {
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        currentTime - startTime_).count();
    
    double itemsPerSecond = elapsedSeconds > 0 ? currentItems / static_cast<double>(elapsedSeconds) : 0;
    
    std::string eta = "";
    if (itemsPerSecond > 0) {
        double minutesElapsed = elapsedSeconds / 60.0;
        eta = " (" + std::to_string(static_cast<int>(minutesElapsed)) + "min elapsed)";
    }
    
    std::cerr << "Processed " << currentItems << " " << itemType 
              << " (" << static_cast<int>(itemsPerSecond) << "/sec)" << eta << std::endl;
    
    lastProgressTime_ = currentTime;
}

void ProgressTracker::updateLastProgressTime() {
    lastProgressTime_ = std::chrono::steady_clock::now();
}

double ProgressTracker::getItemsPerSecond(size_t currentItems) const {
    auto elapsedSeconds = getElapsedTime().count();
    return elapsedSeconds > 0 ? currentItems / static_cast<double>(elapsedSeconds) : 0;
}

std::chrono::seconds ProgressTracker::getElapsedTime() const {
    auto currentTime = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime_);
}

// =============================================================================
// RdfOutputWriter Implementation
// =============================================================================

RdfOutputWriter::RdfOutputWriter(const std::string& format, const std::string& outputFile) 
    : format_(format), outputStream_(nullptr), useGzip_(false) {
    
    if (!RdfFormatUtils::isValidFormat(format)) {
        throw std::invalid_argument("Invalid RDF format: " + format);
    }
    
    if (outputFile.empty()) {
        // Use stdout
        outputStream_ = &std::cout;
    } else {
        useGzip_ = RdfFormatUtils::isGzipFile(outputFile);
        
        if (useGzip_) {
            gzipStream_ = std::make_unique<GzipOutputStream>(outputFile);
        } else {
            fileStream_ = std::make_unique<std::ofstream>(outputFile);
            if (!fileStream_->is_open()) {
                throw std::runtime_error("Failed to open output file: " + outputFile);
            }
            outputStream_ = fileStream_.get();
        }
    }
}

void RdfOutputWriter::writeTriple(const std::string& subject, const std::string& predicate, const std::string& object) {
    std::string formattedTriple = RdfFormatUtils::formatTriple(subject, predicate, object, format_);
    writeRawTriple(formattedTriple);
}

void RdfOutputWriter::writeQuad(const std::string& subject, const std::string& predicate, 
                               const std::string& object, const std::string& graph) {
    std::string formattedQuad = RdfFormatUtils::formatQuad(subject, predicate, object, graph, format_);
    writeRawTriple(formattedQuad);
}

void RdfOutputWriter::writeRawTriple(const std::string& tripleString) {
    if (useGzip_ && gzipStream_) {
        gzipStream_->write(tripleString);
    } else if (outputStream_) {
        *outputStream_ << tripleString;
    }
}

void RdfOutputWriter::writePrefixes() {
    // No prefixes needed for NT and NQ formats
}

void RdfOutputWriter::flush() {
    if (useGzip_ && gzipStream_) {
        gzipStream_->flush();
    } else if (outputStream_) {
        outputStream_->flush();
    }
}

bool RdfOutputWriter::isValid() const {
    if (useGzip_) {
        return gzipStream_ && gzipStream_->isOpen();
    }
    return outputStream_ != nullptr;
}

// =============================================================================
// RdfFormatUtils Implementation
// =============================================================================

namespace RdfFormatUtils {

bool isValidFormat(const std::string& format) {
    return format == "nt" || format == "nq";
}

std::string formatTriple(const std::string& subject, const std::string& predicate, 
                        const std::string& object, const std::string& format) {
    // For nt and nq, use full URIs
    return subject + " " + predicate + " " + object + " .\n";
}

std::string formatQuad(const std::string& subject, const std::string& predicate, 
                      const std::string& object, const std::string& graph, const std::string& format) {
    if (format == "nq") {
        return subject + " " + predicate + " " + object + " " + graph + " .\n";
    }
    // For non-quad formats, just output the triple
    return formatTriple(subject, predicate, object, format);
}

std::string escapeForFormat(const std::string& value, const std::string& /* format */) {
    // For now, return as-is. Could add proper escaping later if needed.
    return value;
}

bool isGzipFile(const std::string& filename) {
    return filename.length() > 3 && filename.substr(filename.length() - 3) == ".gz";
}

} // namespace RdfFormatUtils

// =============================================================================
// DatabaseSerializer Implementation
// =============================================================================

DatabaseSerializer::DatabaseSerializer(std::shared_ptr<qlever::Qlever> qlever) 
    : qlever_(std::move(qlever)) {}

std::string DatabaseSerializer::extractValue(const nlohmann::json& binding) const {
    const auto& type = binding["type"];
    const auto& value = binding["value"];
    
    if (type == "uri") {
        return "<" + value.get<std::string>() + ">";
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

void DatabaseSerializer::serialize(const std::string& format, const std::string& outputFile) {
    // Validate format
    if (!RdfFormatUtils::isValidFormat(format)) {
        throw std::runtime_error("Invalid format: " + format + ". Supported formats: nt, nq");
    }
    
    // Setup output streams
    bool useGzip = !outputFile.empty() && RdfFormatUtils::isGzipFile(outputFile);
    std::unique_ptr<std::ofstream> fileStream;
    std::unique_ptr<GzipOutputStream> gzipStream;
    std::ostream* outputStream = &std::cout;
    
    if (!outputFile.empty()) {
        if (useGzip) {
            gzipStream = std::make_unique<GzipOutputStream>(outputFile);
        } else {
            fileStream = std::make_unique<std::ofstream>(outputFile);
            if (!fileStream->is_open()) {
                throw std::runtime_error("Cannot write to output file: " + outputFile);
            }
            outputStream = fileStream.get();
        }
    }
    
    // Lambda to write output
    auto writeOutput = [&](const std::string& data) {
        if (useGzip && gzipStream) {
            gzipStream->write(data);
        } else {
            *outputStream << data;
        }
    };
    
    // Lambda to flush output
    auto flushOutput = [&]() {
        if (useGzip && gzipStream) {
            gzipStream->flush();
        } else {
            outputStream->flush();
        }
    };
    
    // Start serialization with progress logging
    std::cerr << "Starting serialization to " << format << " format";
    if (!outputFile.empty()) {
        std::cerr << ", output: " << outputFile;
        if (useGzip) std::cerr << " (gzipped)";
    }
    std::cerr << std::endl;
    
    size_t offset = 0;
    size_t totalTriples = 0;
    auto startTime = std::chrono::steady_clock::now();
    auto lastProgressTime = startTime;
    
    // Pre-allocate string buffer for better performance
    std::string batchBuffer;
    batchBuffer.reserve(BATCH_SIZE * 200); // Estimate ~200 chars per triple
    
    // Setup logging suppression for QLever's verbose query messages
    std::ofstream nullStream("/dev/null");
    std::streambuf* originalCerrBuf = std::cerr.rdbuf();
    
    while (true) {
        // Construct batch query with large LIMIT and OFFSET
        std::string sparqlQuery;
        if (format == "nq") {
            sparqlQuery = "SELECT ?s ?p ?o ?g WHERE { GRAPH ?g { ?s ?p ?o } } LIMIT " + 
                         std::to_string(BATCH_SIZE) + " OFFSET " + std::to_string(offset);
        } else {
            sparqlQuery = "SELECT ?s ?p ?o WHERE { ?s ?p ?o } LIMIT " + 
                         std::to_string(BATCH_SIZE) + " OFFSET " + std::to_string(offset);
        }
        
        // Temporarily redirect QLever's logging to suppress verbose messages
        std::cerr.rdbuf(nullStream.rdbuf());
        
        // Execute batch query
        std::string result = qlever_->query(sparqlQuery, ad_utility::MediaType::sparqlJson);
        
        // Restore original logging
        std::cerr.rdbuf(originalCerrBuf);
        
        // Parse the JSON result
        nlohmann::json queryResult = nlohmann::json::parse(result);
        
        if (!queryResult.contains("results") || !queryResult["results"].contains("bindings")) {
            break; // No more results
        }
        
        auto bindings = queryResult["results"]["bindings"];
        if (bindings.empty()) {
            break; // No more results
        }
        
        // Process and write this batch efficiently
        batchBuffer.clear();
        
        for (const auto& binding : bindings) {
            std::string subject = extractValue(binding["s"]);
            std::string predicate = extractValue(binding["p"]);
            std::string object = extractValue(binding["o"]);
            
            if (format == "nt") {
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
        
        // Progress logging every 5 seconds with detailed metrics
        auto currentTime = std::chrono::steady_clock::now();
        if (currentTime - lastProgressTime >= PROGRESS_INTERVAL) {
            auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                currentTime - startTime).count();
            double triplesPerSecond = elapsedSeconds > 0 ? totalTriples / static_cast<double>(elapsedSeconds) : 0;
            
            std::cerr << "Processed " << totalTriples << " triples (" 
                      << static_cast<int>(triplesPerSecond) << "/sec) " 
                      << "(" << (elapsedSeconds / 60) << "min elapsed)" << std::endl;
            lastProgressTime = currentTime;
        }
        
        // If we got fewer results than batch size, we're done
        if (bindings.size() < BATCH_SIZE) {
            break;
        }
        
        offset += BATCH_SIZE;
    }
    
    // Final statistics
    auto endTime = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cerr << "Serialization complete. Total triples: " << totalTriples 
              << ", Time: " << totalDuration.count() << "ms" << std::endl;
}

} // namespace cli_utils