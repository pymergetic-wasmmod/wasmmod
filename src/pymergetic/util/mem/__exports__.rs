//! pymergetic.util.mem — hand-written stand-in for what `bindgen` should
//! emit by reading mem.types.h + mem.export.h. Delete once the real
//! bindgen pipeline exists; this is training-scaffold, not the plan.
#![allow(non_camel_case_types)]

use core::ffi::c_void;

#[repr(C)]
pub struct pm_util_mem_arena_t {
    _opaque: [u8; 0],
}

unsafe extern "C" {
    pub fn pm_util_mem_arena_create(base: *mut c_void, size: usize) -> *mut pm_util_mem_arena_t;
    pub fn pm_util_mem_arena_destroy(arena: *mut pm_util_mem_arena_t);
    pub fn pm_util_mem_alloc(arena: *mut pm_util_mem_arena_t, size: usize) -> *mut c_void;
    pub fn pm_util_mem_realloc(arena: *mut pm_util_mem_arena_t, ptr: *mut c_void, size: usize) -> *mut c_void;
    pub fn pm_util_mem_free(arena: *mut pm_util_mem_arena_t, ptr: *mut c_void);
    pub fn pm_util_mem_arena_overhead() -> usize;
}
