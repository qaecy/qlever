"""Read latency of qlever-cli (a process per query) against qlever_embedded (an open index),
before and during a rebuild, on a synthetic index with an update layer.

    QLEVER_BUILD=/qlever/build python3 test/python/bench_qlever_embedded.py <dir> [base] [delta]
"""
import json
import os
import resource
import statistics
import subprocess
import sys
import threading
import time

BUILD = os.environ.get("QLEVER_BUILD", "build")
sys.path.insert(0, BUILD)
import qlever_embedded  # noqa: E402

EX = "http://example.org/"
QUERY = f"SELECT ?o WHERE {{ GRAPH ?g {{ <{EX}s42> <{EX}p3> ?o }} }}"


def write_quads(path: str, n: int, offset: int) -> None:
    with open(path, "w") as f:
        f.writelines(f'<{EX}s{i // 5}> <{EX}p{i % 5}> "{i}" <{EX}g{i % 7}> .\n' for i in range(offset, offset + n))


def timed(fn) -> float:
    started = time.monotonic()
    fn()
    return time.monotonic() - started


def summary(name: str, samples: list) -> None:
    ms = sorted(map(lambda s: s * 1000, samples))
    print(f"{name:34s} n={len(ms):4d} p50={statistics.median(ms):8.1f} ms  p95={ms[int(len(ms) * 0.95) - 1]:8.1f} ms  max={ms[-1]:8.1f} ms")


def main(directory: str, base: int, delta: int) -> None:
    os.makedirs(directory, exist_ok=True)
    write_quads(f"{directory}/base.nq", base, 0)
    spec = {"index_name": "full", "index_directory": directory, "input_files": [{"path": f"{directory}/base.nq", "format": "nq"}]}
    print(f"build index of {base} triples: {timed(lambda: subprocess.run([f'{BUILD}/qlever-cli', 'build-index', json.dumps(spec)], check=True, capture_output=True)):.1f} s")
    basename = f"{directory}/full"
    write_quads(f"{directory}/delta.nq", delta, base)
    print(f"cli write of {delta} triples: {timed(lambda: subprocess.run([f'{BUILD}/qlever-cli', 'write', basename, 'nq', f'{directory}/delta.nq'], check=True, capture_output=True)):.1f} s")
    print(f"update layer: {os.path.getsize(basename + '.update-triples') / 2**20:.1f} MB")
    cli = [timed(lambda: subprocess.run([f"{BUILD}/qlever-cli", "query", basename, QUERY, "sparql-json"], check=True, capture_output=True)) for _ in range(10)]
    summary("cli query (spawn per query)", cli)
    rss0 = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    engine = None

    def open_engine() -> None:
        nonlocal engine
        engine = qlever_embedded.Engine(basename, 4.0)

    print(f"embedded open (replay once): {timed(open_engine):.2f} s, peak rss +{(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss - rss0) / 1024:.0f} MB")
    sink = []
    query = lambda: engine.query(QUERY, "sparql-json", sink.append)  # noqa: E731
    summary("embedded query (open index)", [timed(query) for _ in range(200)])
    during, stop = [], threading.Event()

    def reader() -> None:
        while not stop.is_set():
            during.append(timed(query))

    readers = [threading.Thread(target=reader) for _ in range(4)]
    [t.start() for t in readers]
    result = {}
    print(f"embedded rebuild with 4 readers running: {timed(lambda: result.update(engine.rebuild('none'))):.1f} s ({result.get('buildMs')} ms build)")
    stop.set()
    [t.join() for t in readers]
    summary("embedded query during rebuild", during)
    summary("embedded query after rebuild", [timed(query) for _ in range(200)])
    print(f"counts after rebuild: {engine.counts()}")
    print(f"peak rss of the process: {resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024:.0f} MB")


if __name__ == "__main__":
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 5_000_000, int(sys.argv[3]) if len(sys.argv) > 3 else 500_000)
