//! Raw bindgen declarations for the `pm_*` C API.
//!
//! Prefer thin wraps in sibling modules. Callers should only use symbols whose
//! names start with `pm_upy_` / `PM_UPY_` (plus shared `pm_status_t` / `PM_*`).

pub use crate::bindgen::*;
