//! Thin wraps for `wasmmod` / `zlib` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_zlib_inflate`.
pub fn zlib_inflate(in_: *const u8, in_len: u32, out: *mut *mut u8, out_len: *mut u32, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_zlib_inflate(in_, in_len, out, out_len, errbuf, errbuf_len) }
}
