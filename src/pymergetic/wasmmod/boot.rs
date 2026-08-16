//! pymergetic.wasmmod.boot — path == module.
//!
//! C muscle is `boot/__impl__.c`. This file is the in-crate FFI, matching
//! `boot/__types__.h`. Generated `__exports__.rs` is the consumer bindgen
//! mirror — not a compile input for this crate (same rule as C muscle vs
//! `__exports__.h`).
#![allow(non_camel_case_types)]

#[repr(C)]
pub struct pm_util_mem_arena_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct pm_mod_boot_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct pm_mod_bootdep_t {
    _opaque: [u8; 0],
}

unsafe extern "C" {
    pub fn pm_mod_boot_run(arena: *mut pm_util_mem_arena_t) -> i32;
    pub fn pm_mod_boot_unwind();
    pub fn pm_mod_boot_add(rec: *const pm_mod_boot_t) -> i32;
    pub fn pm_mod_bootdep_add(rec: *const pm_mod_bootdep_t) -> i32;
}
