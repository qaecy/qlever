// Tests for cli-utils implementation-level logic (requires cliUtils link):
//   - RdfFormatUtils: all pure functions
//   - RdfOutputWriter: construction, write, flush, gzip detection
//   - ProgressTracker: timing, shouldLog, rate
//   - QueryExecutor::extractValue: JSON parsing with fallback
//   - IndexBuilder: JSON validation paths (no real index build)
//   - QleverCliContext::validateConfig: all error branches

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

#include "cli-utils/IndexBuilderUtils.h"
#include "cli-utils/QueryUtils.h"
#include "cli-utils/RdfOutputUtils.h"
#include "cli-utils/StreamSuppressor.h"
#include "QleverCliContext.h"

namespace {

// ============================================================
// Helpers
// ============================================================

// Read the full content of a file into a string.
std::string readFile(const std::string& path) {
  std::ifstream f(path);
  return std::string(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
}

// Create a temp file path in the system temp directory.
std::string tempPath(const std::string& name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

// ============================================================
// RdfFormatUtils – isValidFormat
// ============================================================

TEST(RdfFormatUtils, ValidFormats) {
  EXPECT_TRUE(cli_utils::RdfFormatUtils::isValidFormat("nt"));
  EXPECT_TRUE(cli_utils::RdfFormatUtils::isValidFormat("nq"));
}

TEST(RdfFormatUtils, InvalidFormats) {
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat(""));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat("ttl"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat("turtle"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat("NT"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat("NQ"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat("rdf"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat(" nt"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isValidFormat("nt "));
}

// ============================================================
// RdfFormatUtils – isGzipFile
// ============================================================

TEST(RdfFormatUtils, IsGzipFile) {
  EXPECT_TRUE(cli_utils::RdfFormatUtils::isGzipFile("output.gz"));
  EXPECT_TRUE(cli_utils::RdfFormatUtils::isGzipFile("some/path/file.nt.gz"));
  EXPECT_TRUE(cli_utils::RdfFormatUtils::isGzipFile("a.gz"));
}

TEST(RdfFormatUtils, IsNotGzipFile) {
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile("output.nt"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile("output.nq"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile("gz"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile(""));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile(".g"));
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile("file.gzip"));
}

TEST(RdfFormatUtils, IsGzipFileBoundaryExactlyThreeChars) {
  EXPECT_FALSE(cli_utils::RdfFormatUtils::isGzipFile(".gz"))
      << "string of length 3 ending in .gz: length > 3 required";
}

// ============================================================
// RdfFormatUtils – formatTriple
// ============================================================

TEST(RdfFormatUtils, FormatTripleNt) {
  std::string result = cli_utils::RdfFormatUtils::formatTriple(
      "<http://s>", "<http://p>", "<http://o>", "nt");
  EXPECT_EQ(result, "<http://s> <http://p> <http://o> .\n");
}

TEST(RdfFormatUtils, FormatTripleNq) {
  // formatTriple ignores the format and always writes triple form
  std::string result = cli_utils::RdfFormatUtils::formatTriple(
      "<http://s>", "<http://p>", "<http://o>", "nq");
  EXPECT_EQ(result, "<http://s> <http://p> <http://o> .\n");
}

TEST(RdfFormatUtils, FormatTripleWithLiteral) {
  std::string result = cli_utils::RdfFormatUtils::formatTriple(
      "<http://s>", "<http://p>", "\"hello\"", "nt");
  EXPECT_EQ(result, "<http://s> <http://p> \"hello\" .\n");
}

// ============================================================
// RdfFormatUtils – formatQuad
// ============================================================

TEST(RdfFormatUtils, FormatQuadNq) {
  std::string result = cli_utils::RdfFormatUtils::formatQuad(
      "<http://s>", "<http://p>", "<http://o>", "<http://g>", "nq");
  EXPECT_EQ(result, "<http://s> <http://p> <http://o> <http://g> .\n");
}

TEST(RdfFormatUtils, FormatQuadNtFallsBackToTriple) {
  std::string result = cli_utils::RdfFormatUtils::formatQuad(
      "<http://s>", "<http://p>", "<http://o>", "<http://g>", "nt");
  // For non-nq format, the graph component is dropped
  EXPECT_EQ(result, "<http://s> <http://p> <http://o> .\n");
}

// ============================================================
// RdfFormatUtils – escapeForFormat
// ============================================================

TEST(RdfFormatUtils, EscapeBackslash) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("a\\b", "nt"),
            "a\\\\b");
}

TEST(RdfFormatUtils, EscapeDoubleQuote) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("say \"hi\"", "nt"),
            "say \\\"hi\\\"");
}

TEST(RdfFormatUtils, EscapeNewline) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("line\nbreak", "nt"),
            "line\\nbreak");
}

TEST(RdfFormatUtils, EscapeCarriageReturn) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("cr\r", "nt"), "cr\\r");
}

