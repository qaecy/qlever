// Helpers outside #[test] fns are test code too; clippy's test allowance does not reach them here.
#![allow(clippy::unwrap_used, clippy::expect_used)]

// Against the real libqlever_c and qlever-cli, so these run in the qlever toolchain container (see README.md):
// QLEVER_C_LIBRARY and QLEVER_CLI name them.

use qlever_embedded::{Error, KeepPrevious, counts, load, open, query, rebuild, update, write_file, write_view};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;

fn library() -> Arc<qlever_embedded::Library> {
    Arc::new(load(Path::new(&std::env::var("QLEVER_C_LIBRARY").expect("QLEVER_C_LIBRARY names libqlever_c"))).unwrap())
}

fn index(dir: &Path) -> PathBuf {
    let data: String = (0..10)
        .map(|i| {
            format!(
                "<http://example.org/s{i}> <http://example.org/p> <http://example.org/o{i}> <http://example.org/g> .\n"
            )
        })
        .collect();
    std::fs::write(dir.join("data.nq"), data).unwrap();
    let spec = serde_json::json!({"index_name": "full", "index_directory": dir, "input_files": [{"path": dir.join("data.nq"), "format": "nq"}]});
    let cli = std::env::var("QLEVER_CLI").expect("QLEVER_CLI names qlever-cli");
    let built = std::process::Command::new(cli).arg("build-index").arg(spec.to_string()).output().unwrap();
    assert!(built.status.success(), "{}", String::from_utf8_lossy(&built.stderr));
    dir.join("full")
}

fn answer(
    engine: &qlever_embedded::Engine,
    sparql: &str,
    format: &str,
) -> Result<String, Error> {
    let mut collected = Vec::new();
    query(engine, sparql, format, Some(Duration::from_secs(10)), &mut |chunk: &[u8]| {
        collected.extend_from_slice(chunk);
        true
    })?;
    Ok(String::from_utf8(collected).unwrap())
}

fn count(engine: &qlever_embedded::Engine) -> u64 {
    let answer = answer(engine, "SELECT (COUNT(*) AS ?n) WHERE { GRAPH ?g { ?s ?p ?o } }", "sparql-json").unwrap();
    let parsed: serde_json::Value = serde_json::from_str(&answer).unwrap();
    parsed["results"]["bindings"][0]["n"]["value"].as_str().unwrap().parse().unwrap()
}

#[test]
#[ignore = "needs libqlever_c and qlever-cli: run in the qlever toolchain container"]
fn an_open_index_answers_writes_rebuilds_and_reopens() {
    let dir = tempfile::tempdir().unwrap();
    let base = index(dir.path());
    let library = library();
    let engine = open(&library, &base, 1.0, true).unwrap();
    assert_eq!(count(&engine), 10);
    assert!(
        answer(&engine, "SELECT ?s WHERE { GRAPH ?g { ?s ?p ?o } } ORDER BY ?s LIMIT 1", "tsv")
            .unwrap()
            .contains("<http://example.org/s0>")
    );
    assert!(matches!(
        answer(&engine, "CONSTRUCT { ?s ?p ?o } WHERE { GRAPH ?g { ?s ?p ?o } }", "csv"),
        Err(Error::Invalid(_))
    ));
    assert!(matches!(answer(&engine, "SELECT * WHERE { not sparql", ""), Err(Error::Failed(_))));
    let stopped = query(&engine, "SELECT ?s WHERE { GRAPH ?g { ?s ?p ?o } }", "tsv", None, &mut |_: &[u8]| false);
    assert_eq!(stopped, Err(Error::Stopped));
    let updated = update(&engine, "INSERT DATA { GRAPH <http://example.org/g> { <http://example.org/x> <http://example.org/p> <http://example.org/y> } }").unwrap();
    assert_eq!(updated["after"]["inserted"], 1);
    std::fs::write(
        dir.path().join("add.nt"),
        "<http://example.org/a> <http://example.org/p> <http://example.org/b> .\n",
    )
    .unwrap();
    write_file(&engine, &dir.path().join("add.nt"), "nt", Some("http://example.org/g"), false).unwrap();
    assert_eq!(counts(&engine).unwrap()["delta"]["inserted"], 2);
    assert_eq!(count(&engine), 12);
    rebuild(&engine, KeepPrevious::None).unwrap();
    assert_eq!(count(&engine), 12);
    write_view(&engine, "pairs", "SELECT ?s ?o WHERE { GRAPH ?g { ?s <http://example.org/p> ?o } }").unwrap();
    drop(engine);
    assert_eq!(count(&open(&library, &base, 1.0, true).unwrap()), 12);
}

#[test]
#[ignore = "needs libqlever_c and qlever-cli: run in the qlever toolchain container"]
fn a_missing_index_fails_to_open_with_a_message() {
    let opened = open(&library(), Path::new("/nonexistent/full"), 1.0, true);
    assert!(matches!(opened, Err(Error::Failed(m)) if !m.is_empty()));
}

#[test]
fn a_library_that_is_not_there_is_a_load_error() {
    assert!(matches!(load(Path::new("/nonexistent/libqlever_c.so")), Err(Error::Load(_))));
}
