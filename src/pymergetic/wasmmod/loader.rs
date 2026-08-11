//! pymergetic.wasmmod.loader — thin reexport. The real impl lives in
//! loader/__impl__.rs (tucked away with this module's faces); this
//! file only exists so `pymergetic::wasmmod::loader` resolves at the
//! path "path == module" expects.
#[path = "loader/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
