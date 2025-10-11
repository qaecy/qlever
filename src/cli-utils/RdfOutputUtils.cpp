#include "RdfOutputUtils.h"
#include <zlib.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include "util/Log.h"

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

} // namespace cli_utils