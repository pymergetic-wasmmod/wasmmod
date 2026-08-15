//! pymergetic.wasmmod.io — barrel: path == module. Reexports the
//! bindgen-mirror in io/__exports__.rs. Real logic is io/__impl__.c.
#[path = "io/__exports__.rs"]
mod export;
pub use export::*;
