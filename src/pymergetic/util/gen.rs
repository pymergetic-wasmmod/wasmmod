//! pymergetic.util.gen — thin reexport (path == module).
#[path = "gen/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
