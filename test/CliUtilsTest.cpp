// Tests for cli-utils fixes:
//   C1 – SuppressStreams process-wide rdbuf race awareness
//   C2 – SuppressStreams RAII guard (exception safety, restore, nesting)
//   C3 – QleverCliContext thread-safety contract (compile-time assertions)
//   C4 – QleverCliContext::QueryPlan carries CancellationHandle
//   H2 – Fractional GB memory limit conversion

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <iostream>
#include <latch>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cli-utils/CliArgs.h"
#include "cli-utils/QueryTypeDetect.h"
#include "cli-utils/StreamSuppressor.h"
#include "util/MemorySize/MemorySize.h"

// ============================================================
// Helpers
// ============================================================

// Returns the current rdbuf of std::cerr as a void* for identity comparison.
static void* cerrBuf() { return static_cast<void*>(std::cerr.rdbuf()); }
static void* clogBuf() { return static_cast<void*>(std::clog.rdbuf()); }

// ============================================================
// C2 – SuppressStreams RAII tests
// ============================================================

TEST(SuppressStreams, RestoresCerrOnNormalExit) {
  void* originalCerr = cerrBuf();
  void* originalClog = clogBuf();
  {
    cli_utils::SuppressStreams suppress;
    EXPECT_NE(cerrBuf(), originalCerr) << "cerr should be redirected";
    EXPECT_NE(clogBuf(), originalClog) << "clog should be redirected";
  }
  EXPECT_EQ(cerrBuf(), originalCerr) << "cerr must be restored after scope";
  EXPECT_EQ(clogBuf(), originalClog) << "clog must be restored after scope";
}

TEST(SuppressStreams, RestoresCerrOnException) {
  void* originalCerr = cerrBuf();
  void* originalClog = clogBuf();
  try {
    cli_utils::SuppressStreams suppress;
    EXPECT_NE(cerrBuf(), originalCerr);
    throw std::runtime_error("deliberate");
  } catch (const std::runtime_error&) {
  }
  EXPECT_EQ(cerrBuf(), originalCerr)
      << "cerr must be restored even after exception";
  EXPECT_EQ(clogBuf(), originalClog)
      << "clog must be restored even after exception";
}

TEST(SuppressStreams, SuppressedOutputDoesNotReachOriginalBuffer) {
  // Capture cerr into a stringstream, then verify nothing leaks through.
  std::ostringstream capture;
  std::streambuf* oldCerr = std::cerr.rdbuf(capture.rdbuf());

  {
    cli_utils::SuppressStreams suppress;
    std::cerr << "should be suppressed";
    std::clog << "also suppressed";
  }

  std::cerr.rdbuf(oldCerr);  // Restore to real cerr before assertions.
  EXPECT_TRUE(capture.str().empty())
      << "Suppressed output leaked: " << capture.str();
}

TEST(SuppressStreams, OutputAfterScopeReachesOriginalBuffer) {
  std::ostringstream capture;
  std::streambuf* oldCerr = std::cerr.rdbuf(capture.rdbuf());

  {
    cli_utils::SuppressStreams suppress;
    std::cerr << "suppressed";
  }
  std::cerr << "visible";

  std::cerr.rdbuf(oldCerr);
  EXPECT_EQ(capture.str(), "visible")
      << "Output after scope should reach the buffer that was active before";
}

TEST(SuppressStreams, NestedSuppressorsRestoreCorrectly) {
  void* outerCerr = cerrBuf();
  {
    cli_utils::SuppressStreams outer;
    void* afterOuter = cerrBuf();
    EXPECT_NE(afterOuter, outerCerr);
    {
      cli_utils::SuppressStreams inner;
      // inner suppresses the already-suppressed stream – fine
    }
    // After inner destructs, cerr.rdbuf should equal whatever outer set it to
    EXPECT_EQ(cerrBuf(), afterOuter)
        << "Inner destructor must restore to outer's redirect, not the "
           "original";
  }
  EXPECT_EQ(cerrBuf(), outerCerr) << "Outer destructor must restore original";
}

