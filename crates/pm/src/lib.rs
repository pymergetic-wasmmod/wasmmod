//! Host-side Rust bindings for the public `pm_*` C API (`include/`).
//!
//! - [`upy`] — MicroPython surface (`pm_upy_*`); raw names in [`upy::ffi`]
//! - [`wasmmod`] — engine surface (`pm_wasmmod_*`); raw names in [`wasmmod::ffi`]
//!
//! Prefer thin wraps (`upy::mem`, `wasmmod::host`, …) over calling `ffi` directly.
//! Linking against MicroPython + wasmmod glue is the host's job; this crate only
//! generates declarations and light Result helpers.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(dead_code)]
#![allow(clippy::all)]

mod bindgen;

pub mod upy;
pub mod wasmmod;

/// Config / menuconfig feature unavailable (`PM_ERR_FEATURE`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FeatureError;

/// Non-OK status other than feature-unavailable.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StatusError(pub i32);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PmError {
    Feature(FeatureError),
    Status(StatusError),
}

/// Bindgen constified names for `pm_status_t` (`pm_status_t_PM_OK`, …).
pub use bindgen::{
    pm_status_t_PM_ERR as PM_ERR, pm_status_t_PM_ERR_ARG as PM_ERR_ARG,
    pm_status_t_PM_ERR_FEATURE as PM_ERR_FEATURE, pm_status_t_PM_ERR_NOMEM as PM_ERR_NOMEM,
    pm_status_t_PM_ERR_NOT_READY as PM_ERR_NOT_READY, pm_status_t_PM_OK as PM_OK,
};

/// Map a C status: `PM_OK` → `Ok`, `PM_ERR_FEATURE` → [`PmError::Feature`], else [`PmError::Status`].
pub fn check_status(code: i32) -> Result<(), PmError> {
    if code == PM_OK {
        Ok(())
    } else if code == PM_ERR_FEATURE {
        Err(PmError::Feature(FeatureError))
    } else {
        Err(PmError::Status(StatusError(code)))
    }
}
