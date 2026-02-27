# CLI Code Review — Concurrency & Robustness

**Scope:** `src/QleverCliContext.h`, `src/QleverCliMain.cpp`, `src/cli-utils/*`
**Date:** 2026-02-27
**Focus:** Concurrency hazards, crash/corruption paths, secondary findings.

---

## CRITICAL — Concurrency / Thread-Safety

### C1. Global stream-buffer redirection is not thread-safe

**Files affected (every redirect site):**
- `QleverCliMain.cpp:203-207` — redirects both `std::clog` and `std::cerr`
- `QueryUtils.cpp:33-41` — redirects `std::clog`
- `QueryUtils.cpp:84-93` — redirects `std::clog`
- `QueryUtils.cpp:121-128` — redirects `std::clog`
- `IndexStatsUtils.cpp:20-36` — redirects `std::cerr`
- `RdfOutputUtils.cpp:317-341` — redirects `std::cerr`

`std::cerr.rdbuf()` / `std::clog.rdbuf()` mutate **process-global** state. The QLever engine internally spawns worker threads (e.g. for query execution, sort estimation). If any engine thread writes to `cerr`/`clog` while the CLI thread is swapping the buffer pointer, this is a **data race → undefined behaviour** per the C++ standard.

Even without explicit multithreading in the CLI, the engine's internal threads make every redirect site a live race condition.

**Recommendation:** Replace all bare `rdbuf()` swaps with either:
1. An RAII scope-guard class that restores the buffer in its destructor (exception-safe), **or**
2. A thread-local log sink / QLever-native log-level suppression so the global streams are never touched.

---

### C2. `std::cerr` / `std::clog` not restored on exception → silent error loss + dangling buffer

**`QleverCliMain.cpp:203-241` (`executeQuery`):**

```cpp
// line 203-207: redirect
std::streambuf* clogBuf = std::clog.rdbuf();
std::streambuf* cerrBuf = std::cerr.rdbuf();
std::ofstream devNull("/dev/null");
std::clog.rdbuf(devNull.rdbuf());
std::cerr.rdbuf(devNull.rdbuf());

// ... work that can throw ...

// line 233-234: restore (only reached on success)
std::clog.rdbuf(clogBuf);
std::cerr.rdbuf(cerrBuf);
```

If any exception is thrown between lines 207 and 233:
1. The `catch` block at line 238 writes the error JSON to `std::cerr` — which is still pointed at `/dev/null`. **The user never sees the error.**
2. After the function returns, `devNull` is destroyed. `std::cerr` and `std::clog` now hold a **dangling `streambuf*`**. Any subsequent write to either stream is **undefined behaviour**.

The exact same pattern exists in:
- `QueryUtils.cpp:84-93` (`QueryExecutor::executeQuery`) — uses try/catch but only catches `...`, not all paths
- `RdfOutputUtils.cpp:334-341` (`DatabaseSerializer::serialize`) — if `qlever_->query()` throws, `cerr.rdbuf` is left redirected; `nullStream` destroyed → dangling pointer
- `IndexStatsUtils.cpp:20-36` (`runStatsQuery`) — catches `std::exception` but not other throwables; `cerr` left redirected for anything else

**Severity:** This is both a correctness bug (errors silently vanish) and a potential crash (dangling buffer use).

---

### C3. Mutable shared state in `QleverCliContext` without synchronization

**`QleverCliContext.h:46-51`:**

```cpp
mutable QueryResultCache cache_{};
// ...
mutable NamedResultCache namedResultCache_;
mutable MaterializedViewsManager materializedViewsManager_;
```

These are marked `mutable` and are passed by **raw pointer/reference** into `QueryExecutionContext` objects (lines 79-81, 173-175). Multiple `const` methods (`query`, `parseAndPlanQuery`) can be called, each creating a QEC that reads/writes the same cache.

If the QLever engine evaluates sub-trees concurrently (it does), multiple threads may read/write `cache_` and `namedResultCache_` simultaneously. Whether the underlying types are internally synchronized is not enforced or documented here — the CLI code assumes safety without verifying it.