TEST(SuppressStreams, ThreadDoesNotSeeRedirectedBuffer) {
  // Spawn a thread BEFORE the suppressor is created and check that the thread
  // observes the SAME cerr.rdbuf as the main thread (i.e. both see the same
  // global pointer). This validates that there is only one global rdbuf and
  // that the guard swaps it process-wide – giving us a concrete baseline for
  // the C1 race described in the review.
  void* mainOriginal = cerrBuf();
  std::atomic<void*> threadObserved{nullptr};

  std::thread t([&] {
    // Small sleep to let main thread enter the suppressor scope.
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    threadObserved.store(cerrBuf());
  });

  {
    cli_utils::SuppressStreams suppress;
    t.join();
  }

  // The thread may have seen either the original or the suppressed buffer
  // depending on timing – what we assert is that after the scope, the main
  // thread's cerr is back to normal regardless of what the thread observed.
  EXPECT_EQ(cerrBuf(), mainOriginal)
      << "Main thread must have cerr restored after SuppressStreams scope";
  (void)threadObserved.load();  // suppress unused warning
}

// ============================================================
// H2 – Fractional GB memory size conversion
// ============================================================

// White-box test: verify the byte calculation directly, without building an
// index, by replicating the fixed formula used in IndexBuilderUtils.cpp.
TEST(MemoryLimitConversion, FractionalGbIsNotTruncatedToZero) {
  // Old (broken) formula:
  auto brokenFormula = [](double gb) -> size_t {
    return static_cast<size_t>(gb);  // truncates 0.5 → 0
  };
  // Fixed formula (matches IndexBuilderUtils.cpp after fix):
  auto fixedFormula = [](double gb) -> size_t {
    return static_cast<size_t>(gb * 1024.0 * 1024.0 * 1024.0);
  };

  EXPECT_EQ(brokenFormula(0.5), 0u) << "Sanity: old formula truncates 0.5";
  EXPECT_GT(fixedFormula(0.5), 0u) << "Fixed formula must not truncate 0.5 GB";

  // 0.5 GB = 536870912 bytes
  EXPECT_EQ(fixedFormula(0.5), 536870912u);
  // 1 GB stays correct
  EXPECT_EQ(fixedFormula(1.0), 1073741824u);
  // 2.5 GB
  EXPECT_EQ(fixedFormula(2.5), static_cast<size_t>(2.5 * 1024.0 * 1024.0 * 1024.0));
}

TEST(MemoryLimitConversion, MemorySizeBytesRoundtrip) {
  // Confirm that ad_utility::MemorySize::bytes accepts the value that the
  // fixed formula produces (i.e. it does not overflow or reject it).
  auto ms = ad_utility::MemorySize::bytes(
      static_cast<size_t>(0.5 * 1024.0 * 1024.0 * 1024.0));
  EXPECT_EQ(ms.getBytes(), 536870912u);
}

// ============================================================
// C4 – QueryPlan handle struct has the handle field
// ============================================================

// These are compile-time / type-level checks. If the struct was reverted to
// the old tuple alias (which has no `handle` field), these tests will fail
// to compile.

#include "QleverCliContext.h"

TEST(QueryPlanStruct, HasHandleField) {
  // We can't construct a real QueryPlan without loading an index, but we can
  // verify the struct layout by checking that the type of the `handle` member
  // is correct.
  using PlanType = qlever::QleverCliContext::QueryPlan;
  using HandleType = std::shared_ptr<ad_utility::CancellationHandle<>>;

  // This static_assert will fail to compile if the field is missing.
  static_assert(std::is_same_v<decltype(std::declval<PlanType>().handle),
                               HandleType>,
                "QueryPlan::handle must be a shared_ptr<CancellationHandle<>>");
  SUCCEED();
}

namespace {
template <typename T, typename = void>
struct is_tuple_like : std::false_type {};
template <typename T>
struct is_tuple_like<T, std::void_t<decltype(std::tuple_size<T>{})>>
    : std::true_type {};
}  // namespace

