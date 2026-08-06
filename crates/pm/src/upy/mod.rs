//! MicroPython host surface (`pm_upy_*`).
//!
//! Raw C names live in [`ffi`]; prefer thin wraps in sibling modules.

pub mod features;
pub mod ffi;
pub mod mem;
pub mod run;
pub mod sched;
pub mod time;
