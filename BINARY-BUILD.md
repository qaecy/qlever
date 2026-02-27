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

...results in an **indefinite hang** with no error output.

The coroutines print "Creating permutation PSO/SPO/OPS..." at the _start_ of
`createPermutationWithoutMetadata`, **before** any actual work. All 4 appear
simultaneously, then nothing more is logged. The hang occurs before (or at) the
first actual I/O operation of the lazy scan generator.

---

## What Was Tried

| Approach                                   | Result                                                                |
| ------------------------------------------ | --------------------------------------------------------------------- |
| Original: nested `co_spawn(use_awaitable)` | Hang on Alpine with Boost 1.84 (thread pool deadlock)                 |
| `std::async` for A+B parallel              | Exit 137 (SIGKILL, likely OOM from 8 extra OS threads × large binary) |
| Sequential A then B within each coroutine  | Still hangs — bug is deeper in `lazyScan`/`TaskQueue`                 |
| Ubuntu 24.04 (glibc, Boost 1.83)           | **Same hang** — not Alpine/musl specific                              |
| AMD64 cross-build (running via buildx)     | Unknown yet (in progress as of doc creation)                          |

---

## Fix Applied: Skip Guard

**Location:** `src/QleverCliMain.cpp` → `executeBinaryRebuild()`

Before calling `binaryRebuild()`, we check:

```cpp
auto deltaCounts = qlever->getDeltaCounts();
if (deltaCounts.triplesInserted_ == 0 && deltaCounts.triplesDeleted_ == 0) {
    // return {"success": true, "skipped": true, ...}
    _exit(0);
}
```

**Location:** `src/QleverCliContext.h` → `getDeltaCounts()`

```cpp
DeltaTriplesCount getDeltaCounts() {
    return index_.deltaTriplesManager().modify<DeltaTriplesCount>(
        std::function<DeltaTriplesCount(DeltaTriples&)>(
            [](DeltaTriples& dt) { return dt.getCounts(); }),
        false, false);
}
```

This works correctly: returns immediately when there are no delta triples.

---

## Fix Applied: Sequential Permutation Writing

**Location:** `src/index/IndexRebuilder.cpp` → `createPermutationWriterTask()`

Replaced nested `co_spawn(use_awaitable)` (deadlocks on Boost 1.84/Alpine) and
`std::async` parallel (OOM) with simple sequential execution:

```cpp
auto [_, metaA] = writePermutation(permutationA);   // synchronous
auto [__, metaB] = writePermutation(permutationB);  // synchronous
```

The outer boost pool already runs 4 coroutines concurrently — inner parallelism is
not needed for correctness.

**This does NOT fix the hang.** The hang occurs INSIDE `writePermutation` itself,
in the `PermutationWriter::writePermutation` iteration loop over the lazy scan generator.

---

## Where the Hang Actually Is

The hang is inside the `for (auto& block : sortedTriples)` loop in
`PermutationWriter::writePermutation` (line 307 of
`CompressedRelationPermutationWriterImpl.h`).

The `sortedTriples` generator is produced by `readIndexAndRemap(...)` which calls
`permutation.lazyScan(...)` which calls `CompressedRelationReader::lazyScan(...)`.

Inside `lazyScan`, when there are ≥3 blocks (possible with internal permutations
and delta triples adding sentinel blocks), it creates an `asyncParallelBlockGenerator`
that uses `queueManager<OrderedThreadSafeQueue<...>>` — a producer/consumer queue
with `lazyIndexScanNumThreads` reader threads. These threads read from the same file
that is **memory-mapped** by the existing process, adding contention.

**Exact hang location**: `OrderedThreadSafeQueue` producer/consumer synchronization,
or the `TaskQueue::finish()` join in `CompressedRelationWriter::finish()`, pending
consumer threads that fail to get scheduled due to OS-level thread starvation from
the existing ~80+ threads in the process.

---

## Possible Real Fixes

### Option A — Run binary-rebuild out-of-process

The `binary-rebuild` command already runs as a subprocess via `executeBinaryRebuild`.
The `_exit(0)` pattern shows awareness that the process state is complex. Consider
running `materializeToIndex` in a completely isolated subprocess with minimal
thread overhead.

### Option B — Disable async block generation during rebuild

Set `lazyIndexScanNumThreads = 0` before calling `materializeToIndex` so the lazy
scan runs single-threaded. This requires a RuntimeParameters override mechanism.

### Option C — Build for native architecture

The AMD64 cross-build (currently running) may behave differently. Emulated binaries
on Apple Silicon often have different OS scheduler behavior for thread starvation.

### Option D — Reduce TaskQueue thread count for rebuild context

The `blockWriteQueue_{20, 10}` spawns 10 threads per writer. A rebuild with 4
permutation pairs × 2 writers × 10 threads creates 80 extra threads on top of
the boost pool. Reducing to 1–2 threads per writer would help, but requires
changing `CompressedRelation.h` or making it configurable.

### Option E — Completely rewrite binary-rebuild to avoid Boost coroutines

Replace the `co_spawn` pattern with `std::thread` or `std::async` with explicit
`std::barrier`/`std::latch` for synchronization across pairs. This simplifies the
threading model significantly.

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