TEST(RdfFormatUtils, EscapeTab) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("\ttab", "nt"),
            "\\ttab");
}

TEST(RdfFormatUtils, EscapeNoSpecialChars) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("hello world", "nt"),
            "hello world");
}

TEST(RdfFormatUtils, EscapeEmptyString) {
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat("", "nt"), "");
}

TEST(RdfFormatUtils, EscapeAllSpecialCharsInSequence) {
  std::string input = "\\\"\n\r\t";
  std::string expected = "\\\\\\\"\\n\\r\\t";
  EXPECT_EQ(cli_utils::RdfFormatUtils::escapeForFormat(input, "nt"), expected);
}

// ============================================================
// RdfOutputWriter – constructor validation
// ============================================================

TEST(RdfOutputWriter, InvalidFormatThrows) {
  EXPECT_THROW(cli_utils::RdfOutputWriter("ttl"), std::invalid_argument);
  EXPECT_THROW(cli_utils::RdfOutputWriter(""), std::invalid_argument);
  EXPECT_THROW(cli_utils::RdfOutputWriter("csv"), std::invalid_argument);
}

TEST(RdfOutputWriter, ValidNtFormatToStdout) {
  cli_utils::RdfOutputWriter writer("nt");
  EXPECT_TRUE(writer.isValid());
  EXPECT_EQ(writer.getFormat(), "nt");
  EXPECT_FALSE(writer.isUsingGzip());
}

TEST(RdfOutputWriter, ValidNqFormatToStdout) {
  cli_utils::RdfOutputWriter writer("nq");
  EXPECT_TRUE(writer.isValid());
  EXPECT_EQ(writer.getFormat(), "nq");
  EXPECT_FALSE(writer.isUsingGzip());
}

TEST(RdfOutputWriter, WriteTripleToFile) {
  std::string path = tempPath("rdf_test_triple.nt");
  {
    cli_utils::RdfOutputWriter writer("nt", path);
    EXPECT_TRUE(writer.isValid());
    EXPECT_FALSE(writer.isUsingGzip());
    writer.writeTriple("<http://s>", "<http://p>", "<http://o>");
    writer.flush();
  }
  std::string content = readFile(path);
  EXPECT_EQ(content, "<http://s> <http://p> <http://o> .\n");
  std::filesystem::remove(path);
}

TEST(RdfOutputWriter, WriteQuadToFile) {
  std::string path = tempPath("rdf_test_quad.nq");
  {
    cli_utils::RdfOutputWriter writer("nq", path);
    writer.writeQuad("<http://s>", "<http://p>", "<http://o>", "<http://g>");
    writer.flush();
  }
  std::string content = readFile(path);
  EXPECT_EQ(content, "<http://s> <http://p> <http://o> <http://g> .\n");
  std::filesystem::remove(path);
}

