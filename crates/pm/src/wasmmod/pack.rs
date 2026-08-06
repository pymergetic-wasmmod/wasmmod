//! Thin wraps for `wasmmod` / `pack` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};
use core::ffi::CStr;

/// `pm_wasmmod_pack_arch`.
pub fn pack_arch(pack: *const pm_wasmmod_pack_t) -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_pack_arch(pack);
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}

/// `pm_wasmmod_pack_call_0`.
pub fn pack_call_0(pack: *mut pm_wasmmod_pack_t, func: *const core::ffi::c_char, out_result: *mut i32, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_pack_call_0(pack, func, out_result, errbuf, errbuf_len) }
}

/// `pm_wasmmod_pack_call_i32`.
pub fn pack_call_i32(pack: *mut pm_wasmmod_pack_t, func: *const core::ffi::c_char, args: *const i32, nargs: u32, out_result: *mut i32, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> bool {
    unsafe { ffi::pm_wasmmod_pack_call_i32(pack, func, args, nargs, out_result, errbuf, errbuf_len) }
}

/// `pm_wasmmod_pack_close`.
pub fn pack_close(pack: *mut pm_wasmmod_pack_t) -> () {
    unsafe { ffi::pm_wasmmod_pack_close(pack) }
}

/// `pm_wasmmod_pack_kind_str`.
pub fn pack_kind_str(pack: *const pm_wasmmod_pack_t) -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_pack_kind_str(pack);
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}

/// `pm_wasmmod_pack_linear`.
pub fn pack_linear(pack: *mut pm_wasmmod_pack_t, off: u32, n: u32, out: *mut *mut core::ffi::c_void) -> bool {
    unsafe { ffi::pm_wasmmod_pack_linear(pack, off, n, out) }
}

/// `pm_wasmmod_pack_load`.
pub fn pack_load(bytes: *const u8, len: u32, name: *const core::ffi::c_char, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> *mut pm_wasmmod_pack_t {
    unsafe { ffi::pm_wasmmod_pack_load(bytes, len, name, errbuf, errbuf_len) }
}

/// `pm_wasmmod_pack_load_ex`.
pub fn pack_load_ex(code: *const u8, code_len: u32, meta: *const u8, meta_len: u32, name: *const core::ffi::c_char, path_hint: *const core::ffi::c_char, errbuf: *mut core::ffi::c_char, errbuf_len: usize) -> *mut pm_wasmmod_pack_t {
    unsafe { ffi::pm_wasmmod_pack_load_ex(code, code_len, meta, meta_len, name, path_hint, errbuf, errbuf_len) }
}

/// `pm_wasmmod_pack_lookup_fn`.
pub fn pack_lookup_fn(pack: *mut pm_wasmmod_pack_t, func: *const core::ffi::c_char) -> *mut core::ffi::c_void {
    unsafe { ffi::pm_wasmmod_pack_lookup_fn(pack, func) }
}

/// `pm_wasmmod_pack_mem_alloc`.
pub fn pack_mem_alloc(pack: *mut pm_wasmmod_pack_t, n: u32, native_out: *mut *mut core::ffi::c_void) -> u32 {
    unsafe { ffi::pm_wasmmod_pack_mem_alloc(pack, n, native_out) }
}

/// `pm_wasmmod_pack_mem_free`.
pub fn pack_mem_free(pack: *mut pm_wasmmod_pack_t, off: u32) -> () {
    unsafe { ffi::pm_wasmmod_pack_mem_free(pack, off) }
}

/// `pm_wasmmod_pack_mem_read`.
pub fn pack_mem_read(pack: *mut pm_wasmmod_pack_t, off: u32, n: u32, dst: *mut core::ffi::c_void) -> bool {
    unsafe { ffi::pm_wasmmod_pack_mem_read(pack, off, n, dst) }
}

/// `pm_wasmmod_pack_mem_write`.
pub fn pack_mem_write(pack: *mut pm_wasmmod_pack_t, off: u32, n: u32, src: *const core::ffi::c_void) -> bool {
    unsafe { ffi::pm_wasmmod_pack_mem_write(pack, off, n, src) }
}

/// `pm_wasmmod_pack_origin`.
pub fn pack_origin(pack: *const pm_wasmmod_pack_t) -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_pack_origin(pack);
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}
