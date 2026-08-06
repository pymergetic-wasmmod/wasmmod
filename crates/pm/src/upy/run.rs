//! Execute Python source on the host (`pm_upy_run_str`).

use std::ffi::CString;

use crate::check_status;
use crate::upy::ffi;
use crate::PmError;

/// Run a UTF-8 source string (`pm_upy_run_str`).
pub fn run_str(src: &str) -> Result<(), PmError> {
    let c = CString::new(src).map_err(|_| crate::PmError::Status(crate::StatusError(crate::PM_ERR_ARG)))?;
    check_status(unsafe { ffi::pm_upy_run_str(c.as_ptr()) })
}
