//! Thin wraps for `wasmmod` / `fetch` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_fetch`.
pub fn fetch(url: *const core::ffi::c_char, out_bytes: *mut *mut u8, out_len: *mut u32, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_fetch(url, out_bytes, out_len, errbuf, errbuf_len) }
}
