//! pymergetic.wasmmod.net.cdn — barrel. Real logic is cdn/__impl__.c.
#[path = "cdn/__exports__.rs"]
mod export;
pub use export::*;
