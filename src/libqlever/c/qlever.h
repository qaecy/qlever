/*  Copyright 2026 Qaecy.

    You may not use this file except in compliance with the Apache 2.0 License,
    which can be found in the `LICENSE` file at the root of the QLever project.

    QLever, embedded, for any language with a C FFI: one index held open per
    `qlever_engine`, for any number of concurrent readers and one writer at a
    time. Every function mirrors a method of `EmbeddedEngine` (see
    `libqlever/embedded/EmbeddedEngine.h` for the semantics).

    Conventions:
      - Every call returns a `qlever_status`. On anything but `QLEVER_OK` and a
        non-null `error`, `*error` receives a message the caller frees with
        `qlever_free`.
      - JSON answers arrive in `*json`, a NUL-terminated string the caller frees
        with `qlever_free`; it is only set on `QLEVER_OK`.
      - Strings passed in are UTF-8 and NUL-terminated, except SPARQL texts,
        which are passed with their length and need no terminator.
      - Nothing here aborts the process or lets a C++ exception escape.
*/

#ifndef QLEVER_SRC_LIBQLEVER_C_QLEVER_H
#define QLEVER_SRC_LIBQLEVER_C_QLEVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qlever_engine qlever_engine;

typedef enum qlever_status {
  QLEVER_OK = 0,
  /* An invalid argument: an unknown format or keep-previous policy. */
  QLEVER_INVALID = 1,
  /* A query ran past its deadline and was cancelled. */
  QLEVER_TIMEOUT = 2,
  /* The sink returned non-zero, which stops the query. */
  QLEVER_STOPPED = 3,
  /* Anything else: a parse error, a missing index, an I/O failure. */
  QLEVER_FAILED = 4
} qlever_status;

/* Receives the serialized result in chunks of about 1 MB, in order. Returning
   non-zero stops the query with `QLEVER_STOPPED`. */
typedef int (*qlever_sink)(void* context, const char* data, size_t length);

/* Bumped whenever a signature or a semantic of this header changes. */
uint32_t qlever_abi_version(void);

/* Open the index with this base name, replaying its update layer once. */
qlever_status qlever_open(const char* base_name, double memory_gb,
                          int persist_updates, qlever_engine** engine,
                          char** error);

/* Close the index. Safe on NULL. No call on `engine` may still be running. */
void qlever_close(qlever_engine* engine);

/* Formats: `sparql-json`, `csv`, `tsv` (SELECT/ASK), `nt`, `nq`
   (CONSTRUCT/DESCRIBE); empty picks `sparql-json` or `nt` by query type. A
   positive `timeout_ms` cancels the query once it has passed. */
qlever_status qlever_query(const qlever_engine* engine, const char* sparql,
                           size_t sparql_length, const char* format,
                           uint64_t timeout_ms, qlever_sink sink,
                           void* context, char** error);

/* A SPARQL UPDATE applied atomically; `*json` holds the delta counts before
   and after. */
qlever_status qlever_update(qlever_engine* engine, const char* sparql,
                            size_t sparql_length, char** json, char** error);

/* Insert (or, with `is_delete`, delete) every triple of an N-Quads, N-Triples
   or Turtle file atomically. `default_graph` (may be empty) is the graph of
   triples that carry none. */
qlever_status qlever_write_file(qlever_engine* engine, const char* path,
                                const char* format, const char* default_graph,
                                int is_delete, char** json, char** error);

/* Inserted and deleted delta triples, and the triples of the base index. */
qlever_status qlever_counts(const qlever_engine* engine, char** json,
                            char** error);

/* Merge the update layer into a new index and swap it in, while queries and
   writes continue. `keep_previous`: `all`, `none`, `original-only`,
   `most-recent-only`, `original-and-most-recent`. */
qlever_status qlever_rebuild(qlever_engine* engine, const char* keep_previous,
                             char** json, char** error);

qlever_status qlever_write_view(qlever_engine* engine, const char* name,
                                const char* sparql, size_t sparql_length,
                                char** error);

qlever_status qlever_load_view(const qlever_engine* engine, const char* name,
                               char** error);

/* Free a string this library returned. Safe on NULL. */
void qlever_free(char* text);

#ifdef __cplusplus
}
#endif

#endif /* QLEVER_SRC_LIBQLEVER_C_QLEVER_H */
