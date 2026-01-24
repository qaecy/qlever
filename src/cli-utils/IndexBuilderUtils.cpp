#include "IndexBuilderUtils.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "QleverCliContext.h"
#include "index/DeltaTriples.h"
#include "index/Index.h"
#include "index/IndexImpl.h"
#include "index/Permutation.h"
#include "index/ScanSpecification.h"
#include "index/vocabulary/VocabularyType.h"
#include "libqlever/Qlever.h"
#include "util/AllocatorWithLimit.h"
#include "util/CancellationHandle.h"
#include "util/json.h"

using json = nlohmann::json;

namespace cli_utils {

/**
 * @brief Extract literals from specified predicates and save them to a words
 * file
 */
static void extractLiteralsFromPredicates(
    const std::string& baseName, const std::vector<std::string>& predicates,
    const std::string& wordsfile, const std::string& docsfile) {
  auto allocator = ad_utility::makeUnlimitedAllocator<Id>();
  Index index{allocator};
  index.usePatterns() = false;
  index.createFromOnDiskIndex(baseName, false);

  std::ofstream outWords(wordsfile);
  std::ofstream outDocs(docsfile);
  uint64_t contextId = 0;

  // Create a valid cancellation handle for the scan operations
  auto cancellationHandle =
      std::make_shared<ad_utility::CancellationHandle<>>();

  const auto& pso = index.getImpl().getPermutation(Permutation::Enum::PSO);

  for (auto predName : predicates) {
    VocabIndex predIdx;
    bool found = index.getVocab().getId(predName, &predIdx);

    // If not found and it looks like an IRI without brackets, try adding them
    if (!found && !predName.empty() && predName[0] != '<' &&
        predName.find(':') != std::string::npos) {
      std::string wrapped = "<" + predName + ">";
      found = index.getVocab().getId(wrapped, &predIdx);
      if (found) predName = wrapped;
    }

    if (!found) {
      std::cerr << "[QLever] Warning: Predicate '" << predName
                << "' not found in vocabulary. Skipping." << std::endl;
      continue;
    }

    std::cerr << "[QLever] Extracting literals for predicate: " << predName
              << std::endl;

    Id predId = Id::makeFromVocabIndex(predIdx);
    ScanSpecification scanSpec{predId, std::nullopt, std::nullopt};

    // Use the actual state from the index instead of a dummy one to avoid
    // "originalMetadata_.has_value()" assertion errors.
    auto state =
        index.deltaTriplesManager().getCurrentLocatedTriplesSharedState();
    auto scanSpecAndBlocks = pso.getScanSpecAndBlocks(scanSpec, *state);
    auto results = pso.scan(scanSpecAndBlocks, {}, cancellationHandle, *state);

    size_t count = 0;
    for (size_t i = 0; i < results.numRows(); ++i) {
      Id subjId = results(i, 0);
      Id objId = results(i, 1);

      // We only care about literals
      if (objId.getDatatype() == Datatype::VocabIndex ||
          objId.getDatatype() == Datatype::LocalVocabIndex) {
        auto literal =
            (objId.getDatatype() == Datatype::VocabIndex)
                ? index.getImpl().indexToString(objId.getVocabIndex())
                : "LOCAL_VOCAB_NOT_SUPPORTED_YET";  // Text indexer needs
                                                    // strings

        auto subject = index.getImpl().indexToString(subjId.getVocabIndex());

        // wordsfile format: word \t contextId \t score [\t isEntity]
        // docsfile format: contextId \t text
        // We strip quotes from literals if they exist
        std::string literalStr = std::string(literal);
        // Only strip quotes if the string starts and ends with a quote and is at least 2 chars
        if (literalStr.size() >= 2 && literalStr.front() == '"' && literalStr.back() == '"') {
          literalStr = literalStr.substr(1, literalStr.size() - 2);
        }

        // Skip empty literals
        if (literalStr.empty()) {
          continue;
        }

        // Strip angle brackets from subject IRI
        std::string subjectStr = std::string(subject);
        if (subjectStr.size() >= 2 && subjectStr.front() == '<' &&
            subjectStr.back() == '>') {
          subjectStr = subjectStr.substr(1, subjectStr.size() - 2);
        }

        // Skip if subject is empty after stripping
        if (subjectStr.empty()) {
          continue;
        }

        outWords << literalStr << "\t" << contextId << "\t1.0\n";
        outWords << subjectStr << "\t" << contextId << "\t1.0\t1\n";
        outDocs << contextId << "\t" << literalStr << "\n";
        contextId++;
        count++;
      }
    }
    std::cerr << "[QLever] Extracted " << count << " literals for " << predName
              << std::endl;
  }
}

json IndexBuilder::buildIndex(const json& jsonInput) {
  try {
    // Validate required fields
    if (!jsonInput.contains("input_files") ||
        !jsonInput["input_files"].is_array() ||
        jsonInput["input_files"].empty()) {
      return createErrorResponse("Missing or empty 'input_files' array");
    }

    if (!jsonInput.contains("index_name") ||
        !jsonInput["index_name"].is_string() ||
        jsonInput["index_name"].get<std::string>().empty()) {
      return createErrorResponse("Missing or invalid 'index_name' parameter");
    }

    std::string indexName = jsonInput["index_name"].get<std::string>();
    std::string indexDirectory = jsonInput.value("index_directory", ".");

    // Ensure directory exists
    if (!std::filesystem::exists(indexDirectory)) {
      try {
        std::filesystem::create_directories(indexDirectory);
      } catch (const std::filesystem::filesystem_error& e) {
        return createErrorResponse("Failed to create index directory: " +
                                   std::string(e.what()));
      }
    }

    // Create full path for index files
    std::filesystem::path indexPath =
        std::filesystem::path(indexDirectory) / indexName;
    std::string fullIndexPath = indexPath.string();

    // Create IndexBuilderConfig
    qlever::IndexBuilderConfig config;
    config.baseName_ = fullIndexPath;
    config.kbIndexName_ = indexName;

    // Process input files
    std::string inputFilesError =
        processInputFiles(jsonInput["input_files"], config);
    if (!inputFilesError.empty()) {
      return createErrorResponse(inputFilesError);
    }

    // Process optional parameters and collect text predicates
    std::vector<std::string> textPredicates;
    std::string optionalParamsError =
        processOptionalParameters(jsonInput, config, textPredicates);
    if (!optionalParamsError.empty()) {
      return createErrorResponse(optionalParamsError);
    }

    // If text_literals_predicates is set, extract literals and generate a wordsfile

    if (!textPredicates.empty()) {
      std::string wordsfilePath = fullIndexPath + ".predicates.wordsfile";
      std::string docsfilePath = fullIndexPath + ".predicates.docsfile";
      extractLiteralsFromPredicates(fullIndexPath, textPredicates, wordsfilePath, docsfilePath);

      // Clean wordsfile if requested
      bool cleanWordsfile = false;
      if (jsonInput.contains("clean_wordsfile") && jsonInput["clean_wordsfile"].is_boolean()) {
        cleanWordsfile = jsonInput["clean_wordsfile"].get<bool>();
      }
      if (cleanWordsfile) {
        std::string cleanedWordsfile = wordsfilePath + ".cleaned";
        std::ifstream in(wordsfilePath);
        std::ofstream out(cleanedWordsfile);
        std::string line;
        size_t valid = 0, invalid = 0;
        while (std::getline(in, line)) {
          // Clean logic: require at least 3 tab-separated fields
          size_t tab1 = line.find('\t');
          size_t tab2 = (tab1 != std::string::npos) ? line.find('\t', tab1 + 1) : std::string::npos;
          if (!line.empty() && tab1 != std::string::npos && tab2 != std::string::npos) {
            out << line << "\n";
            ++valid;
          } else {
            ++invalid;
          }
        }
        in.close();
        out.close();
        config.wordsfile_ = cleanedWordsfile;
        std::cerr << "[QLever] Wordsfile cleaned: " << valid << " valid, " << invalid << " invalid lines. Using cleaned file: " << cleanedWordsfile << std::endl;
      } else {
        config.wordsfile_ = wordsfilePath;
      }

      // Clean docsfile if requested
      bool cleanDocsfile = false;
      if (jsonInput.contains("clean_docsfile") && jsonInput["clean_docsfile"].is_boolean()) {
        cleanDocsfile = jsonInput["clean_docsfile"].get<bool>();
      }
      if (cleanDocsfile) {
        std::string cleanedDocsfile = docsfilePath + ".cleaned";
        std::ifstream in(docsfilePath);
        std::ofstream out(cleanedDocsfile);
        std::string line;
        size_t valid = 0, invalid = 0;
        while (std::getline(in, line)) {
          // Clean logic: skip empty lines and lines not containing a tab
          if (!line.empty() && line.find('\t') != std::string::npos) {
            out << line << "\n";
            ++valid;
          } else {
            ++invalid;
          }
        }
        in.close();
        out.close();
        config.docsfile_ = cleanedDocsfile;
        std::cerr << "[QLever] Docsfile cleaned: " << valid << " valid, " << invalid << " invalid lines. Using cleaned file: " << cleanedDocsfile << std::endl;
      } else {
        config.docsfile_ = docsfilePath;
      }
      config.addWordsFromLiterals_ = false; // Only use the extracted predicates
    }

    // Validate and build index
    config.validate();

    // Log the effective memory limit for index building
    if (config.memoryLimit_.has_value()) {
      std::cerr << "[QLever] Index build memory limit: "
                << config.memoryLimit_.value().getBytes() /
                       (1024 * 1024 * 1024.0)
                << " GB (" << config.memoryLimit_.value().getBytes()
                << " bytes)" << std::endl;
    } else {
      std::cerr << "[QLever] Index build memory limit: default (unspecified)"
                << std::endl;
    }

    auto startTime = std::chrono::steady_clock::now();
    qlever::QleverCliContext::buildIndex(config);
    auto endTime = std::chrono::steady_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);

    json response;
    response["success"] = true;
    response["indexName"] = indexName;
    response["indexDirectory"] = indexDirectory;
    response["fullIndexPath"] = fullIndexPath;
    response["numInputFiles"] = config.inputFiles_.size();
    response["buildTimeMs"] = duration.count();
    response["message"] = "Index built successfully";

    return response;

  } catch (const json::parse_error& e) {
    return createErrorResponse("Invalid JSON input: " + std::string(e.what()));
  } catch (const std::exception& e) {
    return createErrorResponse("Index building failed: " +
                               std::string(e.what()));
  }
}

