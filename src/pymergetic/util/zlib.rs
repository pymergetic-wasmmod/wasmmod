//! pymergetic.util.zlib — barrel: not optional, this is what makes
//! `pymergetic::util::zlib` resolve at all (Rust's own `use`/`mod` needs
//! a real item at this path, matching path == module). Reexports the
//! bindgen-mirror in zlib/__exports__.rs under the module's real name.
//! Reexport only, not a second impl — the real logic is zlib/__impl__.c.
#[path = "zlib/__exports__.rs"]
mod export;
pub use export::*;
