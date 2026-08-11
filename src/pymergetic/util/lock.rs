//! pymergetic.util.lock — thin reexport. The real impl (`SpinLock<T>` /
//! `Mutex<T>` + the raw `pm_util_lock_t` primitive) lives in lock/__impl__.rs
//! (tucked away with this module's faces); this file only exists so
//! `pymergetic::util::lock` resolves at the path "path == module"
//! expects.
#[path = "lock/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
