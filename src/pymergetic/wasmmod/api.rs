//! pymergetic.wasmmod.api — thin reexport (path == module).
#[path = "api/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
