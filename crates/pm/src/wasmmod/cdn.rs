//! Thin wraps for `wasmmod` / `cdn` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_cdn_configure`.
pub fn cdn_configure(base_url: *const core::ffi::c_char, token: *const core::ffi::c_char) -> () {
    unsafe { ffi::pm_wasmmod_cdn_configure(base_url, token) }
}

/// `pm_wasmmod_cdn_fetch_index`.
pub fn cdn_fetch_index(channel: *const core::ffi::c_char, out_bytes: *mut *mut u8, out_len: *mut u32, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_cdn_fetch_index(channel, out_bytes, out_len, errbuf, errbuf_len) }
}

/// `pm_wasmmod_cdn_fetch_pack`.
pub fn cdn_fetch_pack(name: *const core::ffi::c_char, version: *const core::ffi::c_char, out_bytes: *mut *mut u8, out_len: *mut u32, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_cdn_fetch_pack(name, version, out_bytes, out_len, errbuf, errbuf_len) }
}

/// `pm_wasmmod_cdn_reset`.
pub fn cdn_reset() -> () {
    unsafe { ffi::pm_wasmmod_cdn_reset() }
}
