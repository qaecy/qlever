use crate::error::Error;
use crate::ffi::{self, Functions};
use std::path::Path;

// libqlever_c, loaded once at the edge and shared by every engine opened from it.
pub struct Library {
    pub(crate) functions: Functions,
    _library: libloading::Library,
}

impl std::fmt::Debug for Library {
    fn fmt(
        &self,
        f: &mut std::fmt::Formatter<'_>,
    ) -> std::fmt::Result {
        f.debug_struct("Library").finish_non_exhaustive()
    }
}

fn checked(
    library: libloading::Library,
    functions: Functions,
) -> Result<Library, Error> {
    let (found, expected) = (ffi::abi_version(&functions), ffi::expected_abi());
    match found == expected {
        true => Ok(Library { functions, _library: library }),
        false => Err(Error::Abi { found, expected }),
    }
}

// QLever links jemalloc, which a running process cannot load: the host preloads it (`LD_PRELOAD`).
pub fn load(path: &Path) -> Result<Library, Error> {
    let (library, functions) = ffi::loaded(path)?;
    checked(library, functions)
}