std::string IndexBuilder::processInputFiles(
    const json& inputFiles, qlever::IndexBuilderConfig& config) {
  for (const auto& inputFile : inputFiles) {
    qlever::InputFileSpecification spec;
    spec.filetype_ = qlever::Filetype::Turtle;  // default

    if (inputFile.is_string()) {
      spec.filename_ = inputFile.get<std::string>();
    } else if (inputFile.is_object()) {
      if (!inputFile.contains("path") || !inputFile["path"].is_string()) {
        return "Input file object must contain 'path' string";
      }
      spec.filename_ = inputFile["path"].get<std::string>();

      // Optional format specification
      if (inputFile.contains("format") && inputFile["format"].is_string()) {
        std::string format = inputFile["format"].get<std::string>();
        if (format == "ttl" || format == "turtle") {
          spec.filetype_ = qlever::Filetype::Turtle;
        } else if (format == "nt") {
          spec.filetype_ = qlever::Filetype::Turtle;  // NT is parsed as Turtle
        } else if (format == "nq") {
          spec.filetype_ = qlever::Filetype::NQuad;
        } else {
          return "Unsupported format: " + format + ". Use 'ttl', 'nt', or 'nq'";
        }
      }

      if (inputFile.contains("default_graph") &&
          inputFile["default_graph"].is_string()) {
        spec.defaultGraph_ = inputFile["default_graph"].get<std::string>();
      }
    } else {
      return "Input file must be a string path or object with 'path' property";
    }

    // Allow '-' or '/dev/stdin' for stdin, skip file existence check in that
    // case
    if (spec.filename_ != "-" && spec.filename_ != "/dev/stdin") {
      if (!std::filesystem::exists(spec.filename_)) {
        return "Input file does not exist: " + spec.filename_;
      }
    }

    // If filepath is '-', leave as is. Downstream code should detect '-' and
    // use std::cin directly.

    config.inputFiles_.push_back(spec);
  }
  return "";  // Success
}

