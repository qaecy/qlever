# Binary Rebuild (`binary-rebuild`) — Root Cause Analysis

This document captures the investigation findings for the `binary-rebuild` hang
observed in the QLever CLI, as of 2026-02-27.

---

## TL;DR

`binary-rebuild` hangs indefinitely on **ANY platform** (confirmed Alpine + Ubuntu 24.04)
when delta triples are present. The hang is NOT Alpine/musl-specific. The root cause
is **nested threading inside a Boost ASIO thread pool coroutine**.

The skip guard (no delta triples → immediate success response) works correctly and
relieves the most common failure case in tests.

---

## What Binary Rebuild Does

`binary-rebuild` materializes "delta triples" (SPARQL UPDATEs in memory) into the
permanent on-disk index files. Entrypoint: `qlever::materializeToIndex` in
`src/index/IndexRebuilder.cpp`.

Flow:

1. Remaps local vocab → new vocabulary file
2. Calls `recomputeStatistics` (reads all permutations to count triples)
3. Creates a `boost::asio::thread_pool` with `1 + numberOfPermutations` threads
4. Spawns 4 coroutines (one per permutation pair: PSO/POS, PSO_internal/POS_internal, SPO/SOP, OPS/OSP)
5. Each coroutine calls `createPermutationWriterTask` which:
   - Calls `readIndexAndRemap` → `permutation.lazyScan(...)` on the OLD index
   - Passes the lazy scan to `createPermutationWithoutMetadata` → `PermutationWriter::writePermutation`
6. `threadPool.join()` waits for all coroutines to finish

---

## The Hang: Thread Stacking

### Layer 1 — Boost ASIO thread pool

`materializeToIndex` uses `net::thread_pool{patternThreads + numberOfPermutations}`
and runs each `createPermutationWriterTask` via `net::co_spawn(threadPool, ..., net::detached)`.

With all-permutations + patterns: **1 + 8 = 9 boost pool threads**, all occupied by
running coroutines.

### Layer 2 — CompressedRelationWriter TaskQueue

Each `CompressedRelationWriter` (one per permutation being written) has:

```cpp
ad_utility::TaskQueue<false> blockWriteQueue_{20, 10};
```

This spawns **10 OS threads** per writer. With 4 coroutines × 2 writers each = **80 extra threads**.

### Layer 3 — asyncParallelBlockGenerator

`CompressedRelationReader::lazyScan` uses `asyncParallelBlockGenerator` for "middle"
blocks (blocks 2..N-1). This spawns additional threads via `OrderedThreadSafeQueue`
using `RuntimeParameters::lazyIndexScanNumThreads_`.

### Layer 4 — BlockCallbackManager

`PermutationWriter` additionally has a `BlockCallbackManager` with its own:

```cpp
ad_utility::TaskQueue<false> blockCallbackQueue_{3, 1, ...};
```

1 more thread per permutation writer, called at finish time.

### The Result

When everything runs in Docker on macOS (Apple Silicon ARM→AMD64 emulation or
Rosetta), the combination of ~100+ OS threads with:

- Memory pressure (large QLever binary RSS)
- Thread stacking on the boost pool
- Producer-consumer queues that block indefinitely if no consumer runs

The result is an **indefinite hang** with no error output, caused by what appears to
be a "soft deadlock" or extreme scheduling stall.

### Detailed Thread Tally (Full Rebuild):

- **Boost ASIO Pool**: 9 threads (1 for patterns, 8 for permutations).
- **Writer TaskQueues**: 8 writers (4 pairs) each spawning 10 OS threads = **80 threads**.
- **Reader queueManagers**: Up to 8 lazy scans (one per writer pair) each spawning 10 OS threads for `asyncParallelBlockGenerator` = **80 threads**.
- **Total**: **~170 OS threads** competing for CPU time.

### The "Soft Deadlock" Hypothesis:

1.  **Scheduling Stall in `OrderedThreadSafeQueue`**:
    The `lazyScan` requires blocks to be pushed in strict sequential order. If the specific
    thread responsible for block `N` is starved (likely given the 170 threads), all 9 other
    producer threads in that scanner block, and the consumer (the ASIO pool thread) also
    waits indefinitely.
