# qlever-embedded (Rust)

QLever held open in a Rust process, the counterpart of the Python module in `../python`. It wraps `libqlever_c`, the C
ABI in `../c` (`qlever.h`), so the C interface and its Rust binding change together, in this repository.

- **Loading.** The library is loaded at run time (`load(path)`) and its ABI version checked, so a crate that depends on
  this one builds where the library does not exist.
- **Unsafe code.** Only `src/ffi.rs` has unsafe code, every block with its `SAFETY` reason. Errors arrive typed: the
  status of each call becomes an `Error` (`Invalid`, `Timeout`, `Stopped`, `Failed`).
- **API.** `open` replays the index's update layer once, and the `Engine` closes it when dropped. `query` streams the
  answer to a sink in chunks of about 1 MB; the sink returns `false` to stop. `update`, `write_file` and `rebuild` are
  atomic; `counts`, `write_view`, `load_view`.
- **jemalloc.** QLever links jemalloc, which a running process cannot load, so the host preloads it:
  `LD_PRELOAD=/usr/lib/<arch>-linux-gnu/libjemalloc.so.2`.

A Rust workspace depends on it by git, pinned to a commit:
`qlever-embedded = { git = "https://github.com/qaecy/qlever", rev = "<commit>" }`.

Tests, against the library and `qlever-cli` from a `builder-ubuntu` build (`-DQLEVER_C_BINDINGS=ON`):

```sh
src/libqlever/rust/tests/container/run.sh            # build-ubuntu by default
```
