#![allow(unsafe_code)]

// The one place this crate crosses into C: libqlever_c's `qlever.h`, loaded at run time.

use crate::error::Error;
use std::ffi::{CStr, CString, c_char, c_int, c_void};
use std::path::Path;
use std::ptr::NonNull;

// The `qlever.h` revision these bindings were written against.
pub(crate) fn expected_abi() -> u32 {
    1
}

#[repr(C)]
pub(crate) struct RawEngine {
    _private: [u8; 0],
}

type Status = c_int;
type Sink = unsafe extern "C" fn(*mut c_void, *const c_char, usize) -> c_int;

#[derive(Clone, Copy)]
pub(crate) struct Functions {
    abi_version: unsafe extern "C" fn() -> u32,
    open: unsafe extern "C" fn(*const c_char, f64, c_int, *mut *mut RawEngine, *mut *mut c_char) -> Status,
    close: unsafe extern "C" fn(*mut RawEngine),
    query: unsafe extern "C" fn(
        *const RawEngine,
        *const c_char,
        usize,
        *const c_char,
        u64,
        Sink,
        *mut c_void,
        *mut *mut c_char,
    ) -> Status,
    update: unsafe extern "C" fn(*mut RawEngine, *const c_char, usize, *mut *mut c_char, *mut *mut c_char) -> Status,
    write_file: unsafe extern "C" fn(
        *mut RawEngine,
        *const c_char,
        *const c_char,
        *const c_char,
        c_int,
        *mut *mut c_char,
        *mut *mut c_char,
    ) -> Status,
    counts: unsafe extern "C" fn(*const RawEngine, *mut *mut c_char, *mut *mut c_char) -> Status,
    rebuild: unsafe extern "C" fn(*mut RawEngine, *const c_char, *mut *mut c_char, *mut *mut c_char) -> Status,
    write_view: unsafe extern "C" fn(*mut RawEngine, *const c_char, *const c_char, usize, *mut *mut c_char) -> Status,
    load_view: unsafe extern "C" fn(*const RawEngine, *const c_char, *mut *mut c_char) -> Status,
    free: unsafe extern "C" fn(*mut c_char),
}

impl std::fmt::Debug for Functions {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
    ) -> std::fmt::Result {
        f.debug_struct("Functions").finish_non_exhaustive()
    }
}

fn symbol<T: Copy>(
    library: &libloading::Library,
    name: &[u8],
) -> Result<T, Error> {
    // SAFETY: each name is looked up with the signature `qlever.h` declares for it, and the pointer is only used
    // while `library`, which the `Library` holding these functions owns, stays loaded.
    unsafe { library.get::<T>(name) }.map(|s| *s).map_err(|e| Error::Load(e.to_string()))
}

pub(crate) fn loaded(path: &Path) -> Result<(libloading::Library, Functions), Error> {
    // SAFETY: loading runs the library's initializers; libqlever_c's are QLever's own static constructors.
    let library = unsafe { libloading::Library::new(path) }.map_err(|e| Error::Load(e.to_string()))?;
    let functions = Functions {
        abi_version: symbol(&library, b"qlever_abi_version\0")?,
        open: symbol(&library, b"qlever_open\0")?,
        close: symbol(&library, b"qlever_close\0")?,
        query: symbol(&library, b"qlever_query\0")?,
        update: symbol(&library, b"qlever_update\0")?,
        write_file: symbol(&library, b"qlever_write_file\0")?,
        counts: symbol(&library, b"qlever_counts\0")?,
        rebuild: symbol(&library, b"qlever_rebuild\0")?,
        write_view: symbol(&library, b"qlever_write_view\0")?,
        load_view: symbol(&library, b"qlever_load_view\0")?,
        free: symbol(&library, b"qlever_free\0")?,
    };
    Ok((library, functions))
}

pub(crate) fn abi_version(functions: &Functions) -> u32 {
    // SAFETY: takes nothing and returns a constant.
    unsafe { (functions.abi_version)() }
}

fn c_string(text: &str) -> Result<CString, Error> {
    CString::new(text).map_err(|_| Error::Invalid(format!("`{text}` holds a NUL byte")))
}

