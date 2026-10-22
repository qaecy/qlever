//  Copyright 2026 Qaecy.
//
//  You may not use this file except in compliance with the Apache 2.0 License,
//  which can be found in the `LICENSE` file at the root of the QLever project.

#include "libqlever/c/qlever.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include "libqlever/embedded/EmbeddedEngine.h"
#include "util/CancellationHandle.h"

struct qlever_engine {
  qlever::embedded::EmbeddedEngine engine;
};

namespace {

// Thrown by the sink adapter when the caller's sink asks to stop.
struct Stopped {};

char* copied(std::string_view text) {
  auto* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) {
    return nullptr;
  }
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  return out;
}

void report(char** error, std::string_view message) {
  if (error != nullptr) {
    *error = copied(message);
  }
}

// Every entry point runs its work through here, so no exception crosses the
// C boundary and each kind of failure has its own status.
template <typename Work>
qlever_status guarded(char** error, Work&& work) {
  try {
    work();
    return QLEVER_OK;
  } catch (const Stopped&) {
    report(error, "the query was stopped by its sink");
    return QLEVER_STOPPED;
  } catch (const ad_utility::CancellationException& e) {
    report(error, e.what());
    return QLEVER_TIMEOUT;
  } catch (const std::invalid_argument& e) {
    report(error, e.what());
    return QLEVER_INVALID;
  } catch (const std::exception& e) {
    report(error, e.what());
    return QLEVER_FAILED;
  } catch (...) {
    report(error, "an unknown error");
    return QLEVER_FAILED;
  }
}

template <typename Work>
qlever_status answered(char** json, char** error, Work&& work) {
  return guarded(error, [&] {
    std::string dumped = work().dump();
    if (json != nullptr) {
      *json = copied(dumped);
    }
  });
}

std::string text(const char* value) { return value == nullptr ? "" : value; }

std::string text(const char* value, size_t length) {
  return value == nullptr ? "" : std::string(value, length);
}

}  // namespace

extern "C" {

uint32_t qlever_abi_version(void) { return 1; }

qlever_status qlever_open(const char* base_name, double memory_gb,
                          int persist_updates, qlever_engine** engine,
                          char** error) {
  return guarded(error, [&] {
    if (engine == nullptr) {
      throw std::invalid_argument("qlever_open needs somewhere to put the engine");
    }
    *engine = new qlever_engine{qlever::embedded::EmbeddedEngine{
        text(base_name), memory_gb, persist_updates != 0}};
  });
}

void qlever_close(qlever_engine* engine) { delete engine; }

qlever_status qlever_query(const qlever_engine* engine, const char* sparql,
                           size_t sparql_length, const char* format,
                           uint64_t timeout_ms, qlever_sink sink,
                           void* context, char** error) {
  return guarded(error, [&] {
    engine->engine.query(
        text(sparql, sparql_length), text(format),
        [sink, context](std::string_view chunk) {
          if (sink(context, chunk.data(), chunk.size()) != 0) {
            throw Stopped{};
          }
        },
        std::chrono::milliseconds{timeout_ms});
  });
}

qlever_status qlever_update(qlever_engine* engine, const char* sparql,
                            size_t sparql_length, char** json, char** error) {
  return answered(json, error, [&] {
    return engine->engine.update(text(sparql, sparql_length));
  });
}

qlever_status qlever_write_file(qlever_engine* engine, const char* path,
                                const char* format, const char* default_graph,
                                int is_delete, char** json, char** error) {
  return answered(json, error, [&] {
    return engine->engine.writeFile(text(path), text(format),
                                    text(default_graph), is_delete != 0);
  });
}

qlever_status qlever_counts(const qlever_engine* engine, char** json,
                            char** error) {
  return answered(json, error, [&] { return engine->engine.counts(); });
}

qlever_status qlever_rebuild(qlever_engine* engine, const char* keep_previous,
                             char** json, char** error) {
  return answered(json, error, [&] {
    return engine->engine.rebuild(text(keep_previous));
  });
}

qlever_status qlever_write_view(qlever_engine* engine, const char* name,
                                const char* sparql, size_t sparql_length,
                                char** error) {
  return guarded(error, [&] {
    engine->engine.writeView(text(name), text(sparql, sparql_length));
  });
}

qlever_status qlever_load_view(const qlever_engine* engine, const char* name,
                               char** error) {
  return guarded(error, [&] { engine->engine.loadView(text(name)); });
}

void qlever_free(char* text) { std::free(text); }

}  // extern "C"
