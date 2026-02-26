# Segmentation Fault: PREFIX in CONSTRUCT/DESCRIBE Queries

## Summary

QLever's core library (`libqlever`) crashes with exit code 139 when executing CONSTRUCT or DESCRIBE queries containing PREFIX declarations. SELECT queries with PREFIX work fine.

## Environment

- QLever version: latest main branch (2026-01-29)
- Platform: Alpine Linux / macOS

## Reproduction

```cpp
#include "QleverCliContext.h"

auto qlever = std::make_shared<qlever::QleverCliContext>(config);

// This crashes (exit 139):
std::string query = "PREFIX ex: <http://example.org#> "
                    "DESCRIBE <http://example.org/entity>";
std::string result = qlever->query(query, ad_utility::MediaType::turtle);
```

## Test Cases

| Query Type | Has PREFIX | Result          |
| ---------- | ---------- | --------------- |
| SELECT     | No         | ✅ Works        |
| SELECT     | Yes        | ✅ Works        |
| DESCRIBE   | No         | ✅ Works        |
| DESCRIBE   | Yes        | ❌ **Segfault** |
| CONSTRUCT  | No         | ✅ Works        |
| CONSTRUCT  | Yes        | ❌ **Segfault** |

## Analysis

Crash occurs in QLever's library during query processing:

- `QleverCliContext::query()` → `parseAndPlanQuery()` → **segfault**
- Crash bypasses exception handlers (no stack trace)
- Occurs even when PREFIX isn't used in query body
- Likely in `SparqlParser::parseQuery()` or `QueryPlanner::createExecutionTree()`

## Workaround

Use full IRIs instead of PREFIX:

```sparql
# Don't use:
PREFIX ex: <http://example.org#>
CONSTRUCT WHERE { ?s ex:property ?o }

# Use instead:
CONSTRUCT WHERE { ?s <http://example.org#property> ?o }
```

## Impact

This breaks SPARQL 1.1 compliance - PREFIX declarations are standard. CONSTRUCT/DESCRIBE queries are unusable with PREFIX syntax.
