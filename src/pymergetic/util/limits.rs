//! pymergetic.util.limits — barrel: not optional, this is what makes
//! `pymergetic::util::limits` resolve at all (Rust's own `use`/`mod` needs a
//! real item at this path, matching path == module). Reexports the
//! bindgen-mirror in limits/__exports__.rs under the module's real name.
//! Reexport only, not a second impl — the real logic is limits/__impl__.c.
#[path = "limits/__exports__.rs"]
mod export;
pub use export::*;