**Additionally:** `update()` (non-const, line 115) mutates `index_` via `deltaTriplesManager().modify()`. If `query()` and `update()` overlap on the same context — even theoretically — the `index_` is accessed without external synchronization.

**Recommendation:** Either document thread-safety guarantees of the underlying types, or add a mutex around context access.

---

### C4. Separate `CancellationHandle` instances for plan vs. execute

**`QleverCliContext.h:84-91` (`parseAndPlanQuery`):**

```cpp
auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
QueryPlanner qp{qecPtr.get(), handle};
// ... builds tree with this handle ...
```

**`QleverCliContext.h:99-102` (`query`):**

```cpp
auto handle = std::make_shared<ad_utility::CancellationHandle<>>();
auto responseGenerator = ExportQueryExecutionTrees::computeResult(
    parsedQuery, *qet, mediaType, timer, std::move(handle));
```

The execution tree is **planned** with one `CancellationHandle` but **executed** with a completely different one. If the engine stores the planning handle internally and checks it during execution, cancellation signals sent to the execution handle will never reach the planner's handle (and vice versa). The query is effectively **uncancellable** from either handle.

---

## HIGH — Robustness / Correctness

### H1. `#include` directives in the middle of a source file

**`QleverCliMain.cpp:159-160`:**

```cpp
#include <algorithm>
#include <cctype>
std::string trimAndUpper(const std::string& s) {
```

Includes must be at the top of the file. Mid-file includes can cause subtle ODR (One Definition Rule) violations if macros or inline definitions interact with preceding code. This also breaks every style guide and static analysis tool.

---

### H2. Fractional GB memory limit silently truncated to zero

**`IndexBuilderUtils.cpp:168`:**

```cpp
config.memoryLimit_ =
    ad_utility::MemorySize::gigabytes(static_cast<size_t>(memoryLimitGb));
```

`static_cast<size_t>(0.5)` → `0`. A user requesting `"memory_limit_gb": 0.5` gets a **0-byte** memory limit. This will likely crash or produce incorrect results silently.

**Fix:** Use `MemorySize::bytes(static_cast<size_t>(memoryLimitGb * 1024 * 1024 * 1024))` or reject sub-1-GB values explicitly.

---

### H3. `executeUpdate` writes errors to `std::cout` instead of `std::cerr`

**`QleverCliMain.cpp:265`:**

```cpp
std::cout << errorResponse.dump() << std::endl;
```

Error responses should go to `stderr`. This mixes error output into the stdout data stream, breaking piped workflows.

Same issue in `buildIndex` (lines 313, 318) and `getIndexStats` (line 341).

---

### H4. Hardcoded 4 GB memory limit with no user override

`executeQuery`, `executeUpdate`, `serializeDatabase`, `executeQueryToFile` all hardcode:

```cpp
config.memoryLimit_ = ad_utility::MemorySize::gigabytes(4);
```

There is no CLI flag or environment variable to override this. Users with large datasets or constrained systems have no recourse.

---

## MEDIUM — Code Quality / Maintainability

### M1. `extractValue` duplicated in two places

Identical implementations exist in:
- `QleverCliMain.cpp:78-96`
- `RdfOutputUtils.cpp:234-253`

Should be a single shared utility function (e.g. in `RdfFormatUtils`).

---

### M2. `getMediaType` logic duplicated

Format-to-MediaType mapping is implemented independently in:
- `QleverCliMain.cpp:69-75` (`getMediaType` function — **unused**)
- `QueryUtils.cpp:70-81` (inline in `executeQuery`)

The standalone `getMediaType` in `QleverCliMain.cpp` is defined but never called — dead code.

---

### M3. All `QleverCliContext` members are public

**`QleverCliContext.h:46-52`:**

