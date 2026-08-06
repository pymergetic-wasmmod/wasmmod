//! MicroPython GC / allocator helpers.

use crate::check_status;
use crate::upy::ffi;
use crate::PmError;

/// Run a GC collection (`pm_upy_gc_collect`).
pub fn gc_collect() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_gc_collect() })
}
