//! pymergetic.util.version — thin reexport (path == module).
#[path = "version/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
