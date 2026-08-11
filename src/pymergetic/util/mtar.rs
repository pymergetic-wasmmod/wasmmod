//! pymergetic.util.mtar — thin reexport. The real impl lives in
//! mtar/__impl__.rs (tucked away with this module's faces); this file only
//! exists so `pymergetic::util::mtar` resolves at the path "path == module"
//! expects. `#[unsafe(no_mangle)]` exports inside __impl__.rs are reachable
//! by symbol name regardless of nesting — this reexport is for real Rust
//! `use` paths, not FFI.
#[path = "mtar/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
