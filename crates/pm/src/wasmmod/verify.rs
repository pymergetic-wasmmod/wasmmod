//! Thin wraps for `wasmmod` / `verify` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_get_verify_enabled`.
pub fn get_verify_enabled() -> bool {
    unsafe { ffi::pm_wasmmod_get_verify_enabled() }
}

/// `pm_wasmmod_set_verify_enabled`.
pub fn set_verify_enabled(on: bool) -> () {
    unsafe { ffi::pm_wasmmod_set_verify_enabled(on) }
}

/// `pm_wasmmod_trust_add`.
pub fn trust_add(key: *const u8, key_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_trust_add(key, key_len) }
}

/// `pm_wasmmod_trust_clear`.
pub fn trust_clear() -> () {
    unsafe { ffi::pm_wasmmod_trust_clear() }
}

/// `pm_wasmmod_trust_count`.
pub fn trust_count() -> usize {
    unsafe { ffi::pm_wasmmod_trust_count() }
}

/// `pm_wasmmod_verify_bytes`.
pub fn verify_bytes(bytes: *const u8, len: u32, path_hint: *const core::ffi::c_char, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_verify_bytes(bytes, len, path_hint, errbuf, errbuf_len) }
}
