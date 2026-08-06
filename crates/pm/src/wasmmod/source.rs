//! Thin wraps for `wasmmod` / `source` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_source_close`.
pub fn source_close(src: *mut pm_wasmmod_source_t) -> () {
    unsafe { ffi::pm_wasmmod_source_close(src) }
}

/// `pm_wasmmod_source_info`.
pub fn source_info(src: *const pm_wasmmod_source_t) -> *const pm_wasmmod_source_info_t {
    unsafe { ffi::pm_wasmmod_source_info(src) }
}

/// `pm_wasmmod_source_list_files`.
pub fn source_list_files(src: *const pm_wasmmod_source_t, module_or_null: *const core::ffi::c_char, ctx: *mut core::ffi::c_void, cb: pm_wasmmod_source_path_cb) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_source_list_files(src, module_or_null, ctx, cb) })
}

/// `pm_wasmmod_source_list_modules`.
pub fn source_list_modules(src: *const pm_wasmmod_source_t, ctx: *mut core::ffi::c_void, cb: pm_wasmmod_source_name_cb) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_source_list_modules(src, ctx, cb) })
}

/// `pm_wasmmod_source_list_submodules`.
pub fn source_list_submodules(src: *const pm_wasmmod_source_t, parent: *const core::ffi::c_char, ctx: *mut core::ffi::c_void, cb: pm_wasmmod_source_name_cb) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_source_list_submodules(src, parent, ctx, cb) })
}

/// `pm_wasmmod_source_mount_prefix`.
pub fn source_mount_prefix(src: *const pm_wasmmod_source_t, buf: *mut core::ffi::c_char, buf_len: usize) -> usize {
    unsafe { ffi::pm_wasmmod_source_mount_prefix(src, buf, buf_len) }
}

/// `pm_wasmmod_source_open_buffer`.
pub fn source_open_buffer(wasm: *const u8, len: u32) -> *mut pm_wasmmod_source_t {
    unsafe { ffi::pm_wasmmod_source_open_buffer(wasm, len) }
}

/// `pm_wasmmod_source_open_file`.
pub fn source_open_file(path: *const core::ffi::c_char) -> *mut pm_wasmmod_source_t {
    unsafe { ffi::pm_wasmmod_source_open_file(path) }
}

/// `pm_wasmmod_source_open_name`.
pub fn source_open_name(pack_name: *const core::ffi::c_char) -> *mut pm_wasmmod_source_t {
    unsafe { ffi::pm_wasmmod_source_open_name(pack_name) }
}

/// `pm_wasmmod_source_open_owned`.
pub fn source_open_owned(wasm: *mut u8, len: u32) -> *mut pm_wasmmod_source_t {
    unsafe { ffi::pm_wasmmod_source_open_owned(wasm, len) }
}

/// `pm_wasmmod_source_read`.
pub fn source_read(src: *const pm_wasmmod_source_t, path: *const core::ffi::c_char, out: *mut *mut u8, out_len: *mut u32) -> bool {
    unsafe { ffi::pm_wasmmod_source_read(src, path, out, out_len) }
}
