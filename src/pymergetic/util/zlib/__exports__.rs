//! pymergetic.util.zlib — hand-written stand-in for what `bindgen` should
//! emit by reading zlib.types.h + zlib.export.h. Delete once the real
//! bindgen pipeline exists; this is training-scaffold, not the plan.
#![allow(non_camel_case_types)]

use core::ffi::c_void;

pub const PM_UTIL_ZLIB_OK: i32 = 0;
pub const PM_UTIL_ZLIB_ERR_NOSPACE: i32 = -1;
pub const PM_UTIL_ZLIB_ERR_DATA: i32 = -2;

unsafe extern "C" {
    pub fn pm_util_zlib_inflate(src: *const c_void, src_len: usize, dst: *mut c_void, dst_cap: usize) -> i32;
    pub fn pm_util_zlib_deflate(
        src: *const c_void,
        src_len: usize,
        dst: *mut c_void,
        dst_cap: usize,
        hist_scratch: *mut c_void,
        hist_scratch_len: usize,
    ) -> i32;
}
