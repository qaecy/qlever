#ifndef QLEVER_SRC_QLEVERCLICONTEXT_H
#define QLEVER_SRC_QLEVERCLICONTEXT_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/ExecuteUpdate.h"
#include "engine/ExportQueryExecutionTrees.h"
#include "engine/MaterializedViews.h"
#include "engine/NamedResultCache.h"
#include "engine/QueryExecutionContext.h"
#include "engine/QueryPlanner.h"
#include "engine/SortPerformanceEstimator.h"
#include "global/Constants.h"
#include "global/Id.h"
#include "global/RuntimeParameters.h"
#include "index/DeltaTriples.h"
#include "index/Index.h"
#include "index/IndexImpl.h"
#include "index/TextIndexBuilder.h"
#include "libqlever/Qlever.h"
#include "libqlever/QleverTypes.h"
#include "parser/ParsedQuery.h"
#include "parser/SparqlParser.h"
#include "util/AllocatorWithLimit.h"
#include "util/CancellationHandle.h"
#include "util/Exception.h"
#include "util/SourceLocation.h"
#include "util/StringUtils.h"
#include "util/TimeTracer.h"
#include "util/Timer.h"
#include "util/http/MediaTypes.h"
#include "util/json.h"

namespace qlever {

// This is a local replacement for the qlever::Qlever class to bypass the broken
// core file (Qlever.cpp). It replicates the state and initialization logic of
// qlever::Qlever but ensures compatibility with the latest repo APIs.
//
// THREAD-SAFETY (C3): This class is NOT thread-safe. It must only be accessed
// from a single thread at a time. cache_, namedResultCache_, and
// materializedViewsManager_ are marked mutable because they are updated during
// logically-const query operations, but they carry no internal synchronization
// guarantees in the CLI context. query() and update() must not be called
// concurrently. If concurrent access is ever required, an external mutex must
// guard all method calls.
class QleverCliContext {
 public:
  mutable QueryResultCache cache_{};
  ad_utility::AllocatorWithLimit<Id> allocator_;
  SortPerformanceEstimator sortPerformanceEstimator_;
  Index index_;
  mutable NamedResultCache namedResultCache_;
  mutable MaterializedViewsManager materializedViewsManager_;
  bool enablePatternTrick_;

  explicit QleverCliContext(const EngineConfig& config)
      : allocator_{ad_utility::AllocatorWithLimit<Id>{
            ad_utility::makeAllocationMemoryLeftThreadsafeObject(
                config.memoryLimit_.value_or(DEFAULT_MEM_FOR_QUERIES))}},
        index_{allocator_},
        enablePatternTrick_{!config.noPatterns_} {
    // Load the index from disk.
    index_.usePatterns() = enablePatternTrick_;
    index_.loadAllPermutations() = !config.onlyPsoAndPos_;
    index_.createFromOnDiskIndex(config.baseName_, config.persistUpdates_);
    if (config.loadTextIndex_) {
      index_.addTextFromOnDiskIndex();
    }

    materializedViewsManager_.setOnDiskBase(config.baseName_);

    // Estimate the cost of sorting operations (needed for query planning).
    sortPerformanceEstimator_.computeEstimatesExpensively(
        allocator_, index_.numTriples().normalAndInternal_() *
                        PERCENTAGE_OF_TRIPLES_FOR_SORT_ESTIMATE / 100);
  }

  // Local query plan struct that carries the CancellationHandle created during
  // planning so that the same handle is reused during execution (C4 fix).
  struct QueryPlan {
    std::shared_ptr<QueryExecutionTree> qet;
    std::shared_ptr<QueryExecutionContext> qec;
    ParsedQuery parsedQuery;
    std::shared_ptr<ad_utility::CancellationHandle<>> handle;
  };

  QueryPlan parseAndPlanQuery(std::string query) const {
    auto qecPtr = std::make_shared<QueryExecutionContext>(
        index_, &cache_, allocator_, sortPerformanceEstimator_,
        &namedResultCache_, &materializedViewsManager_);
    auto parsedQuery = SparqlParser::parseQuery(
        &index_.getImpl().encodedIriManager(), std::move(query), {});
    auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
    QueryPlanner qp{qecPtr.get(), handle};
    qp.setEnablePatternTrick(enablePatternTrick_);
    auto qet = qp.createExecutionTree(parsedQuery);
    qet.isRoot() = true;

    auto qetPtr = std::make_shared<QueryExecutionTree>(std::move(qet));
    return {qetPtr, std::move(qecPtr), std::move(parsedQuery), std::move(handle)};
  }

  std::string query(const QueryPlan& queryPlan,
                    ad_utility::MediaType mediaType =
                        ad_utility::MediaType::sparqlJson) const {
    ad_utility::Timer timer{ad_utility::Timer::Started};
    // Reuse the same handle that was used during planning.
    auto handle = queryPlan.handle;
    std::string result;
    auto responseGenerator = ExportQueryExecutionTrees::computeResult(
        queryPlan.parsedQuery, *queryPlan.qet, mediaType, timer,
        std::move(handle));
    for (const auto& batch : responseGenerator) {
      result += batch;
    }
    return result;
  }

