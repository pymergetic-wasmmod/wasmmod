//! pymergetic.util.pysample — barrel: not optional, this is what makes
//! `pymergetic::util::pysample` resolve at all (Rust's own `use`/`mod`
//! needs a real item at this path, matching path == module). Reexports
//! the bindgen-mirror in pysample/__exports__.rs under the module's real
//! name. Reexport only, not a second impl — the real body is Python
//! (see pysample/__init__.py).
#[path = "pysample/__exports__.rs"]
mod export;
pub use export::*;