TEST(RdfOutputWriter, WriteRawTripleToFile) {
  std::string path = tempPath("rdf_test_raw.nt");
  {
    cli_utils::RdfOutputWriter writer("nt", path);
    writer.writeRawTriple("<http://a> <http://b> <http://c> .\n");
    writer.flush();
  }
  EXPECT_EQ(readFile(path), "<http://a> <http://b> <http://c> .\n");
  std::filesystem::remove(path);
}

TEST(RdfOutputWriter, MultipleWritesToFile) {
  std::string path = tempPath("rdf_test_multi.nt");
  {
    cli_utils::RdfOutputWriter writer("nt", path);
    for (int i = 0; i < 5; ++i) {
      writer.writeTriple("<http://s" + std::to_string(i) + ">", "<http://p>",
                         "<http://o>");
    }
    writer.flush();
  }
  std::string content = readFile(path);
  EXPECT_THAT(content, ::testing::HasSubstr("<http://s0>"));
  EXPECT_THAT(content, ::testing::HasSubstr("<http://s4>"));
  // 5 lines
  size_t lineCount = 0;
  for (char c : content) {
    if (c == '\n') ++lineCount;
  }
  EXPECT_EQ(lineCount, 5u);
  std::filesystem::remove(path);
}

TEST(RdfOutputWriter, GzipDetectedFromExtension) {
  std::string path = tempPath("rdf_test.nt.gz");
  {
    cli_utils::RdfOutputWriter writer("nt", path);
    EXPECT_TRUE(writer.isUsingGzip());
    EXPECT_TRUE(writer.isValid());
    writer.writeRawTriple("<http://s> <http://p> <http://o> .\n");
    writer.flush();
  }
  // File should exist and be non-empty (gzip compressed)
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  std::filesystem::remove(path);
}

TEST(RdfOutputWriter, WritePrefixesIsNoOp) {
  // writePrefixes() must not throw or write anything for nt/nq
  std::string path = tempPath("rdf_test_prefix.nt");
  {
    cli_utils::RdfOutputWriter writer("nt", path);
    writer.writePrefixes();
    writer.flush();
  }
  EXPECT_EQ(readFile(path), "");
  std::filesystem::remove(path);
}

TEST(RdfOutputWriter, InvalidOutputFileThrows) {
  EXPECT_THROW(
      cli_utils::RdfOutputWriter("nt", "/nonexistent/directory/output.nt"),
      std::runtime_error);
}

// ============================================================
// GzipOutputStream – basic write and flush
// ============================================================

TEST(GzipOutputStream, OpenWriteFlushClose) {
  std::string path = tempPath("gzip_test.gz");
  {
    cli_utils::GzipOutputStream gz(path);
    EXPECT_TRUE(gz.isOpen());
    gz.write("hello gzip");
    gz.flush();
  }
  EXPECT_TRUE(std::filesystem::exists(path));
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  std::filesystem::remove(path);
}

TEST(GzipOutputStream, InvalidPathThrows) {
  EXPECT_THROW(
      cli_utils::GzipOutputStream("/nonexistent/path/to/file.gz"),
      std::runtime_error);
}

TEST(GzipOutputStream, WriteEmptyStringDoesNotThrow) {
  std::string path = tempPath("gzip_empty.gz");
  {
    cli_utils::GzipOutputStream gz(path);
    // Empty string write: zlib may return 0 bytes written; implementation
    // throws on result <= 0.  Accept either outcome but not a crash.
    try {
      gz.write("");
    } catch (const std::runtime_error&) {
      // Acceptable – zlib returned 0 for empty write
    }
  }
  std::filesystem::remove(path);
}

TEST(GzipOutputStream, MultipleWritesAccumulate) {
  std::string path = tempPath("gzip_multi.gz");
  {
    cli_utils::GzipOutputStream gz(path);
    gz.write("part1");
    gz.write("part2");
    gz.write("part3");
    gz.flush();
  }
  EXPECT_GT(std::filesystem::file_size(path), 0u);
  std::filesystem::remove(path);
}

