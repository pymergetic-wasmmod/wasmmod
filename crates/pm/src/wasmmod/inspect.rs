//! Thin wraps for `wasmmod` / `inspect` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_inspect_addr2line`.
pub fn inspect_addr2line(buf: *const u8, len: u32, addr: u64, out: *mut pm_wasmmod_loc_t, cap: usize, n_out: *mut usize) -> bool {
    unsafe { ffi::pm_wasmmod_inspect_addr2line(buf, len, addr, out, cap, n_out) }
}

/// `pm_wasmmod_inspect_disasm`.
pub fn inspect_disasm(buf: *const u8, len: u32, section_index: u32, offset: u32, limit: u32, out: *mut pm_wasmmod_disasm_line_t, cap: usize, n_out: *mut usize) -> bool {
    unsafe { ffi::pm_wasmmod_inspect_disasm(buf, len, section_index, offset, limit, out, cap, n_out) }
}

/// `pm_wasmmod_inspect_has_dwarf`.
pub fn inspect_has_dwarf(buf: *const u8, len: u32) -> bool {
    unsafe { ffi::pm_wasmmod_inspect_has_dwarf(buf, len) }
}

/// `pm_wasmmod_inspect_locations`.
pub fn inspect_locations(buf: *const u8, len: u32, name: *const core::ffi::c_char, out: *mut pm_wasmmod_loc_t, cap: usize, n_out: *mut usize) -> bool {
    unsafe { ffi::pm_wasmmod_inspect_locations(buf, len, name, out, cap, n_out) }
}

/// `pm_wasmmod_inspect_mpy_disasm`.
pub fn inspect_mpy_disasm(mpy: *const u8, len: u32, limit: u32, out: *mut pm_wasmmod_disasm_line_t, cap: usize, n_out: *mut usize) -> bool {
    unsafe { ffi::pm_wasmmod_inspect_mpy_disasm(mpy, len, limit, out, cap, n_out) }
}

/// `pm_wasmmod_inspect_symbols`.
pub fn inspect_symbols(buf: *const u8, len: u32, out: *mut pm_wasmmod_sym_t, cap: usize, n_out: *mut usize) -> bool {
    unsafe { ffi::pm_wasmmod_inspect_symbols(buf, len, out, cap, n_out) }
}