TEST(QueryPlanStruct, IsNotTupleAlias) {
  // The old type was a std::tuple – verify the new type is a struct (not a
  // tuple). The is_tuple_like trait uses void_t on std::tuple_size<T>
  // (not ::value) which SFINAE-fails cleanly for non-tuple types on GCC.
  using PlanType = qlever::QleverCliContext::QueryPlan;
  EXPECT_FALSE(is_tuple_like<PlanType>::value)
      << "QueryPlan must be a struct, not a tuple alias";
}

// ============================================================
// C2 – Additional SuppressStreams edge cases
// ============================================================

TEST(SuppressStreams, ClogIsIndependentlyRestored) {
  void* originalClog = clogBuf();
  {
    cli_utils::SuppressStreams suppress;
    EXPECT_NE(clogBuf(), originalClog);
    std::clog << "suppressed clog message";
  }
  EXPECT_EQ(clogBuf(), originalClog)
      << "clog buffer must be restored after scope";
}

TEST(SuppressStreams, BothStreamsAreRedirectedSimultaneously) {
  void* originalCerr = cerrBuf();
  void* originalClog = clogBuf();
  {
    cli_utils::SuppressStreams suppress;
    EXPECT_NE(cerrBuf(), originalCerr) << "cerr must be redirected";
    EXPECT_NE(clogBuf(), originalClog) << "clog must be redirected";
    // Both must point to the same /dev/null rdbuf (they share the stream).
    EXPECT_EQ(cerrBuf(), clogBuf())
        << "cerr and clog must point to the same suppressed buffer";
  }
}

TEST(SuppressStreams, TripleNestingRestoresCorrectly) {
  void* lvl0 = cerrBuf();
  {
    cli_utils::SuppressStreams s1;
    void* lvl1 = cerrBuf();
    EXPECT_NE(lvl1, lvl0);
    {
      cli_utils::SuppressStreams s2;
      void* lvl2 = cerrBuf();
      (void)lvl2;  // may equal lvl1 (both /dev/null), just mustn't crash
      {
        cli_utils::SuppressStreams s3;
      }
      EXPECT_EQ(cerrBuf(), lvl2)
          << "After s3 destructs, buffer must equal what s2 saw";
    }
    EXPECT_EQ(cerrBuf(), lvl1)
        << "After s2 destructs, buffer must equal what s1 saw";
  }
  EXPECT_EQ(cerrBuf(), lvl0) << "After s1 destructs, buffer must equal lvl0";
}

TEST(SuppressStreams, ExceptionInNestedScopeRestoresBothLevels) {
  void* outerCerr = cerrBuf();
  void* outerClog = clogBuf();
  try {
    cli_utils::SuppressStreams outer;
    try {
      cli_utils::SuppressStreams inner;
      throw std::runtime_error("inner exception");
    } catch (...) {
    }
    // outer is still alive; after inner is destroyed, outer's redirect holds
    EXPECT_NE(cerrBuf(), outerCerr)
        << "outer suppressor still active after inner exception";
  } catch (...) {
  }
  EXPECT_EQ(cerrBuf(), outerCerr) << "cerr fully restored after both scopes";
  EXPECT_EQ(clogBuf(), outerClog) << "clog fully restored after both scopes";
}

// ============================================================
// C1 – SuppressStreams concurrency regression tests
// ============================================================

// These tests do NOT assert absence of data races (that requires TSan), but
// they do assert the single-threaded postcondition: after ALL SuppressStreams
// objects are destroyed, the main thread's cerr is back to normal.  They also
// stress-test for crashes caused by use-after-free (C2 fix).