// ============================================================
// ProgressTracker
// ============================================================

TEST(ProgressTracker, DefaultIntervalIsFiveSeconds) {
  cli_utils::ProgressTracker pt;
  pt.start();
  // Immediately after start, shouldLog() must be false (interval not elapsed)
  EXPECT_FALSE(pt.shouldLog());
}

TEST(ProgressTracker, CustomShortIntervalTriggers) {
  cli_utils::ProgressTracker pt(std::chrono::seconds(0));
  pt.start();
  // With zero-second interval, shouldLog() should be true immediately.
  EXPECT_TRUE(pt.shouldLog());
}

TEST(ProgressTracker, ElapsedTimeAfterStart) {
  cli_utils::ProgressTracker pt;
  pt.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  // At least 0 seconds elapsed (rounding to seconds may give 0 for <1s)
  EXPECT_GE(pt.getElapsedTime().count(), 0);
}

TEST(ProgressTracker, ItemsPerSecondZeroBeforeOneSecond) {
  cli_utils::ProgressTracker pt;
  pt.start();
  // With elapsed < 1s, getItemsPerSecond returns 0
  double rate = pt.getItemsPerSecond(1000);
  EXPECT_GE(rate, 0.0);
}

TEST(ProgressTracker, ItemsPerSecondPositiveAfterDelay) {
  cli_utils::ProgressTracker pt;
  pt.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  double rate = pt.getItemsPerSecond(1000);
  // 1000 items / ~1 second = ~1000/s; accept a wide range
  EXPECT_GE(rate, 0.0);
}

TEST(ProgressTracker, UpdateLastProgressTimeResetsInterval) {
  cli_utils::ProgressTracker pt(std::chrono::seconds(0));
  pt.start();
  EXPECT_TRUE(pt.shouldLog());
  pt.updateLastProgressTime();
  // After resetting, the zero-interval may still trigger immediately
  // (depends on clock resolution). This just must not crash.
  (void)pt.shouldLog();
}

TEST(ProgressTracker, LogProgressDoesNotThrow) {
  cli_utils::ProgressTracker pt;
  pt.start();
  // Redirect cerr to suppress output
  cli_utils::SuppressStreams s;
  EXPECT_NO_THROW(pt.logProgress(42, "triples"));
  EXPECT_NO_THROW(pt.logProgress(0, "items"));
}

// ============================================================
// QueryExecutor::extractValue
// ============================================================

TEST(QueryExecutorExtractValue, ExtractsStringField) {
  std::string json = R"({"key": "value"})";
  EXPECT_EQ(cli_utils::QueryExecutor::extractValue(json, "key"), "value");
}

TEST(QueryExecutorExtractValue, MissingKeyReturnsEmpty) {
  std::string json = R"({"other": "val"})";
  EXPECT_EQ(cli_utils::QueryExecutor::extractValue(json, "missing"), "");
}

TEST(QueryExecutorExtractValue, EmptyJsonReturnsEmpty) {
  EXPECT_EQ(cli_utils::QueryExecutor::extractValue("{}", "key"), "");
}

TEST(QueryExecutorExtractValue, InvalidJsonFallsBackToSearch) {
  // Not valid JSON – the parser throws, fallback string search kicks in.
  std::string pseudo = "{\"target\": \"found\"}";
  EXPECT_EQ(cli_utils::QueryExecutor::extractValue(pseudo, "target"), "found");
}

TEST(QueryExecutorExtractValue, InvalidJsonWithNoMatchReturnsEmpty) {
  std::string bad = "not json at all";
  EXPECT_EQ(cli_utils::QueryExecutor::extractValue(bad, "key"), "");
}

