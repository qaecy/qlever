#pragma once

#include <chrono>
#include <fstream>
#include <memory>
#include <string>

#include "util/Timer.h"
#include "util/json.h"

// Forward declarations
namespace qlever {
class QleverCliContext;
}

namespace cli_utils {

/**
 * @brief GZIP output stream wrapper for compressed file writing
 */
class GzipOutputStream {
 private:
  void* gzFile_;  // gzFile type from zlib

 public:
  explicit GzipOutputStream(const std::string& filename);
  ~GzipOutputStream();

  void write(const std::string& data);
  void flush();
  bool isOpen() const;

  // Disable copy constructor and assignment
  GzipOutputStream(const GzipOutputStream&) = delete;
  GzipOutputStream& operator=(const GzipOutputStream&) = delete;
};

/**
 * @brief Progress tracker for RDF operations
 */
class ProgressTracker {
 private:
  std::chrono::steady_clock::time_point startTime_;
  std::chrono::steady_clock::time_point lastProgressTime_;
  std::chrono::seconds progressInterval_;
  size_t totalItems_;

 public:
  explicit ProgressTracker(
      std::chrono::seconds interval = std::chrono::seconds(5));

  void start();
  bool shouldLog() const;
  void logProgress(size_t currentItems, const std::string& itemType = "items");
  void updateLastProgressTime();

  double getItemsPerSecond(size_t currentItems) const;
  std::chrono::seconds getElapsedTime() const;
};

/**
 * @brief RDF output writer that handles different formats and compression
 */
class RdfOutputWriter {
 private:
  std::string format_;
  std::unique_ptr<std::ofstream> fileStream_;
  std::unique_ptr<GzipOutputStream> gzipStream_;
  std::ostream* outputStream_;
  bool useGzip_;

 public:
  RdfOutputWriter(const std::string& format,
                  const std::string& outputFile = "");
  ~RdfOutputWriter() = default;

  void writeTriple(const std::string& subject, const std::string& predicate,
                   const std::string& object);
  void writeQuad(const std::string& subject, const std::string& predicate,
                 const std::string& object, const std::string& graph);
  void writeRawTriple(const std::string& tripleString);
  void writePrefixes();
  void flush();

  bool isValid() const;
  const std::string& getFormat() const { return format_; }
  bool isUsingGzip() const { return useGzip_; }

  // Disable copy operations
  RdfOutputWriter(const RdfOutputWriter&) = delete;
  RdfOutputWriter& operator=(const RdfOutputWriter&) = delete;
};

/**
 * @brief Database serializer for large-scale RDF output with chunked processing
 */
class DatabaseSerializer {
 private:
  std::shared_ptr<qlever::QleverCliContext> qlever_;
  static constexpr size_t BATCH_SIZE = 500000;  // 500K triples per batch
  static constexpr auto PROGRESS_INTERVAL = std::chrono::seconds(5);

  std::string extractValue(const nlohmann::json& binding) const;

 public:
  explicit DatabaseSerializer(std::shared_ptr<qlever::QleverCliContext> qlever);

  void serialize(const std::string& format, const std::string& outputFile = "");
};

/**
 * @brief Utility functions for RDF format handling
 */
namespace RdfFormatUtils {
bool isValidFormat(const std::string& format);
std::string formatTriple(const std::string& subject,
                         const std::string& predicate,
                         const std::string& object, const std::string& format);
std::string formatQuad(const std::string& subject, const std::string& predicate,
                       const std::string& object, const std::string& graph,
                       const std::string& format);
std::string escapeForFormat(const std::string& value,
                            const std::string& format);
bool isGzipFile(const std::string& filename);
}  // namespace RdfFormatUtils

}  // namespace cli_utils