std::string IndexBuilder::processOptionalParameters(
    const json& input, qlever::IndexBuilderConfig& config,
    std::vector<std::string>& textPredicates) {
  // Memory limit
  if (input.contains("memory_limit_gb") &&
      input["memory_limit_gb"].is_number()) {
    double memoryLimitGb = input["memory_limit_gb"].get<double>();
    if (memoryLimitGb <= 0) {
      return "memory_limit_gb must be positive";
    }
    config.memoryLimit_ =
        ad_utility::MemorySize::gigabytes(static_cast<size_t>(memoryLimitGb));
  }

  // Settings file
  if (input.contains("settings_file") && input["settings_file"].is_string()) {
    std::string settingsFile = input["settings_file"].get<std::string>();
    if (!std::filesystem::exists(settingsFile)) {
      return "Settings file does not exist: " + settingsFile;
    }
    config.settingsFile_ = settingsFile;
  }

  // Keep temporary files
  if (input.contains("keep_temp_files") &&
      input["keep_temp_files"].is_boolean()) {
    config.keepTemporaryFiles_ = input["keep_temp_files"].get<bool>();
  }

  // Vocabulary type for GeoSPARQL support
  if (input.contains("vocabulary_type") &&
      input["vocabulary_type"].is_string()) {
    std::string vocabTypeStr = input["vocabulary_type"].get<std::string>();
    try {
      config.vocabType_ = ad_utility::VocabularyType::fromString(vocabTypeStr);
    } catch (const std::exception& e) {
      return "Invalid vocabulary_type: " + vocabTypeStr +
             ". Supported types: in-memory-uncompressed, on-disk-uncompressed, "
             "in-memory-compressed, on-disk-compressed, "
             "on-disk-compressed-geo-split";
    }
  }

  // Add words from literals (for text index)
  if (input.contains("add_words_from_literals") &&
      input["add_words_from_literals"].is_boolean()) {
    config.addWordsFromLiterals_ = input["add_words_from_literals"].get<bool>();
  }

  // Alternative way to specify text index (following Qleverfile syntax)
  if (input.contains("text_index") && input["text_index"].is_string()) {
    if (input["text_index"].get<std::string>() == "from_literals") {
      config.addWordsFromLiterals_ = true;
    }
  }

  // Predicates for text index from literals
  if (input.contains("text_literals_predicates") &&
      input["text_literals_predicates"].is_array()) {
    textPredicates.clear();
    for (const auto& predicate : input["text_literals_predicates"]) {
      if (predicate.is_string()) {
        textPredicates.push_back(predicate.get<std::string>());
      }
    }
  }

  // Prefixes for ID-encoded IRIs
  if (input.contains("prefixes_for_id_encoded_iris") &&
      input["prefixes_for_id_encoded_iris"].is_array()) {
    config.prefixesForIdEncodedIris_.clear();
    for (const auto& prefix : input["prefixes_for_id_encoded_iris"]) {
      if (!prefix.is_string()) {
        return "All entries in prefixes_for_id_encoded_iris must be strings";
      }
      config.prefixesForIdEncodedIris_.push_back(prefix.get<std::string>());
    }
  }

  return "";  // Success
}

json IndexBuilder::createErrorResponse(const std::string& message) {
  json response;
  response["success"] = false;
  response["error"] = message;
  return response;
}

}  // namespace cli_utils
