//! pymergetic.wasmmod.registry — thin reexport. The real impl lives in
//! registry/__impl__.rs (tucked away with this module's faces); this
//! file only exists so `pymergetic::wasmmod::registry` resolves at the
//! path "path == module" expects.
#[path = "registry/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
