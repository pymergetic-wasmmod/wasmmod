//! Thin wraps for `upy` / `vfs` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_builtin_open`.
pub fn builtin_open(path: *const core::ffi::c_char, mode: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_builtin_open(path, mode) }
}

/// `pm_upy_vfs_blockdev_available`.
pub fn vfs_blockdev_available() -> i32 {
    unsafe { ffi::pm_upy_vfs_blockdev_available() }
}

/// `pm_upy_vfs_blockdev_ioctl`.
pub fn vfs_blockdev_ioctl(bdev: pm_upy_obj_t, op: u32, arg: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_blockdev_ioctl(bdev, op, arg) })
}

/// `pm_upy_vfs_blockdev_read`.
pub fn vfs_blockdev_read(bdev: pm_upy_obj_t, buf: *mut u8, block_num: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_blockdev_read(bdev, buf, block_num) })
}

/// `pm_upy_vfs_blockdev_write`.
pub fn vfs_blockdev_write(bdev: pm_upy_obj_t, buf: *const u8, block_num: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_blockdev_write(bdev, buf, block_num) })
}

/// `pm_upy_vfs_import_stat`.
pub fn vfs_import_stat(path: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_import_stat(path) })
}

/// `pm_upy_vfs_listdir`.
pub fn vfs_listdir(path: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_vfs_listdir(path) }
}

/// `pm_upy_vfs_mount`.
pub fn vfs_mount(vfs: pm_upy_obj_t, mount_point: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_mount(vfs, mount_point) })
}

/// `pm_upy_vfs_open`.
pub fn vfs_open(path: *const core::ffi::c_char, mode: *const core::ffi::c_char, out: *mut pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_open(path, mode, out) })
}

/// `pm_upy_vfs_stat`.
pub fn vfs_stat(path: *const core::ffi::c_char, out: *mut pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_vfs_stat(path, out) })
}