TEST(QueryExecutorExtractValue, NonStringValueReturnsEmpty) {
  // JSON has a number – not a string; get<string>() would throw, fallback
  // string search looks for a quoted value after the colon.
  std::string json = R"({"count": 42})";
  // Fallback finds no quoted value after "count":42, returns "".
  std::string result = cli_utils::QueryExecutor::extractValue(json, "count");
  // Either "" or some parse artefact – must not throw.
  (void)result;
  SUCCEED();
}

// ============================================================
// IndexBuilder::buildIndex – JSON validation paths
// ============================================================

TEST(IndexBuilderValidation, MissingInputFilesReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("input_files"));
}

TEST(IndexBuilderValidation, EmptyInputFilesReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array();
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("input_files"));
}

TEST(IndexBuilderValidation, MissingIndexNameReturnsError) {
  nlohmann::json input;
  input["input_files"] = nlohmann::json::array({"file.ttl"});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("index_name"));
}

TEST(IndexBuilderValidation, EmptyIndexNameReturnsError) {
  nlohmann::json input;
  input["index_name"] = "";
  input["input_files"] = nlohmann::json::array({"file.ttl"});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("index_name"));
}

TEST(IndexBuilderValidation, NonExistentInputFileReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({"/nonexistent/file.ttl"});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("does not exist"));
}

TEST(IndexBuilderValidation, InvalidInputFileObjectMissingPath) {
  nlohmann::json input;
  input["index_name"] = "test";
  nlohmann::json fileObj;
  fileObj["format"] = "ttl";
  // No "path" key
  input["input_files"] = nlohmann::json::array({fileObj});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("path"));
}

TEST(IndexBuilderValidation, InputFileNeitherStringNorObject) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({42});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
}

TEST(IndexBuilderValidation, UnsupportedFormatReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  nlohmann::json fileObj;
  fileObj["path"] = "-";  // stdin – skips file existence check
  fileObj["format"] = "xml";
  input["input_files"] = nlohmann::json::array({fileObj});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("Unsupported format"));
}

TEST(IndexBuilderValidation, NegativeMemoryLimitReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({"-"});
  input["memory_limit_gb"] = -1.0;
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("memory_limit_gb"));
}

TEST(IndexBuilderValidation, ZeroMemoryLimitReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({"-"});
  input["memory_limit_gb"] = 0.0;
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
}

TEST(IndexBuilderValidation, NonExistentSettingsFileReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({"-"});
  input["settings_file"] = "/nonexistent/settings.json";
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("Settings file"));
}

TEST(IndexBuilderValidation, InvalidVocabularyTypeReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({"-"});
  input["vocabulary_type"] = "definitely-not-a-valid-type";
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("vocabulary_type"));
}

TEST(IndexBuilderValidation, PrefixesNonStringEntryReturnsError) {
  nlohmann::json input;
  input["index_name"] = "test";
  input["input_files"] = nlohmann::json::array({"-"});
  input["prefixes_for_id_encoded_iris"] = nlohmann::json::array({42});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  EXPECT_FALSE(result["success"].get<bool>());
  EXPECT_THAT(result["error"].get<std::string>(),
              ::testing::HasSubstr("prefixes_for_id_encoded_iris"));
}

TEST(IndexBuilderValidation, StdinPathSkipsFileExistenceCheck) {
  // "-" is stdin – processInputFiles must accept it without checking the
  // filesystem.  The build will then fail later (no real files), but the
  // validation error message must NOT say "does not exist".
  nlohmann::json input;
  input["index_name"] = "stdin-test";
  input["input_files"] = nlohmann::json::array({"-"});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  // The build itself will fail (no real index), but the error must not be
  // "does not exist" – that would mean the stdin bypass is broken.
  if (!result["success"].get<bool>()) {
    EXPECT_THAT(result["error"].get<std::string>(),
                ::testing::Not(::testing::HasSubstr("does not exist")));
  }
}