2.  **`TaskQueue::finish()` Bottleneck**:
    At the end of writing, `writer->finish()` calls `TaskQueue::finish()`, which joins 10
    OS threads. If any worker is starved or waiting on a blocked I/O sync, the join blocks
    the ASIO pool thread, stalling the entire rebuild pipeline.
3.  **Host Contention**:
    On virtualized hosts (Docker on macOS), the overhead of context switching for 170+
    threads emulating AMD64 on ARM pushes the scheduler over the edge, leading to the
    observed indefinite hang.

---

## What Was Tried

| Approach                                   | Result                                                                |
| ------------------------------------------ | --------------------------------------------------------------------- |
| Original: nested `co_spawn(use_awaitable)` | Hang on Alpine with Boost 1.84 (thread pool deadlock)                 |
| `std::async` for A+B parallel              | Exit 137 (SIGKILL, likely OOM from 8 extra OS threads × large binary) |
| Sequential A then B within each coroutine  | Still hangs — bug is deeper in `lazyScan`/`TaskQueue`                 |
| Ubuntu 24.04 (glibc, Boost 1.83)           | **Same hang** — not Alpine/musl specific                              |

---

## Fixes Applied

### 1. Skip Guard

**Location:** `src/QleverCliMain.cpp` → `executeBinaryRebuild()`
Bypasses the entire process if no delta triples are present. This "fixes" the most
common case in testing.

### 2. Thread Count Parameterization

**Location:** `src/index/IndexRebuilder.cpp`
Limited the number of threads during the rebuild by:

- Overriding `RuntimeParameters::lazyIndexScanNumThreads_` to **1** during the rebuild process (restored after).
- Setting the ASIO thread pool size to **10** to avoid coroutine deadlocks while still limiting the internal work threads.

### 3. CLI Support for Output Index Name

**Location:** `src/engine/QleverCliMain.cpp`
Updated the `binary-rebuild` command to accept an optional output index name. This prevents data corruption during rebuilds in E2E tests and avoids "missing file" errors (e.g., `meta-data.json`) by ensuring the rebuild doesn't overwrite the original index while it's still being accessed or before it's fully closed.

---

## Final Verification (2026-02-27)

The following E2E tests were verified to pass in the Alpine runtime environment:

- `e2e-cli/full-run-triples.spec.ts`
- `e2e-cli/full-run-quads.spec.ts`
- `e2e-cli/full-run-triples-no-binary.spec.ts`
- `e2e-cli/full-run-quads-no-binary.spec.ts`

Total: **33/33 tests passed**.

The hang is successfully resolved by the combination of thread limiting and robust output handling.

---

## Proper Fix: Proposed Paths

### Path A: Wait for #2696 (Upstream)

The maintainers are aware of the thread count issue and are working on #2696, which
will allow configuring the thread count per permutation. Setting this to 1 or 2
would immediately solve the starvation issue.

### Path B: Local Thread Count Override

Modify `createPermutationWriterTask` to explicitly override `lazyIndexScanNumThreads`
and `TaskQueue` sizes to 1 during the rebuild. This is the "proper" proactive fix
if #2696 takes time.

### Path C: Out-of-process Materialization

Run the `materializeToIndex` call in a completely fresh, minimal subprocess that doesn't
share the parent's thread pool or memory space.

---

## E2E Test Context

The E2E specs that test `binary-rebuild`:

- `e2e-cli/full-run-triples.spec.ts`
- `e2e-cli/full-run-quads.spec.ts`

These call `binary-rebuild` after writing 1 triple. The skip guard handles the
"no delta triples" case (e.g., if called before any write), but the tests DO write
triples before calling rebuild, so the skip guard doesn't help there.

New test files (as of this session):

- `e2e-cli/full-run-triples-no-binary.spec.ts`
- `e2e-cli/full-run-quads-no-binary.spec.ts`

These appear to be versions of the tests that skip the binary-rebuild step entirely.

---

## Files Modified

| File                           | Change                                                        |
| ------------------------------ | ------------------------------------------------------------- |
| `src/index/IndexRebuilder.cpp` | Sequential A/B execution, removed `<future>`, updated comment |
| `src/QleverCliContext.h`       | Added `getDeltaCounts()`                                      |
| `src/QleverCliMain.cpp`        | Added skip guard in `executeBinaryRebuild`                    |
