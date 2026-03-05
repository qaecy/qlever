# CLI Extension Code Review

**Scope:** `src/QleverCliContext.h`, `src/QleverCliMain.cpp`, `src/cli-utils/*`
**Date:** 2026-02-27 (updated 2026-03-04)

---

## Resolved

| ID | Summary | Resolution |
|----|---------|------------|
| C2 | Stream buffers not restored on exception | Replaced all bare `rdbuf()` swaps with `SuppressStreams` RAII guard (commit `201899e9`) |
| C4 | Separate CancellationHandles for plan vs. execute | `QueryPlan` struct now carries the handle through from planning to execution |
| H1 | `#include` directives in middle of source file | Moved to top of file |
| H2 | Fractional GB memory limit truncated to zero | Fixed formula: `static_cast<size_t>(gb * 1024 * 1024 * 1024)` |
| H3 | Errors written to stdout instead of stderr | Changed error output to `std::cerr` |
| H4 | Hardcoded 4 GB memory limit | Added `QLEVER_MEMORY_LIMIT_GB` env var override via `memoryLimit()` helper |
| M1 | `extractValue` duplicated in two files | Consolidated into `QueryExecutor::extractValue` static method |
| M2 | `getMediaType` duplicated + dead code | Removed dead `getMediaType` from `QleverCliMain.cpp`; single implementation in `QueryUtils.cpp` |
| M4 | `shared_ptr` copied instead of moved | Constructor now uses `std::move` |
| M5 | `escapeForFormat` was a no-op | Implemented proper N-Triples escaping (`\`, `"`, `\n`, `\r`, `\t`) |
| M6 | Deep-copies 500K-entry JSON array per batch | Changed to `const auto&` reference |
| L2 | Dead `totalItems_` field in ProgressTracker | Removed unused field |
| L3 | No `ORDER BY` in batched serialization queries | Added `ORDER BY` to prevent duplicate/missing triples across batches |

## Mitigated

| ID | Summary | Status |
|----|---------|--------|
| C1 | Global `rdbuf()` swap races with engine threads | Mutex in `SuppressStreams` serializes swaps. Limitation documented in `StreamSuppressor.h`: the mutex cannot prevent concurrent `operator<<` calls during a swap (technically UB). Acceptable for CLI's short-lived single-command usage. Full fix would require QLever's native log-level suppression. |
| C3 | Mutable shared state in `QleverCliContext` | Thread-safety contract documented. `QleverCliContext` is non-copyable/non-movable. The underlying `QueryResultCache` and `Index` types provide their own internal synchronization for concurrent reads. |

## Resolved (2026-03-05)

| ID | Summary | Resolution |
|----|---------|------------|
| L1 | `trimAndUpper` breaks PREFIX-prefixed queries | Replaced with `cli_utils::detectQueryType()` which skips PREFIX/BASE/comments. Added regression tests. |
| D1 | Removing `_exit(0)` causes hangs/crashes | Restored via `flushAndExit()` helper. QLever destructors don't join cleanly; `_exit` is required. See `docs/troubleshooting.md`. |
| D2 | Ubuntu CLI binary fails in Alpine container | Downstream uses `node:22-alpine3.22` (musl). Must build CLI with `Dockerfile.cli-only.alpine`, not Ubuntu. Produces `rosetta error: failed to open elf` otherwise. |

## Dropped

| ID | Summary | Reason |
|----|---------|--------|
| M3 | All `QleverCliContext` members are public | Acceptable for a CLI-internal context class. Adding getters would add boilerplate with no safety benefit since the class is only used within the CLI extension layer. |

## Summary Table

| ID  | Severity | Status |
|-----|----------|--------|
| C1  | CRITICAL | Mitigated |
| C2  | CRITICAL | Resolved |
| C3  | CRITICAL | Mitigated |
| C4  | HIGH     | Resolved |
| H1  | HIGH     | Resolved |
| H2  | HIGH     | Resolved |
| H3  | HIGH     | Resolved |
| H4  | HIGH     | Resolved |
| M1  | MEDIUM   | Resolved |
| M2  | MEDIUM   | Resolved |
| M3  | MEDIUM   | Dropped |
| M4  | MEDIUM   | Resolved |
| M5  | MEDIUM   | Resolved |
| M6  | MEDIUM   | Resolved |
| L1  | LOW      | Resolved |
| L2  | LOW      | Resolved |
| L3  | LOW      | Resolved |
| D1  | HIGH     | Resolved |
| D2  | HIGH     | Resolved |
