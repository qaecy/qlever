// Copyright 2025, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: QleverCLI contributors

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "util/MemorySize/MemorySize.h"

namespace cli_utils {

/// Result of parsing global CLI flags (e.g. --max-memory-in-gb) from argv.
struct ParsedCliArgs {
  /// If present, the user-specified memory limit in GB.
  std::optional<double> maxMemoryGb;
  /// The remaining arguments after global flags have been extracted.
  std::vector<std::string> remaining;
};

/// Scan `argv` for `--max-memory-in-gb <value>`, extract it, and return the
/// remaining arguments.  Throws `std::runtime_error` on missing or invalid
/// value (non-numeric, negative, or zero).
ParsedCliArgs parseGlobalFlags(int argc, char* argv[]);

/// Resolve the effective memory limit.  Precedence:
///   1. CLI override (`cliOverrideGb`)
///   2. Environment variable `QLEVER_MEMORY_LIMIT_GB`
///   3. `defaultGb`
ad_utility::MemorySize resolveMemoryLimit(
    std::optional<double> cliOverrideGb, double defaultGb = 4.0);

}  // namespace cli_utils
