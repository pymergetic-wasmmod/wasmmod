//! HAL time helpers (`pm_upy_ticks_*`, `pm_upy_delay_*`, `pm_upy_sleep_us`).

use crate::upy::ffi;

/// Millisecond ticks (`pm_upy_ticks_ms`).
pub fn ticks_ms() -> u32 {
    unsafe { ffi::pm_upy_ticks_ms() }
}

/// Microsecond ticks (`pm_upy_ticks_us`).
pub fn ticks_us() -> u32 {
    unsafe { ffi::pm_upy_ticks_us() }
}

/// Nanosecond wall time (`pm_upy_time_ns`).
pub fn time_ns() -> u64 {
    unsafe { ffi::pm_upy_time_ns() }
}

/// Busy-delay milliseconds (`pm_upy_delay_ms`).
pub fn delay_ms(ms: u32) {
    unsafe { ffi::pm_upy_delay_ms(ms) }
}

/// Busy-delay microseconds (`pm_upy_delay_us`).
pub fn delay_us(us: u32) {
    unsafe { ffi::pm_upy_delay_us(us) }
}

/// Sleep/delay microseconds (`pm_upy_sleep_us`).
pub fn sleep_us(us: u64) -> u32 {
    unsafe { ffi::pm_upy_sleep_us(us) }
}
