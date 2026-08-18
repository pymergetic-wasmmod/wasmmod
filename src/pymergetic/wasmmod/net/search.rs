//! pymergetic.wasmmod.net.search — CDN pack list / search / filter (impl = rs).
//!
//! Owns the JSON index parsing so every seat (host C, Rust, µPy) shares the
//! same search/filter semantics over the fetched index. Fetching stays on the
//! `net.cdn` C card (`pm_wasmmod_net_cdn_fetch_index`); parsing + matching
//! live here.
#[path = "search/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