fn taken(
    functions: &Functions,
    text: *mut c_char,
) -> Option<String> {
    let owned = NonNull::new(text)?;
    // SAFETY: the library returned this NUL-terminated string and hands its ownership to the caller.
    let read = unsafe { CStr::from_ptr(owned.as_ptr()) }.to_string_lossy().into_owned();
    // SAFETY: freed once, with the library's own `qlever_free`, as `qlever.h` requires.
    unsafe { (functions.free)(owned.as_ptr()) };
    Some(read)
}

fn outcome(
    functions: &Functions,
    status: Status,
    error: *mut c_char,
) -> Result<(), Error> {
    let message = taken(functions, error).unwrap_or_default();
    match status {
        0 => Ok(()),
        1 => Err(Error::Invalid(message)),
        2 => Err(Error::Timeout(message)),
        3 => Err(Error::Stopped),
        _ => Err(Error::Failed(message)),
    }
}

fn answered(
    functions: &Functions,
    status: Status,
    json: *mut c_char,
    error: *mut c_char,
) -> Result<serde_json::Value, Error> {
    outcome(functions, status, error)?;
    let text = taken(functions, json).unwrap_or_default();
    serde_json::from_str(&text).map_err(|e| Error::Failed(format!("libqlever_c answered unreadable JSON: {e}")))
}

// An open engine: the pointer `qlever_open` returned, closed once when dropped.
#[derive(Debug)]
pub(crate) struct Handle {
    pub raw: NonNull<RawEngine>,
    pub functions: Functions,
}

// SAFETY: the engine behind the pointer serves any number of concurrent readers and serializes its writers
// (`EmbeddedEngine`'s contract); nothing here hands out the pointer itself.
unsafe impl Send for Handle {}
// SAFETY: as for `Send`.
unsafe impl Sync for Handle {}

impl Drop for Handle {
    fn drop(&mut self) {
        // SAFETY: `raw` came from `qlever_open` and is closed only here, once no call can be running on it.
        unsafe { (self.functions.close)(self.raw.as_ptr()) }
    }
}

pub(crate) fn open(
    functions: Functions,
    base_name: &Path,
    memory_gb: f64,
    persist_updates: bool,
) -> Result<Handle, Error> {
    let base = c_string(&base_name.to_string_lossy())?;
    let mut raw: *mut RawEngine = std::ptr::null_mut();
    let mut error: *mut c_char = std::ptr::null_mut();
    // SAFETY: `base` is NUL-terminated and outlives the call; `raw` and `error` are valid out-pointers.
    let status =
        unsafe { (functions.open)(base.as_ptr(), memory_gb, c_int::from(persist_updates), &mut raw, &mut error) };
    outcome(&functions, status, error)?;
    let raw = NonNull::new(raw).ok_or_else(|| Error::Failed("qlever_open returned no engine".into()))?;
    Ok(Handle { raw, functions })
}

pub(crate) type SinkFn<'a> = dyn FnMut(&[u8]) -> bool + 'a;

unsafe extern "C" fn trampoline(
    context: *mut c_void,
    data: *const c_char,
    length: usize,
) -> c_int {
    // SAFETY: `context` is the `&mut &mut SinkFn` that `query` passed, alive for the whole call.
    let sink = unsafe { &mut *context.cast::<&mut SinkFn<'_>>() };
    // SAFETY: the library passes `length` readable bytes at `data` for the duration of this call.
    let chunk = unsafe { std::slice::from_raw_parts(data.cast::<u8>(), length) };
    let kept = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| sink(chunk))).unwrap_or(false);
    c_int::from(!kept)
}

