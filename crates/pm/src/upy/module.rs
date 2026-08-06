//! Thin wraps for `upy` / `module` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_bind`.
pub fn bind(mod_: *const core::ffi::c_char, name: *const core::ffi::c_char, fn_: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_bind(mod_, name, fn_) })
}

/// `pm_upy_bind_reg`.
pub fn bind_reg(full_module: *const core::ffi::c_char, func: *const core::ffi::c_char, fn_ptr: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_bind_reg(full_module, func, fn_ptr) })
}

/// `pm_upy_bind_resolve_module`.
pub fn bind_resolve_module(name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_bind_resolve_module(name) }
}

/// `pm_upy_import_all`.
pub fn import_all(mod_: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_import_all(mod_) })
}

/// `pm_upy_import_from`.
pub fn import_from(mod_: *const core::ffi::c_char, name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_import_from(mod_, name) }
}

/// `pm_upy_import_name`.
pub fn import_name(name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_import_name(name) }
}

/// `pm_upy_module_get_builtin`.
pub fn module_get_builtin(name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_module_get_builtin(name) }
}

/// `pm_upy_module_has`.
pub fn module_has(full_name: *const core::ffi::c_char) -> bool {
    unsafe { ffi::pm_upy_module_has(full_name) }
}

/// `pm_upy_module_install_face`.
pub fn module_install_face(full_name: *const core::ffi::c_char, face: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_module_install_face(full_name, face) })
}
