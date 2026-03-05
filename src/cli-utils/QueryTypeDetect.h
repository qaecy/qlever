#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace cli_utils {

// Detect the SPARQL query verb (SELECT, CONSTRUCT, DESCRIBE, ASK, etc.)
// by skipping PREFIX declarations, BASE declarations, and comment lines
// (lines starting with '#').  Returns the uppercased verb, or "" if the
// query is empty / whitespace-only.
inline std::string detectQueryType(const std::string& query) {
  std::istringstream stream(query);
  std::string line;
  while (std::getline(stream, line)) {
    // Trim leading whitespace.
    auto start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos) continue;  // blank line
    line = line.substr(start);

    // Skip comment lines.
    if (line[0] == '#') continue;

    // Extract first word and uppercase it.
    auto end = line.find_first_of(" \t\r\n");
    std::string word = line.substr(0, end);
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Skip PREFIX and BASE declarations.
    if (word == "PREFIX" || word == "BASE") continue;

    return word;
  }
  return "";
}

}  // namespace cli_utils