pub(crate) fn query(
    handle: &Handle,
    sparql: &str,
    format: &str,
    timeout_ms: u64,
    sink: &mut SinkFn<'_>,
) -> Result<(), Error> {
    let format = c_string(format)?;
    let mut error: *mut c_char = std::ptr::null_mut();
    let mut sink: &mut SinkFn<'_> = sink;
    let context = (&mut sink as *mut &mut SinkFn<'_>).cast::<c_void>();
    // SAFETY: the SPARQL text is passed with its length; `format` is NUL-terminated; `context` points at `sink`,
    // which outlives the call, and `trampoline` reads it back with the same type.
    let status = unsafe {
        (handle.functions.query)(
            handle.raw.as_ptr(),
            sparql.as_ptr().cast(),
            sparql.len(),
            format.as_ptr(),
            timeout_ms,
            trampoline,
            context,
            &mut error,
        )
    };
    outcome(&handle.functions, status, error)
}

pub(crate) fn update(
    handle: &Handle,
    sparql: &str,
) -> Result<serde_json::Value, Error> {
    let (mut json, mut error): (*mut c_char, *mut c_char) = (std::ptr::null_mut(), std::ptr::null_mut());
    // SAFETY: the SPARQL text is passed with its length; `json` and `error` are valid out-pointers.
    let status = unsafe {
        (handle.functions.update)(handle.raw.as_ptr(), sparql.as_ptr().cast(), sparql.len(), &mut json, &mut error)
    };
    answered(&handle.functions, status, json, error)
}

pub(crate) fn write_file(
    handle: &Handle,
    path: &Path,
    format: &str,
    default_graph: &str,
    delete: bool,
) -> Result<serde_json::Value, Error> {
    let (path, format, graph) = (c_string(&path.to_string_lossy())?, c_string(format)?, c_string(default_graph)?);
    let (mut json, mut error): (*mut c_char, *mut c_char) = (std::ptr::null_mut(), std::ptr::null_mut());
    // SAFETY: every string is NUL-terminated and outlives the call; `json` and `error` are valid out-pointers.
    let status = unsafe {
        (handle.functions.write_file)(
            handle.raw.as_ptr(),
            path.as_ptr(),
            format.as_ptr(),
            graph.as_ptr(),
            c_int::from(delete),
            &mut json,
            &mut error,
        )
    };
    answered(&handle.functions, status, json, error)
}

pub(crate) fn counts(handle: &Handle) -> Result<serde_json::Value, Error> {
    let (mut json, mut error): (*mut c_char, *mut c_char) = (std::ptr::null_mut(), std::ptr::null_mut());
    // SAFETY: `json` and `error` are valid out-pointers.
    let status = unsafe { (handle.functions.counts)(handle.raw.as_ptr(), &mut json, &mut error) };
    answered(&handle.functions, status, json, error)
}

pub(crate) fn rebuild(
    handle: &Handle,
    keep_previous: &str,
) -> Result<serde_json::Value, Error> {
    let keep = c_string(keep_previous)?;
    let (mut json, mut error): (*mut c_char, *mut c_char) = (std::ptr::null_mut(), std::ptr::null_mut());
    // SAFETY: `keep` is NUL-terminated and outlives the call; `json` and `error` are valid out-pointers.
    let status = unsafe { (handle.functions.rebuild)(handle.raw.as_ptr(), keep.as_ptr(), &mut json, &mut error) };
    answered(&handle.functions, status, json, error)
}

pub(crate) fn write_view(
    handle: &Handle,
    name: &str,
    sparql: &str,
) -> Result<(), Error> {
    let name = c_string(name)?;
    let mut error: *mut c_char = std::ptr::null_mut();
    // SAFETY: `name` is NUL-terminated and outlives the call, the SPARQL text is passed with its length.
    let status = unsafe {
        (handle.functions.write_view)(
            handle.raw.as_ptr(),
            name.as_ptr(),
            sparql.as_ptr().cast(),
            sparql.len(),
            &mut error,
        )
    };
    outcome(&handle.functions, status, error)
}

pub(crate) fn load_view(
    handle: &Handle,
    name: &str,
) -> Result<(), Error> {
    let name = c_string(name)?;
    let mut error: *mut c_char = std::ptr::null_mut();
    // SAFETY: `name` is NUL-terminated and outlives the call; `error` is a valid out-pointer.
    let status = unsafe { (handle.functions.load_view)(handle.raw.as_ptr(), name.as_ptr(), &mut error) };
    outcome(&handle.functions, status, error)
}
