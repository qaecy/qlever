#include "IndexBuilderUtils.h"
#include "../libqlever/Qlever.h"
#include "../index/vocabulary/VocabularyType.h"
#include <filesystem>
#include <chrono>

using json = nlohmann::json;

namespace cli_utils {

json IndexBuilder::buildIndex(const json& jsonInput) {
    try {
        // Validate required fields
        if (!jsonInput.contains("input_files") || !jsonInput["input_files"].is_array() || 
            jsonInput["input_files"].empty()) {
            return createErrorResponse("Missing or empty 'input_files' array");
        }

        if (!jsonInput.contains("index_name") || !jsonInput["index_name"].is_string() || 
            jsonInput["index_name"].get<std::string>().empty()) {
            return createErrorResponse("Missing or invalid 'index_name' parameter");
        }

        std::string indexName = jsonInput["index_name"];
        std::string indexDirectory = jsonInput.value("index_directory", ".");
        
        // Ensure directory exists
        if (!std::filesystem::exists(indexDirectory)) {
            try {
                std::filesystem::create_directories(indexDirectory);
            } catch (const std::filesystem::filesystem_error& e) {
                return createErrorResponse("Failed to create index directory: " + std::string(e.what()));
            }
        }
        
        // Create full path for index files
        std::filesystem::path indexPath = std::filesystem::path(indexDirectory) / indexName;
        std::string fullIndexPath = indexPath.string();
        
        // Create IndexBuilderConfig
        qlever::IndexBuilderConfig config;
        config.baseName_ = fullIndexPath;
        config.kbIndexName_ = indexName;

        // Process input files
        std::string inputFilesError = processInputFiles(jsonInput["input_files"], config);
        if (!inputFilesError.empty()) {
            return createErrorResponse(inputFilesError);
        }

        // Process optional parameters
        std::string optionalParamsError = processOptionalParameters(jsonInput, config);
        if (!optionalParamsError.empty()) {
            return createErrorResponse(optionalParamsError);
        }

        // Validate and build index
        config.validate();
        
        auto startTime = std::chrono::steady_clock::now();
        qlever::Qlever::buildIndex(config);
        auto endTime = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

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
        return createErrorResponse("Index building failed: " + std::string(e.what()));
    }
}

std::string IndexBuilder::processInputFiles(const json& inputFiles, qlever::IndexBuilderConfig& config) {
    for (const auto& inputFile : inputFiles) {
        std::string filepath;
        qlever::Filetype filetype = qlever::Filetype::Turtle; // default

        if (inputFile.is_string()) {
            filepath = inputFile.get<std::string>();
        } else if (inputFile.is_object()) {
            if (!inputFile.contains("path") || !inputFile["path"].is_string()) {
                return "Input file object must contain 'path' string";
            }
            filepath = inputFile["path"];

            // Optional format specification
            if (inputFile.contains("format") && inputFile["format"].is_string()) {
                std::string format = inputFile["format"];
                if (format == "ttl" || format == "turtle") {
                    filetype = qlever::Filetype::Turtle;
                } else if (format == "nt") {
                    filetype = qlever::Filetype::Turtle; // NT is parsed as Turtle
                } else if (format == "nq") {
                    filetype = qlever::Filetype::NQuad;
                } else {
                    return "Unsupported format: " + format + ". Use 'nt' or 'nq'";
                }
            }
        } else {
            return "Input file must be a string path or object with 'path' property";
        }

        // Allow '-' or '/dev/stdin' for stdin, skip file existence check in that case
        if (filepath != "-" && filepath != "/dev/stdin") {
            if (!std::filesystem::exists(filepath)) {
                return "Input file does not exist: " + filepath;
            }
        }

        // For compatibility with IndexBuilderMain, map '-' to '/dev/stdin'
        if (filepath == "-") {
            filepath = "/dev/stdin";
        }

        config.inputFiles_.emplace_back(filepath, filetype);
    }
    return ""; // Success
}

std::string IndexBuilder::processOptionalParameters(const json& input, qlever::IndexBuilderConfig& config) {
    // Memory limit
    if (input.contains("memory_limit_gb") && input["memory_limit_gb"].is_number()) {
        double memoryLimitGb = input["memory_limit_gb"];
        if (memoryLimitGb <= 0) {
            return "memory_limit_gb must be positive";
        }
        config.memoryLimit_ = ad_utility::MemorySize::gigabytes(static_cast<size_t>(memoryLimitGb));
    }

    // Settings file
    if (input.contains("settings_file") && input["settings_file"].is_string()) {
        std::string settingsFile = input["settings_file"];
        if (!std::filesystem::exists(settingsFile)) {
            return "Settings file does not exist: " + settingsFile;
        }
        config.settingsFile_ = settingsFile;
    }

    // Keep temporary files
    if (input.contains("keep_temp_files") && input["keep_temp_files"].is_boolean()) {
        config.keepTemporaryFiles_ = input["keep_temp_files"];
    }

    // Vocabulary type for GeoSPARQL support
    if (input.contains("vocabulary_type") && input["vocabulary_type"].is_string()) {
        std::string vocabTypeStr = input["vocabulary_type"];
        try {
            config.vocabType_ = ad_utility::VocabularyType::fromString(vocabTypeStr);
        } catch (const std::exception& e) {
            return "Invalid vocabulary_type: " + vocabTypeStr + 
                ". Supported types: in-memory-uncompressed, on-disk-uncompressed, in-memory-compressed, on-disk-compressed, on-disk-compressed-geo-split";
        }
    }

    // Add words from literals (for text index)
    if (input.contains("add_words_from_literals") && input["add_words_from_literals"].is_boolean()) {
        config.addWordsFromLiterals_ = input["add_words_from_literals"];
    }

    return ""; // Success
}

json IndexBuilder::createErrorResponse(const std::string& message) {
    json response;
    response["success"] = false;
    response["error"] = message;
    return response;
}

} // namespace cli_utils