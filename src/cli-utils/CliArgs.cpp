// Copyright 2025, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: QleverCLI contributors

#include "cli-utils/CliArgs.h"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace cli_utils {

// ____________________________________________________________________________
ParsedCliArgs parseGlobalFlags(int argc, char* argv[]) {
  ParsedCliArgs result;
  for (int i = 0; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--max-memory-in-gb") {
      if (i + 1 >= argc) {
        throw std::runtime_error(
            "--max-memory-in-gb requires a numeric value (e.g. "
            "--max-memory-in-gb 4)");
      }
      std::string valStr = argv[i + 1];
      double val;
      try {
        size_t pos = 0;
        val = std::stod(valStr, &pos);
        if (pos != valStr.size()) {
          throw std::invalid_argument("trailing characters");
        }
      } catch (const std::exception&) {
        throw std::runtime_error(
            "--max-memory-in-gb: invalid value '" + valStr +
            "'. Must be a positive number (e.g. 4 or 0.5).");
      }
      if (val <= 0.0) {
        throw std::runtime_error(
            "--max-memory-in-gb: value must be positive, got " + valStr);
      }
      result.maxMemoryGb = val;
      ++i;  // skip the value argument
    } else {
      result.remaining.push_back(arg);
    }
  }
  return result;
}

// ____________________________________________________________________________
ad_utility::MemorySize resolveMemoryLimit(std::optional<double> cliOverrideGb,
                                          double defaultGb) {
  auto toMemorySize = [](double gb) {
    return ad_utility::MemorySize::bytes(
        static_cast<size_t>(gb * 1024.0 * 1024.0 * 1024.0));
  };

  // 1. CLI flag takes highest precedence.
  if (cliOverrideGb.has_value()) {
    return toMemorySize(cliOverrideGb.value());
  }

  // 2. Environment variable.
  if (const char* env = std::getenv("QLEVER_MEMORY_LIMIT_GB")) {
    try {
      double gb = std::stod(env);
      if (gb > 0.0) {
        return toMemorySize(gb);
      }
    } catch (const std::exception&) {
      // Ignore malformed env var, fall through to default.
    }
  }

  // 3. Default.
  return toMemorySize(defaultGb);
}

}  // namespace cli_utils
