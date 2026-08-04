#pragma once

#include <functional>
#include <string>
#include <utility>

namespace cli_utils {

/**
 * @brief Reassembles complete lines from a stream of arbitrarily-split chunks.
 *
 * The CLI streams query results rather than buffering them, and
 * `ExportQueryExecutionTrees::computeResult` yields ~1MB chunks whose boundaries
 * fall wherever its byte budget runs out — essentially never on a line boundary.
 * Any per-line post-processing of a streamed result therefore has to carry the
 * trailing partial line over into the next chunk. Handing that partial line to
 * the consumer instead produces a truncated triple, and for the N-Quads rewrite
 * a truncated triple with a graph term welded into the middle of it.
 *
 * Header-only and free of any engine dependency, so it can be unit-tested on its
 * own; the invariant worth testing is that the output does not depend on where
 * the chunk boundaries fall.
 */
class LineChunker {
 public:
  using LineSink = std::function<void(const std::string&)>;

  explicit LineChunker(LineSink onLine) : onLine_(std::move(onLine)) {}

  void feed(const std::string& chunk) {
    pending_ += chunk;
    std::size_t start = 0;
    for (std::size_t nl = pending_.find('\n', start); nl != std::string::npos;
         nl = pending_.find('\n', start)) {
      onLine_(pending_.substr(start, nl - start));
      start = nl + 1;
    }
    pending_.erase(0, start);
  }

  /// Flush a final line that has no newline after it. `std::getline` over a
  /// fully-buffered result returned that line too, so dropping it here would
  /// silently lose the last triple of any result without a trailing newline.
  void finish() {
    if (!pending_.empty()) {
      onLine_(pending_);
      pending_.clear();
    }
  }

 private:
  LineSink onLine_;
  std::string pending_;
};

/**
 * @brief True for a line that is a complete RDF statement.
 *
 * Only lines ending in '.' are emitted downstream; blank lines and Turtle prefix
 * declarations are dropped rather than passed through as invalid N-Triples. This
 * is deliberately the same (loose) test the buffered implementation used, so the
 * streamed output is byte-identical to what it produced.
 */
inline bool isCompleteTripleLine(const std::string& line) {
  return !line.empty() && line.back() == '.';
}

/**
 * @brief Rewrites an N-Triples line as an N-Quads line in the default graph.
 * Precondition: `isCompleteTripleLine(line)`.
 */
inline std::string toDefaultGraphQuad(std::string line) {
  line.pop_back();  // the '.' that `isCompleteTripleLine` guarantees
  return line + " <http://default.graph/> .\n";
}

}  // namespace cli_utils
