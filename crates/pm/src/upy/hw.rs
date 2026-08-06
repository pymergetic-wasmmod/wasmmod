//! Thin wraps for `upy` / `hw` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_framebuf_new`.
pub fn framebuf_new(buf: pm_upy_obj_t, w: i32, h: i32, fmt: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_framebuf_new(buf, w, h, fmt) }
}

/// `pm_upy_machine_i2c`.
pub fn machine_i2c(id: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_machine_i2c(id) }
}

/// `pm_upy_machine_pin`.
pub fn machine_pin(pin: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_machine_pin(pin) }
}

/// `pm_upy_machine_spi`.
pub fn machine_spi(id: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_machine_spi(id) }
}

/// `pm_upy_machine_uart`.
pub fn machine_uart(id: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_machine_uart(id) }
}
