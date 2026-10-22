/*  Copyright 2026 Qaecy.

    You may not use this file except in compliance with the Apache 2.0 License,
    which can be found in the `LICENSE` file at the root of the QLever project.

    The C ABI exercised from C: `qlever_c_test <index base name> <scratch dir>`
    against an index of ten triples in <http://example.org/g> (see `run.sh`).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libqlever/c/qlever.h"

static int failures = 0;

#define CHECK(condition, what)                       \
  do {                                               \
    if (!(condition)) {                              \
      fprintf(stderr, "FAILED: %s (line %d)\n", what, __LINE__); \
      failures++;                                    \
    }                                                \
  } while (0)

typedef struct {
  char data[1 << 16];
  size_t length;
  int chunks;
  int stop_after;
} buffer;

static int collect(void* context, const char* data, size_t length) {
  buffer* b = (buffer*)context;
  size_t room = sizeof(b->data) - 1 - b->length;
  size_t taken = length < room ? length : room;
  memcpy(b->data + b->length, data, taken);
  b->length += taken;
  b->data[b->length] = '\0';
  b->chunks++;
  return b->stop_after > 0 && b->chunks >= b->stop_after;
}

static qlever_status ask(const qlever_engine* engine, const char* sparql,
                         const char* format, buffer* out, char** error) {
  out->length = 0;
  out->chunks = 0;
  out->data[0] = '\0';
  return qlever_query(engine, sparql, strlen(sparql), format, 0, collect, out,
                      error);
}

static const char* COUNT =
    "SELECT (COUNT(*) AS ?n) WHERE { GRAPH ?g { ?s ?p ?o } }";

static int counted(const qlever_engine* engine, int expected) {
  buffer out = {0};
  char needle[64];
  snprintf(needle, sizeof(needle), "\"value\":\"%d\"", expected);
  qlever_status status = ask(engine, COUNT, "sparql-json", &out, NULL);
  return status == QLEVER_OK && strstr(out.data, needle) != NULL;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <index base name> <scratch dir>\n", argv[0]);
    return 2;
  }
  const char* base = argv[1];
  char path[4096];
  char* error = NULL;
  char* json = NULL;
  qlever_engine* engine = NULL;
  buffer out = {0};

  CHECK(qlever_abi_version() == 1, "the ABI version is 1");

  CHECK(qlever_open("/nonexistent/index", 1.0, 1, &engine, &error) == QLEVER_FAILED,
        "a missing index fails to open");
  CHECK(error != NULL && strlen(error) > 0, "with a message");
  qlever_free(error);
  error = NULL;

  CHECK(qlever_open(base, 1.0, 1, &engine, &error) == QLEVER_OK, "the index opens");
  CHECK(counted(engine, 10), "it holds ten triples");

  CHECK(ask(engine, "SELECT ?s WHERE { GRAPH ?g { ?s ?p ?o } } ORDER BY ?s LIMIT 1", "tsv", &out, NULL) == QLEVER_OK &&
            strstr(out.data, "<http://example.org/s0>") != NULL,
        "a query answers in TSV");
  CHECK(ask(engine, "CONSTRUCT { ?s ?p ?o } WHERE { GRAPH ?g { ?s ?p ?o } }", "csv", &out, &error) == QLEVER_INVALID,
        "a CONSTRUCT cannot answer as CSV");
  qlever_free(error);
  error = NULL;
  CHECK(ask(engine, "SELECT * WHERE { not sparql", "", &out, &error) == QLEVER_FAILED, "a broken query fails");
  qlever_free(error);
  error = NULL;

  buffer stopping = {0};
  stopping.stop_after = 1;
  const char* all = "SELECT ?s WHERE { GRAPH ?g { ?s ?p ?o } }";
  CHECK(qlever_query(engine, all, strlen(all), "tsv", 0, collect, &stopping, &error) == QLEVER_STOPPED,
        "a sink stops its query");
  qlever_free(error);
  error = NULL;

  const char* insert = "INSERT DATA { GRAPH <http://example.org/g> { <http://example.org/x> <http://example.org/p> <http://example.org/y> } }";
  CHECK(qlever_update(engine, insert, strlen(insert), &json, &error) == QLEVER_OK && strstr(json, "\"after\"") != NULL,
        "an update reports its counts");
  qlever_free(json);
  json = NULL;
  CHECK(counted(engine, 11), "the update is seen");

  snprintf(path, sizeof(path), "%s/add.nt", argv[2]);
  FILE* file = fopen(path, "w");
  fprintf(file, "<http://example.org/a> <http://example.org/p> <http://example.org/b> .\n");
  fprintf(file, "<http://example.org/c> <http://example.org/p> <http://example.org/d> .\n");
  fclose(file);
  CHECK(qlever_write_file(engine, path, "nt", "http://example.org/g", 0, &json, &error) == QLEVER_OK,
        "a file is written into a graph");
  qlever_free(json);
  json = NULL;
  CHECK(counted(engine, 13), "the file's triples are seen");
  CHECK(qlever_counts(engine, &json, &error) == QLEVER_OK && strstr(json, "\"inserted\":3") != NULL,
        "the delta counts three insertions");
  qlever_free(json);
  json = NULL;

  CHECK(qlever_rebuild(engine, "bogus", &json, &error) == QLEVER_INVALID, "an unknown keep-previous policy is refused");
  qlever_free(error);
  error = NULL;
  CHECK(qlever_rebuild(engine, "none", &json, &error) == QLEVER_OK, "a rebuild merges the delta");
  qlever_free(json);
  json = NULL;
  CHECK(counted(engine, 13), "nothing is lost by the rebuild");

  const char* view = "SELECT ?s ?o WHERE { GRAPH ?g { ?s <http://example.org/p> ?o } }";
  CHECK(qlever_write_view(engine, "pairs", view, strlen(view), &error) == QLEVER_OK, "a view is written");
  CHECK(qlever_load_view(engine, "pairs", &error) == QLEVER_OK, "and loaded");

  qlever_close(engine);
  engine = NULL;
  CHECK(qlever_open(base, 1.0, 1, &engine, &error) == QLEVER_OK && counted(engine, 13), "a reopened index keeps every write");
  qlever_close(engine);
  qlever_close(NULL);
  qlever_free(NULL);

  if (error != NULL) {
    fprintf(stderr, "last error: %s\n", error);
    qlever_free(error);
  }
  fprintf(stderr, failures == 0 ? "qlever_c_test: all passed\n" : "qlever_c_test: %d failed\n", failures);
  return failures == 0 ? 0 : 1;
}