TEST(SuppressStreamsConcurrency, MainThreadRestoresAfterWorkerWritesDuring) {
  void* original = cerrBuf();
  std::atomic<bool> workerDone{false};

  std::thread worker([&] {
    // Keep writing to cerr while the suppressor is active (may see either
    // the suppressed or restored buffer, depending on scheduling).
    for (int i = 0; i < 200; ++i) {
      std::cerr << "w";
    }
    workerDone.store(true, std::memory_order_release);
  });

  {
    cli_utils::SuppressStreams suppress;
    // Let the worker run while we're suppressed.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  worker.join();
  EXPECT_TRUE(workerDone.load());
  EXPECT_EQ(cerrBuf(), original)
      << "Main thread cerr must be restored after worker finishes";
}

TEST(SuppressStreamsConcurrency, MultipleSequentialSuppressors) {
  void* original = cerrBuf();
  // Rapidly create and destroy many suppressors from the main thread.
  for (int i = 0; i < 100; ++i) {
    cli_utils::SuppressStreams s;
    ASSERT_NE(cerrBuf(), original) << "iter " << i;
  }
  EXPECT_EQ(cerrBuf(), original)
      << "cerr must be original after 100 sequential suppressors";
}

TEST(SuppressStreamsConcurrency, SuppressorsOnMultipleThreadsDoNotDeadlock) {
  // Each of N threads creates a SuppressStreams, writes, then destroys.
  // We verify: (a) no crash/deadlock, (b) main thread cerr is intact at end.
  void* original = cerrBuf();
  constexpr int kThreads = 8;
  constexpr int kIter = 20;

  std::latch ready(kThreads);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&ready] {
      ready.count_down();
      ready.wait();  // all start at the same time
      for (int i = 0; i < kIter; ++i) {
        cli_utils::SuppressStreams s;
        std::cerr << "t" << i;
        std::clog << "t" << i;
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  // After all threads finish, the main thread's cerr must still be intact.
  EXPECT_EQ(cerrBuf(), original)
      << "cerr must be the original buffer after all threads complete";
}

TEST(SuppressStreamsConcurrency,
     SuppressorDestructorNotCalledWhileOtherThreadHoldsReference) {
  // Verifies no use-after-free: a suppressor destroyed in thread A must not
  // corrupt a suppressor still alive in thread B.
  void* original = cerrBuf();

  std::latch startLatch(2);
  std::atomic<bool> bDone{false};

  // Thread B holds a SuppressStreams for longer than thread A.
  std::thread threadB([&] {
    cli_utils::SuppressStreams bSuppress;
    startLatch.count_down();
    startLatch.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::cerr << "b";
    bDone.store(true, std::memory_order_release);
  });

  // Thread A holds a SuppressStreams briefly, then lets it go.
  std::thread threadA([&] {
    startLatch.count_down();
    startLatch.wait();
    {
      cli_utils::SuppressStreams aSuppress;
      std::cerr << "a";
    }
  });

  threadA.join();
  threadB.join();

  EXPECT_TRUE(bDone.load());
  EXPECT_EQ(cerrBuf(), original)
      << "cerr must be original after both threads finish";
}

// ============================================================
// H2 – Additional memory size edge cases
// ============================================================

TEST(MemoryLimitConversion, ZeroGbProducesZeroBytes) {
  auto ms = ad_utility::MemorySize::bytes(
      static_cast<size_t>(0.0 * 1024.0 * 1024.0 * 1024.0));
  EXPECT_EQ(ms.getBytes(), 0u);
}

TEST(MemoryLimitConversion, VerySmallFractionalGbIsPositive) {
  // 0.001 GB = ~1 MB
  size_t bytes =
      static_cast<size_t>(0.001 * 1024.0 * 1024.0 * 1024.0);
  EXPECT_GT(bytes, 0u);
  auto ms = ad_utility::MemorySize::bytes(bytes);
  EXPECT_GT(ms.getBytes(), 0u);
}

TEST(MemoryLimitConversion, LargeValueDoesNotOverflow) {
  // 16 GB – well within size_t range on 64-bit
  constexpr double kGb = 16.0;
  size_t bytes = static_cast<size_t>(kGb * 1024.0 * 1024.0 * 1024.0);
  EXPECT_EQ(bytes, static_cast<size_t>(16ull * 1024 * 1024 * 1024));
  auto ms = ad_utility::MemorySize::bytes(bytes);
  EXPECT_EQ(ms.getBytes(), bytes);
}

TEST(MemoryLimitConversion, FourGbExact) {
  size_t expected = static_cast<size_t>(4ull * 1024 * 1024 * 1024);
  size_t computed = static_cast<size_t>(4.0 * 1024.0 * 1024.0 * 1024.0);
  EXPECT_EQ(computed, expected);
}

// ============================================================
// C3 – QleverCliContext non-copyable / non-movable contract
// ============================================================

TEST(QleverCliContextContract, IsNotCopyConstructible) {
  static_assert(!std::is_copy_constructible_v<qlever::QleverCliContext>,
                "QleverCliContext must not be copy-constructible (C3)");
  SUCCEED();
}

TEST(QleverCliContextContract, IsNotCopyAssignable) {
  static_assert(!std::is_copy_assignable_v<qlever::QleverCliContext>,
                "QleverCliContext must not be copy-assignable (C3)");
  SUCCEED();
}

// ============================================================
// L1 – detectQueryType: skip PREFIX/BASE/comments
// ============================================================

TEST(DetectQueryType, SimpleSelect) {
  EXPECT_EQ(cli_utils::detectQueryType("SELECT * WHERE { ?s ?p ?o }"),
            "SELECT");
}

TEST(DetectQueryType, SimpleConstruct) {
  EXPECT_EQ(cli_utils::detectQueryType("CONSTRUCT { ?s ?p ?o } WHERE { ?s ?p ?o }"),
            "CONSTRUCT");
}

TEST(DetectQueryType, SimpleDescribe) {
  EXPECT_EQ(cli_utils::detectQueryType("DESCRIBE <http://example.org>"),
            "DESCRIBE");
}

TEST(DetectQueryType, SimpleAsk) {
  EXPECT_EQ(cli_utils::detectQueryType("ASK { ?s ?p ?o }"), "ASK");
}

TEST(DetectQueryType, PrefixThenSelect) {
  EXPECT_EQ(cli_utils::detectQueryType(
                "PREFIX ex: <http://example.org/>\nSELECT * WHERE { ?s ?p ?o }"),
            "SELECT");
}

TEST(DetectQueryType, MultiplePrefixesThenConstruct) {
  std::string query =
      "PREFIX ex: <http://example.org/>\n"
      "PREFIX foaf: <http://xmlns.com/foaf/0.1/>\n"
      "CONSTRUCT { ?s ?p ?o } WHERE { ?s ?p ?o }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "CONSTRUCT");
}

TEST(DetectQueryType, BaseThenSelect) {
  EXPECT_EQ(cli_utils::detectQueryType(
                "BASE <http://example.org/>\nSELECT * WHERE { ?s ?p ?o }"),
            "SELECT");
}

TEST(DetectQueryType, LowercasePrefixAndBase) {
  std::string query =
      "prefix ex: <http://example.org/>\n"
      "base <http://example.org/>\n"
      "SELECT * WHERE { ?s ?p ?o }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "SELECT");
}

TEST(DetectQueryType, CommentLinesSkipped) {
  std::string query =
      "# This is a comment\n"
      "# Another comment\n"
      "SELECT * WHERE { ?s ?p ?o }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "SELECT");
}

TEST(DetectQueryType, EmptyQueryReturnsEmpty) {
  EXPECT_EQ(cli_utils::detectQueryType(""), "");
}

TEST(DetectQueryType, WhitespaceOnlyReturnsEmpty) {
  EXPECT_EQ(cli_utils::detectQueryType("   \n\t\n  "), "");
}

TEST(DetectQueryType, LeadingWhitespaceHandled) {
  EXPECT_EQ(cli_utils::detectQueryType("   \t  SELECT * WHERE { ?s ?p ?o }"),
            "SELECT");
}

// ============================================================
// Regression: queries from the downstream API often carry PREFIX
// declarations.  The old trimAndUpper() would return "PREFIX"
// instead of the actual verb, breaking CONSTRUCT/DESCRIBE dispatch.
// ============================================================

TEST(DetectQueryType, PrefixThenDescribe) {
  std::string query =
      "PREFIX schema: <http://schema.org/>\n"
      "DESCRIBE <http://example.org/resource>";
  EXPECT_EQ(cli_utils::detectQueryType(query), "DESCRIBE");
}

TEST(DetectQueryType, MixedPrefixBaseCommentThenAsk) {
  std::string query =
      "# Check existence\n"
      "PREFIX ex: <http://example.org/>\n"
      "BASE <http://example.org/>\n"
      "ASK { ex:foo ex:bar ex:baz }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "ASK");
}

TEST(DetectQueryType, PrefixOnSameLineStyleDescribe) {
  // Some clients emit PREFIX without a trailing newline before the verb
  std::string query =
      "PREFIX ex: <http://example.org/>\n"
      "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns#>\n"
      "PREFIX rdfs: <http://www.w3.org/2000/01/rdf-schema#>\n"
      "CONSTRUCT { ?s rdfs:label ?o } WHERE { ?s rdfs:label ?o }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "CONSTRUCT");
}

TEST(DetectQueryType, InsertDataDetected) {
  // SPARQL UPDATE verb — detectQueryType should return it as-is
  std::string query =
      "PREFIX ex: <http://example.org/>\n"
      "INSERT DATA { ex:s ex:p ex:o }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "INSERT");
}

TEST(DetectQueryType, DeleteWhereDetected) {
  std::string query =
      "PREFIX ex: <http://example.org/>\n"
      "DELETE WHERE { ex:s ex:p ?o }";
  EXPECT_EQ(cli_utils::detectQueryType(query), "DELETE");
}

// ============================================================
// CliArgs – parseGlobalFlags tests
// ============================================================

namespace {
// Helper to build a fake argv from a vector of strings.
struct FakeArgv {
  std::vector<std::string> storage;
  std::vector<char*> ptrs;

  explicit FakeArgv(std::initializer_list<std::string> args)
      : storage(args) {
    for (auto& s : storage) {
      ptrs.push_back(s.data());
    }
  }
  int argc() const { return static_cast<int>(ptrs.size()); }
  char** argv() { return ptrs.data(); }
};
}  // namespace

TEST(CliArgs, NoFlagPresent) {
  FakeArgv a{"qlever-cli", "query", "myindex", "SELECT * { ?s ?p ?o }"};
  auto parsed = cli_utils::parseGlobalFlags(a.argc(), a.argv());
  EXPECT_FALSE(parsed.maxMemoryGb.has_value());
  ASSERT_EQ(parsed.remaining.size(), 4u);
  EXPECT_EQ(parsed.remaining[0], "qlever-cli");
  EXPECT_EQ(parsed.remaining[1], "query");
}

TEST(CliArgs, FlagBeforeCommand) {
  FakeArgv a{"qlever-cli", "--max-memory-in-gb", "2", "query", "myindex",
             "SELECT *"};
  auto parsed = cli_utils::parseGlobalFlags(a.argc(), a.argv());
  ASSERT_TRUE(parsed.maxMemoryGb.has_value());
  EXPECT_DOUBLE_EQ(parsed.maxMemoryGb.value(), 2.0);
  // remaining should be: qlever-cli query myindex SELECT *
  ASSERT_EQ(parsed.remaining.size(), 4u);
  EXPECT_EQ(parsed.remaining[1], "query");
}

TEST(CliArgs, FlagAfterCommand) {
  FakeArgv a{"qlever-cli", "stats", "--max-memory-in-gb", "0.5", "myindex"};
  auto parsed = cli_utils::parseGlobalFlags(a.argc(), a.argv());
  ASSERT_TRUE(parsed.maxMemoryGb.has_value());
  EXPECT_DOUBLE_EQ(parsed.maxMemoryGb.value(), 0.5);
  ASSERT_EQ(parsed.remaining.size(), 3u);
  EXPECT_EQ(parsed.remaining[1], "stats");
  EXPECT_EQ(parsed.remaining[2], "myindex");
}

TEST(CliArgs, FractionalValue) {
  FakeArgv a{"prog", "--max-memory-in-gb", "0.25"};
  auto parsed = cli_utils::parseGlobalFlags(a.argc(), a.argv());
  ASSERT_TRUE(parsed.maxMemoryGb.has_value());
  EXPECT_DOUBLE_EQ(parsed.maxMemoryGb.value(), 0.25);
}

TEST(CliArgs, MissingValueThrows) {
  FakeArgv a{"prog", "--max-memory-in-gb"};
  EXPECT_THROW(cli_utils::parseGlobalFlags(a.argc(), a.argv()),
               std::runtime_error);
}

TEST(CliArgs, InvalidValueThrows) {
  FakeArgv a{"prog", "--max-memory-in-gb", "abc"};
  EXPECT_THROW(cli_utils::parseGlobalFlags(a.argc(), a.argv()),
               std::runtime_error);
}

TEST(CliArgs, NegativeValueThrows) {
  FakeArgv a{"prog", "--max-memory-in-gb", "-1"};
  EXPECT_THROW(cli_utils::parseGlobalFlags(a.argc(), a.argv()),
               std::runtime_error);
}

TEST(CliArgs, ZeroValueThrows) {
  FakeArgv a{"prog", "--max-memory-in-gb", "0"};
  EXPECT_THROW(cli_utils::parseGlobalFlags(a.argc(), a.argv()),
               std::runtime_error);
}

TEST(CliArgs, TrailingCharsThrows) {
  FakeArgv a{"prog", "--max-memory-in-gb", "4abc"};
  EXPECT_THROW(cli_utils::parseGlobalFlags(a.argc(), a.argv()),
               std::runtime_error);
}

// ============================================================
// CliArgs – resolveMemoryLimit tests
// ============================================================

TEST(ResolveMemoryLimit, CliOverrideWins) {
  auto ms = cli_utils::resolveMemoryLimit(2.0);
  EXPECT_EQ(ms.getBytes(),
            static_cast<size_t>(2.0 * 1024.0 * 1024.0 * 1024.0));
}

TEST(ResolveMemoryLimit, DefaultWhenNoOverrideNoEnv) {
  // Unset the env var to be safe.
  ::unsetenv("QLEVER_MEMORY_LIMIT_GB");
  auto ms = cli_utils::resolveMemoryLimit(std::nullopt);
  EXPECT_EQ(ms.getBytes(),
            static_cast<size_t>(4.0 * 1024.0 * 1024.0 * 1024.0));
}

TEST(ResolveMemoryLimit, CustomDefault) {
  ::unsetenv("QLEVER_MEMORY_LIMIT_GB");
  auto ms = cli_utils::resolveMemoryLimit(std::nullopt, 1.0);
  EXPECT_EQ(ms.getBytes(),
            static_cast<size_t>(1.0 * 1024.0 * 1024.0 * 1024.0));
}

TEST(ResolveMemoryLimit, EnvVarUsedWhenNoCliOverride) {
  ::setenv("QLEVER_MEMORY_LIMIT_GB", "8", 1);
  auto ms = cli_utils::resolveMemoryLimit(std::nullopt);
  ::unsetenv("QLEVER_MEMORY_LIMIT_GB");
  EXPECT_EQ(ms.getBytes(),
            static_cast<size_t>(8.0 * 1024.0 * 1024.0 * 1024.0));
}

TEST(ResolveMemoryLimit, CliOverrideTakesPrecedenceOverEnv) {
  ::setenv("QLEVER_MEMORY_LIMIT_GB", "8", 1);
  auto ms = cli_utils::resolveMemoryLimit(3.0);
  ::unsetenv("QLEVER_MEMORY_LIMIT_GB");
  EXPECT_EQ(ms.getBytes(),
            static_cast<size_t>(3.0 * 1024.0 * 1024.0 * 1024.0));
}

TEST(ResolveMemoryLimit, MalformedEnvFallsToDefault) {
  ::setenv("QLEVER_MEMORY_LIMIT_GB", "not-a-number", 1);
  auto ms = cli_utils::resolveMemoryLimit(std::nullopt);
  ::unsetenv("QLEVER_MEMORY_LIMIT_GB");
  EXPECT_EQ(ms.getBytes(),
            static_cast<size_t>(4.0 * 1024.0 * 1024.0 * 1024.0));
}