```cpp
public:
  mutable QueryResultCache cache_{};
  ad_utility::AllocatorWithLimit<Id> allocator_;
  SortPerformanceEstimator sortPerformanceEstimator_;
  Index index_;
  mutable NamedResultCache namedResultCache_;
  mutable MaterializedViewsManager materializedViewsManager_;
  bool enablePatternTrick_;
```

No encapsulation. Any consumer can directly mutate the index, cache, or allocator, bypassing any future invariant checks.

---

### M4. `QueryExecutor` constructor copies `shared_ptr` instead of moving

**`QueryUtils.cpp:64-65`:**

```cpp
QueryExecutor::QueryExecutor(std::shared_ptr<qlever::QleverCliContext> qlever)
    : qlever_(qlever) {}
```

Should be `qlever_(std::move(qlever))`. The current code performs a needless atomic reference-count increment/decrement.

---

### M5. `escapeForFormat` is a no-op

**`RdfOutputUtils.cpp:213-216`:**

```cpp
std::string escapeForFormat(const std::string& value, const std::string&) {
  return value;
}
```

No escaping is performed. Literal values containing `"`, `\`, or newlines will produce malformed N-Triples/N-Quads output. This is a correctness issue for any dataset with special characters.

---

### M6. `DatabaseSerializer::serialize` copies entire JSON result per batch

**`RdfOutputUtils.cpp:351`:**

```cpp
auto bindings = queryResult["results"]["bindings"];
```

This copies the entire bindings array (potentially 500K entries). Should use `const auto&` to avoid the deep copy.

---

## LOW — Style / Minor

### L1. `trimAndUpper` only extracts the first word — fragile query-type detection

**`QleverCliMain.cpp:161-169`:** Queries with leading comments (`# comment\nSELECT ...`) or `PREFIX` declarations before the query verb will be misclassified. Consider stripping SPARQL prologues before detection.

### L2. `ProgressTracker::totalItems_` is initialized but never read

**`RdfOutputUtils.h:46` / `RdfOutputUtils.cpp:64`:** `totalItems_` is set to `0` in `start()` but never updated or queried. Dead field.

### L3. No `ORDER BY` in serialization batched queries

**`RdfOutputUtils.cpp:322-331`:** The `LIMIT`/`OFFSET` serialization queries have no `ORDER BY`. Without deterministic ordering, SPARQL engines may return overlapping or missing triples across batches. This can produce **duplicate or incomplete** serialization output.

---

## Summary Table

| ID  | Severity | Category        | One-liner |
|-----|----------|-----------------|-----------|
| C1  | CRITICAL | Concurrency     | Global `rdbuf()` swap races with engine threads |
| C2  | CRITICAL | Concurrency     | Stream buffers not restored on exception → dangling ptr / silent errors |
| C3  | CRITICAL | Concurrency     | Mutable shared state (`cache_`, `index_`) without synchronization |
| C4  | HIGH     | Concurrency     | Separate CancellationHandles for plan vs. execute → uncancellable queries |
| H1  | HIGH     | Correctness     | `#include` in middle of source file |
| H2  | HIGH     | Correctness     | Fractional GB truncated to 0 |
| H3  | HIGH     | Correctness     | Errors written to stdout instead of stderr |
| H4  | HIGH     | Usability       | Hardcoded 4 GB memory limit |
| M1  | MEDIUM   | Maintainability | `extractValue` duplicated |
| M2  | MEDIUM   | Maintainability | `getMediaType` duplicated + dead code |
| M3  | MEDIUM   | Maintainability | All context members public |
| M4  | MEDIUM   | Maintainability | `shared_ptr` copied instead of moved |
| M5  | MEDIUM   | Correctness     | `escapeForFormat` is a no-op |
| M6  | MEDIUM   | Performance     | Deep-copies 500K-entry JSON array per batch |
| L1  | LOW      | Robustness      | Fragile query-type detection |
| L2  | LOW      | Code quality    | Dead `totalItems_` field |
| L3  | LOW      | Correctness     | No `ORDER BY` in batched serialization → duplicates/gaps |