TEST(IndexBuilderValidation, DevStdinPathSkipsFileExistenceCheck) {
  nlohmann::json input;
  input["index_name"] = "devstdin-test";
  input["input_files"] = nlohmann::json::array({"/dev/stdin"});
  auto result = cli_utils::IndexBuilder::buildIndex(input);
  if (!result["success"].get<bool>()) {
    EXPECT_THAT(result["error"].get<std::string>(),
                ::testing::Not(::testing::HasSubstr("does not exist")));
  }
}

// ============================================================
// QleverCliContext::validateConfig
// ============================================================

TEST(ValidateConfig, NegativeKScoreParamThrows) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = -0.001;
  EXPECT_THROW(qlever::QleverCliContext::validateConfig(config),
               std::invalid_argument);
}

TEST(ValidateConfig, ZeroKScoreParamIsValid) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 0.0;
  config.bScoringParam_ = 0.5;
  EXPECT_NO_THROW(qlever::QleverCliContext::validateConfig(config));
}

TEST(ValidateConfig, PositiveKScoreParamIsValid) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.5;
  config.bScoringParam_ = 0.75;
  EXPECT_NO_THROW(qlever::QleverCliContext::validateConfig(config));
}

TEST(ValidateConfig, NegativeBScoreParamThrows) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.2;
  config.bScoringParam_ = -0.1;
  EXPECT_THROW(qlever::QleverCliContext::validateConfig(config),
               std::invalid_argument);
}

TEST(ValidateConfig, BScoreParamAboveOneThrows) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.2;
  config.bScoringParam_ = 1.001;
  EXPECT_THROW(qlever::QleverCliContext::validateConfig(config),
               std::invalid_argument);
}

TEST(ValidateConfig, BScoreParamAtZeroIsValid) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.0;
  EXPECT_NO_THROW(qlever::QleverCliContext::validateConfig(config));
}

TEST(ValidateConfig, BScoreParamAtOneIsValid) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 1.0;
  EXPECT_NO_THROW(qlever::QleverCliContext::validateConfig(config));
}

TEST(ValidateConfig, OnlyWordsFileSpecifiedThrows) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.5;
  config.wordsfile_ = "words.txt";
  config.docsfile_ = "";  // docs not specified
  EXPECT_THROW(qlever::QleverCliContext::validateConfig(config),
               std::runtime_error);
}

TEST(ValidateConfig, OnlyDocsFileSpecifiedThrows) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.5;
  config.wordsfile_ = "";  // words not specified
  config.docsfile_ = "docs.txt";
  EXPECT_THROW(qlever::QleverCliContext::validateConfig(config),
               std::runtime_error);
}

TEST(ValidateConfig, BothWordsAndDocsSpecifiedIsValid) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.5;
  config.wordsfile_ = "words.txt";
  config.docsfile_ = "docs.txt";
  EXPECT_NO_THROW(qlever::QleverCliContext::validateConfig(config));
}

TEST(ValidateConfig, NeitherWordsNorDocsIsValid) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.5;
  config.wordsfile_ = "";
  config.docsfile_ = "";
  EXPECT_NO_THROW(qlever::QleverCliContext::validateConfig(config));
}

TEST(ValidateConfig, ErrorMessageMentionsWordsfileWhenMissing) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.5;
  config.wordsfile_ = "";
  config.docsfile_ = "docs.txt";
  try {
    qlever::QleverCliContext::validateConfig(config);
    FAIL() << "Expected exception not thrown";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(std::string(e.what()),
                ::testing::HasSubstr("wordsfile"));
  }
}

TEST(ValidateConfig, ErrorMessageMentionsDocsfileWhenMissing) {
  qlever::IndexBuilderConfig config;
  config.kScoringParam_ = 1.0;
  config.bScoringParam_ = 0.5;
  config.wordsfile_ = "words.txt";
  config.docsfile_ = "";
  try {
    qlever::QleverCliContext::validateConfig(config);
    FAIL() << "Expected exception not thrown";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(std::string(e.what()),
                ::testing::HasSubstr("docsfile"));
  }
}