  std::string query(std::string queryString,
                    ad_utility::MediaType mediaType =
                        ad_utility::MediaType::sparqlJson) const {
    return query(parseAndPlanQuery(std::move(queryString)), mediaType);
  }

  void update(const std::string& updateQuery) {
    auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
    auto qec = createQec();
    auto parsedQueries = SparqlParser::parseUpdate(
        index_.getBlankNodeManager(), &index_.getImpl().encodedIriManager(),
        updateQuery, {});

    QueryPlanner planner{qec.get(), handle};
    planner.setEnablePatternTrick(enablePatternTrick_);

    index_.deltaTriplesManager().modify<UpdateMetadata>(
        std::function<UpdateMetadata(DeltaTriples&)>(
            [&](DeltaTriples& deltaTriples) mutable {
              UpdateMetadata lastMetadata;
              for (auto& parsedQuery : parsedQueries) {
                auto qet = planner.createExecutionTree(parsedQuery);
                lastMetadata = ExecuteUpdate::executeUpdate(
                    index_, parsedQuery, qet, deltaTriples, handle);
              }
              return lastMetadata;
            }));
  }

  void queryAndPinResultWithName(
      QueryExecutionContext::PinResultWithName options, std::string queryStr) {
    auto queryPlan = parseAndPlanQuery(std::move(queryStr));
    queryPlan.qec->pinResultWithName() = std::move(options);
    [[maybe_unused]] auto result = this->query(queryPlan);
  }

  void queryAndPinResultWithName(std::string name, std::string queryStr) {
    queryAndPinResultWithName(
        QueryExecutionContext::PinResultWithName{std::move(name)},
        std::move(queryStr));
  }

  static void validateConfig(const IndexBuilderConfig& config) {
    if (config.kScoringParam_ < 0) {
      throw std::invalid_argument("The value of bm25-k must be >= 0");
    }
    if (config.bScoringParam_ < 0 || config.bScoringParam_ > 1) {
      throw std::invalid_argument(
          "The value of bm25-b must be between and "
          "including 0 and 1");
    }
    if (!(config.wordsAndDocsFileSpecified() ||
          (config.wordsfile_.empty() && config.docsfile_.empty()))) {
      throw std::runtime_error(absl::StrCat(
          "Only specified ",
          config.wordsfile_.empty() ? "docsfile" : "wordsfile",
          ". Both or none of docsfile and wordsfile have to be given to build "
          "text index. If none are given the option to add words from literals "
          "has to be true. For details see --help."));
    }
  }

  std::shared_ptr<QueryExecutionContext> createQec() const {
    return std::make_shared<QueryExecutionContext>(
        index_, &cache_, allocator_, sortPerformanceEstimator_,
        &namedResultCache_, &materializedViewsManager_);
  }

  static void buildIndex(IndexBuilderConfig config) {
    Index index{ad_utility::makeUnlimitedAllocator<Id>()};

    if (config.memoryLimit_.has_value()) {
      index.memoryLimitIndexBuilding() = config.memoryLimit_.value();
    }
    if (config.parserBufferSize_.has_value()) {
      index.parserBufferSize() = config.parserBufferSize_.value();
    }

    if (config.textIndexName_.empty() && !config.wordsfile_.empty()) {
      config.textIndexName_ =
          ad_utility::getLastPartOfString(config.wordsfile_, '/');
    }

    index.setKbName(config.kbIndexName_);
    index.setTextName(config.textIndexName_);
    index.usePatterns() = !config.noPatterns_;
    index.setOnDiskBase(config.baseName_);
    index.setKeepTempFiles(config.keepTemporaryFiles_);
    index.setSettingsFile(config.settingsFile_);
    index.loadAllPermutations() = !config.onlyPsoAndPos_;
    index.getImpl().setVocabularyTypeForIndexBuilding(config.vocabType_);
    index.getImpl().setPrefixesForEncodedValues(
        config.prefixesForIdEncodedIris_);

    if (!config.onlyAddTextIndex_) {
      AD_CONTRACT_CHECK(!config.inputFiles_.empty());
      index.createFromFiles(config.inputFiles_);
    }

    if (config.wordsAndDocsFileSpecified() || config.addWordsFromLiterals_) {
#ifndef QLEVER_REDUCED_FEATURE_SET_FOR_CPP17
      auto textIndexBuilder = TextIndexBuilder(
          ad_utility::makeUnlimitedAllocator<Id>(), index.getOnDiskBase());
      textIndexBuilder.buildTextIndexFile(
          config.wordsAndDocsFileSpecified()
              ? std::optional{std::pair{config.wordsfile_, config.docsfile_}}
              : std::nullopt,
          config.addWordsFromLiterals_, config.textScoringMetric_,
          {config.bScoringParam_, config.kScoringParam_});
      if (!config.docsfile_.empty()) {
        textIndexBuilder.buildDocsDB(config.docsfile_);
      }
#endif
    }
  }
};

}  // namespace qlever

#endif  // QLEVER_SRC_QLEVERCLICONTEXT_H
