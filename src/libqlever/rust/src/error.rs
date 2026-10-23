#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum Error {
    #[error("libqlever_c could not be loaded: {0}")]
    Load(String),
    #[error("libqlever_c speaks ABI {found}, this crate {expected}")]
    Abi { found: u32, expected: u32 },
    #[error("{0}")]
    Invalid(String),
    #[error("{0}")]
    Timeout(String),
    #[error("the query was stopped by its sink")]
    Stopped,
    #[error("{0}")]
    Failed(String),
}
