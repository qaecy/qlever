use crate::error::Error;
use crate::ffi::{self, Handle};
use crate::library::Library;
use serde_json::Value;
use std::path::Path;
use std::sync::Arc;
use std::time::Duration;

// One index held open in this process; it keeps its library loaded for as long as it lives.
#[derive(Debug)]
pub struct Engine {
    handle: Handle,
    _library: Arc<Library>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum KeepPrevious {
    All,
    None,
    OriginalOnly,
    MostRecentOnly,
    OriginalAndMostRecent,
}

fn keep_token(keep: KeepPrevious) -> &'static str {
    match keep {
        KeepPrevious::All => "all",
        KeepPrevious::None => "none",
        KeepPrevious::OriginalOnly => "original-only",
        KeepPrevious::MostRecentOnly => "most-recent-only",
        KeepPrevious::OriginalAndMostRecent => "original-and-most-recent",
    }
}

// Opening replays the index's update layer once; every later call reads it from memory.
pub fn open(
    library: &Arc<Library>,
    base_name: &Path,
    memory_gb: f64,
    persist_updates: bool,
) -> Result<Engine, Error> {
    let handle = ffi::open(library.functions, base_name, memory_gb, persist_updates)?;
    Ok(Engine { handle, _library: Arc::clone(library) })
}

// The serialized answer goes to `sink` in chunks of about 1 MB; a sink returning `false` stops the query.
pub fn query(
    engine: &Engine,
    sparql: &str,
    format: &str,
    timeout: Option<Duration>,
    sink: &mut dyn FnMut(&[u8]) -> bool,
) -> Result<(), Error> {
    let timeout_ms = timeout.map_or(0, |t| u64::try_from(t.as_millis()).unwrap_or(u64::MAX).max(1));
    ffi::query(&engine.handle, sparql, format, timeout_ms, sink)
}

pub fn update(
    engine: &Engine,
    sparql: &str,
) -> Result<Value, Error> {
    ffi::update(&engine.handle, sparql)
}

pub fn write_file(
    engine: &Engine,
    path: &Path,
    format: &str,
    default_graph: Option<&str>,
    delete: bool,
) -> Result<Value, Error> {
    ffi::write_file(&engine.handle, path, format, default_graph.unwrap_or_default(), delete)
}

pub fn counts(engine: &Engine) -> Result<Value, Error> {
    ffi::counts(&engine.handle)
}

pub fn rebuild(
    engine: &Engine,
    keep_previous: KeepPrevious,
) -> Result<Value, Error> {
    ffi::rebuild(&engine.handle, keep_token(keep_previous))
}

pub fn write_view(
    engine: &Engine,
    name: &str,
    sparql: &str,
) -> Result<(), Error> {
    ffi::write_view(&engine.handle, name, sparql)
}

pub fn load_view(
    engine: &Engine,
    name: &str,
) -> Result<(), Error> {
    ffi::load_view(&engine.handle, name)
}
