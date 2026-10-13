"""Tests for the `qlever_embedded` module, against a small index built with `qlever-cli`.

Run with the module and the CLI from one build directory:
    QLEVER_BUILD=/qlever/build python3 -m pytest test/python
"""
import json
import os
import subprocess
import sys
import threading
import time

import pytest

BUILD = os.environ.get("QLEVER_BUILD", "build")
sys.path.insert(0, os.path.join(BUILD, "lib"))
import qlever_embedded  # noqa: E402

EX = "http://example.org/"
GRAPH = f"{EX}g"


def triples(n: int, offset: int = 0) -> str:
    return "".join(f"<{EX}s{i}> <{EX}p> <{EX}o{i}> <{GRAPH}> .\n" for i in range(offset, offset + n))


@pytest.fixture
def index(tmp_path):
    source = tmp_path / "data.nq"
    source.write_text(triples(1000))
    spec = {"index_name": "full", "index_directory": str(tmp_path), "input_files": [{"path": str(source), "format": "nq"}]}
    subprocess.run([f"{BUILD}/qlever-cli", "build-index", json.dumps(spec)], check=True, capture_output=True)
    return str(tmp_path / "full")


def run(engine, sparql: str, fmt: str = "") -> bytes:
    chunks = []
    engine.query(sparql, fmt, chunks.append)
    return b"".join(chunks)


def count(engine) -> int:
    result = json.loads(run(engine, "SELECT (COUNT(*) AS ?n) WHERE { GRAPH ?g { ?s ?p ?o } }"))
    return int(result["results"]["bindings"][0]["n"]["value"])


def test_queries_in_every_format(index):
    engine = qlever_embedded.Engine(index, 1.0)
    assert count(engine) == 1000
    assert run(engine, f"SELECT ?o WHERE {{ GRAPH ?g {{ <{EX}s1> ?p ?o }} }}", "tsv").splitlines()[1] == f"<{EX}o1>".encode()
    construct = f"CONSTRUCT {{ ?s ?p ?o }} WHERE {{ GRAPH ?g {{ ?s ?p ?o }} }} LIMIT 3"
    assert len(run(engine, construct, "nt").splitlines()) == 3
    assert all(line.endswith(b" <http://default.graph/> .") for line in run(engine, construct, "nq").splitlines())
    with pytest.raises(ValueError):
        run(engine, construct, "csv")


def test_a_file_write_is_atomic_and_counted(index, tmp_path):
    engine = qlever_embedded.Engine(index, 1.0)
    good = tmp_path / "add.nq"
    good.write_text(triples(50, offset=1000))
    result = engine.write_file(str(good), "nq")
    assert result["processed"] == 50 and engine.counts()["delta"]["inserted"] == 50
    bad = tmp_path / "bad.nq"
    bad.write_text(triples(20, offset=2000) + f'<{EX}a> <{EX}p> "unterminated <{GRAPH}> .\n' + triples(20, offset=3000))
    with pytest.raises(RuntimeError):
        engine.write_file(str(bad), "nq")
    assert engine.counts()["delta"]["inserted"] == 50
    assert count(engine) == 1050


def test_updates_persist_and_survive_a_reopen(index):
    engine = qlever_embedded.Engine(index, 1.0)
    result = engine.update(f"INSERT DATA {{ GRAPH <{GRAPH}> {{ <{EX}x> <{EX}p> <{EX}y> }} }}")
    assert result["after"]["inserted"] == 1
    with pytest.raises(RuntimeError):
        engine.update("INSERT DATA { not sparql")
    del engine
    assert qlever_embedded.Engine(index, 1.0).counts()["delta"]["inserted"] == 1


def test_a_rebuild_merges_the_delta_while_reads_and_writes_go_on(index, tmp_path):
    engine = qlever_embedded.Engine(index, 1.0)
    first = tmp_path / "first.nq"
    first.write_text(triples(500, offset=1000))
    engine.write_file(str(first), "nq")
    seen, errors, stop = [], [], threading.Event()

    def reader() -> None:
        while not stop.is_set():
            try:
                seen.append(count(engine))
            except Exception as e:  # noqa: BLE001
                errors.append(e)

    readers = [threading.Thread(target=reader) for _ in range(4)]
    [t.start() for t in readers]
    def rebuilder() -> None:
        try:
            seen.append(engine.rebuild("none"))
        except Exception as e:  # noqa: BLE001
            errors.append(e)

    rebuild = threading.Thread(target=rebuilder)
    rebuild.start()
    time.sleep(0.05)
    late = tmp_path / "late.nq"
    late.write_text(triples(10, offset=5000))
    engine.write_file(str(late), "nq")
    rebuild.join()
    stop.set()
    [t.join() for t in readers]
    assert not errors
    assert count(engine) == 1510
    assert engine.counts()["triples"] >= 1500
    assert all(isinstance(n, dict) or n in (1500, 1510) for n in seen)


def test_an_exception_in_the_sink_stops_the_query(index):
    engine = qlever_embedded.Engine(index, 1.0)

    def sink(_: bytes) -> None:
        raise KeyError("stop")

    with pytest.raises(KeyError):
        engine.query("SELECT * WHERE { GRAPH ?g { ?s ?p ?o } }", "tsv", sink)
    assert count(engine) == 1000


def test_a_query_past_its_deadline_raises_and_the_engine_keeps_serving(index):
    engine = qlever_embedded.Engine(index, 4.0)
    slow = "SELECT ?a ?d WHERE { GRAPH ?g { ?a ?b ?c . ?d ?e ?f } } ORDER BY DESC(CONCAT(STR(?a), STR(?d), STR(?c), STR(?f)))"
    started = time.monotonic()
    with pytest.raises(qlever_embedded.QueryTimeout) as raised:
        engine.query(slow, "sparql-json", lambda chunk: None, 50)
    assert isinstance(raised.value, TimeoutError) and time.monotonic() - started < 5
    chunks = []
    engine.query("SELECT (COUNT(*) AS ?n) WHERE { GRAPH ?g { ?s ?p ?o } }", "sparql-json", chunks.append, 10_000)
    assert json.loads(b"".join(chunks))["results"]["bindings"][0]["n"]["value"] == "1000"


def test_an_engine_can_be_weakly_referenced(index):
    import gc
    import weakref
    engine = qlever_embedded.Engine(index, 1.0)
    ref = weakref.ref(engine)
    assert ref() is engine
    del engine
    gc.collect()
    assert ref() is None


def test_the_module_exports_only_its_entry_point():
    # Statically linked abseil and re2 must not be visible to a host that loads its own (pyarrow does).
    out = subprocess.run(["nm", "-D", "--defined-only", qlever_embedded.__file__], check=True, capture_output=True, text=True).stdout
    exported = {line.split()[-1] for line in out.splitlines() if line.strip()}
    assert exported == {"PyInit_qlever_embedded"}


def test_a_values_clause_of_literals_parses(index):
    engine = qlever_embedded.Engine(index)
    result = json.loads(run(engine, f'SELECT ?s WHERE {{ VALUES ?v {{ "a" "b" ".pdf" }} ?s <{EX}p> <{EX}o1> }}'))
    assert [b["s"]["value"] for b in result["results"]["bindings"]] == [f"{EX}s1"] * 3
