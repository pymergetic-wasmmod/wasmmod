//! Thin wraps for `upy` / `util` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_asm_available`.
pub fn asm_available() -> i32 {
    unsafe { ffi::pm_upy_asm_available() }
}

/// `pm_upy_asm_emit`.
pub fn asm_emit(as_: *mut core::ffi::c_void, op: i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_asm_emit(as_, op) })
}

/// `pm_upy_autoload`.
pub fn autoload(name: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_autoload(name) })
}

/// `pm_upy_autoload_available`.
pub fn autoload_available() -> i32 {
    unsafe { ffi::pm_upy_autoload_available() }
}

/// `pm_upy_errno_get`.
pub fn errno_get() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_errno_get() })
}

/// `pm_upy_format_float`.
pub fn format_float(v: f64, buf: *mut core::ffi::c_char, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_format_float(v, buf, len) })
}

/// `pm_upy_formatfloat_available`.
pub fn formatfloat_available() -> i32 {
    unsafe { ffi::pm_upy_formatfloat_available() }
}

/// `pm_upy_libc_policy`.
pub fn libc_policy() -> u32 {
    unsafe { ffi::pm_upy_libc_policy() }
}

/// `pm_upy_mpz_available`.
pub fn mpz_available() -> i32 {
    unsafe { ffi::pm_upy_mpz_available() }
}

/// `pm_upy_mpz_from_int`.
pub fn mpz_from_int(v: i64) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_mpz_from_int(v) }
}

/// `pm_upy_pairheap_available`.
pub fn pairheap_available() -> i32 {
    unsafe { ffi::pm_upy_pairheap_available() }
}

/// `pm_upy_pairheap_init`.
pub fn pairheap_init(heap: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_pairheap_init(heap) })
}

/// `pm_upy_parse_num`.
pub fn parse_num(s: *const core::ffi::c_char, out: *mut i64) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_parse_num(s, out) })
}

/// `pm_upy_parsenum_available`.
pub fn parsenum_available() -> i32 {
    unsafe { ffi::pm_upy_parsenum_available() }
}

/// `pm_upy_persistentcode_available`.
pub fn persistentcode_available() -> i32 {
    unsafe { ffi::pm_upy_persistentcode_available() }
}

/// `pm_upy_persistentcode_save_fun`.
pub fn persistentcode_save_fun(fun: pm_upy_obj_t, writer: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_persistentcode_save_fun(fun, writer) })
}

/// `pm_upy_pystack_init`.
pub fn pystack_init(start: *mut core::ffi::c_void, end: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_pystack_init(start, end) })
}

/// `pm_upy_ringbuf_available`.
pub fn ringbuf_available() -> i32 {
    unsafe { ffi::pm_upy_ringbuf_available() }
}

/// `pm_upy_ringbuf_get`.
pub fn ringbuf_get(rb: *mut core::ffi::c_void, v: *mut u8) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_ringbuf_get(rb, v) })
}

/// `pm_upy_ringbuf_put`.
pub fn ringbuf_put(rb: *mut core::ffi::c_void, v: u8) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_ringbuf_put(rb, v) })
}

/// `pm_upy_scope_available`.
pub fn scope_available() -> i32 {
    unsafe { ffi::pm_upy_scope_available() }
}

/// `pm_upy_scope_new`.
pub fn scope_new() -> *mut core::ffi::c_void {
    unsafe { ffi::pm_upy_scope_new() }
}

/// `pm_upy_stackalt_available`.
pub fn stackalt_available() -> i32 {
    unsafe { ffi::pm_upy_stackalt_available() }
}

/// `pm_upy_warning`.
pub fn warning(msg: *const core::ffi::c_char) -> () {
    unsafe { ffi::pm_upy_warning(msg) }
}
