//! pymergetic.types — barrel: not optional, this is what makes
//! `pymergetic::types` resolve at all (Rust's own `use`/`mod` needs a
//! real item at this path, matching path == module). Reexports the
//! bindgen-mirror in types/__exports__.rs under the module's real
//! name. Reexport only, not a second impl — the real logic is
//! types/__impl__.c.
#[path = "types/__exports__.rs"]
mod export;
pub use export::*;
