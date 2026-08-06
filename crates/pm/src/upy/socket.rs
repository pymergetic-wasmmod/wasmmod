//! Thin wraps for `upy` / `socket` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_socket_accept`.
pub fn socket_accept(sock: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_socket_accept(sock) }
}

/// `pm_upy_socket_available`.
pub fn socket_available() -> i32 {
    unsafe { ffi::pm_upy_socket_available() }
}

/// `pm_upy_socket_bind`.
pub fn socket_bind(sock: pm_upy_obj_t, host: *const core::ffi::c_char, port: i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_socket_bind(sock, host, port) })
}

/// `pm_upy_socket_connect`.
pub fn socket_connect(sock: pm_upy_obj_t, host: *const core::ffi::c_char, port: i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_socket_connect(sock, host, port) })
}

/// `pm_upy_socket_create`.
pub fn socket_create(af: i32, type_: i32, proto: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_socket_create(af, type_, proto) }
}

/// `pm_upy_socket_listen`.
pub fn socket_listen(sock: pm_upy_obj_t, backlog: i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_socket_listen(sock, backlog) })
}

/// `pm_upy_socket_recv`.
pub fn socket_recv(sock: pm_upy_obj_t, buf: *mut core::ffi::c_void, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_socket_recv(sock, buf, len) })
}

/// `pm_upy_socket_send`.
pub fn socket_send(sock: pm_upy_obj_t, buf: *const core::ffi::c_void, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_socket_send(sock, buf, len) })
}
