//! pymergetic.util.lock — glue + ergonomic wrapper.
//!
//! The card muscle (raw `pm_util_lock_t`, the C ABI face) lives in
//! lock/__impl__.rs and must compile through the kernel's rsx subset — no
//! generics, no trait impls, no lifetimes there. This file is crate glue
//! (no `__pmm__.toml` beside it, so it is never embedded or swept): it
//! resolves `pymergetic::util::lock` at the path "path == module" expects
//! AND carries the generic `SpinLock<T>` / `Mutex<T>` wrapper that product
//! Rust code (registry, loader, net.search, gen) wants. Same primitive,
//! same atomics — `Mutex<T>` here wraps the very `pm_util_lock_t` the C
//! face exports, not a second mechanism.

#[path = "lock/__impl__.rs"]
mod r#impl;

pub use r#impl::*;

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};

/// Ergonomic Rust-side wrapper: a real payload sits behind the same raw
/// primitive the card muscle exports. Not exported to C — C consumers
/// manage their own payload next to a bare `pm_util_lock_t`, same as any
/// real-world lock.
pub struct SpinLock<T> {
    raw: pm_util_lock_t,
    value: UnsafeCell<T>,
}

// SAFETY: `raw` is the sole gate to `value` — every access to `value`
// happens through a `SpinLockGuard` obtained after `raw.acquire()`
// succeeds, and `release()` happens only on guard drop.
unsafe impl<T: Send> Send for SpinLock<T> {}
unsafe impl<T: Send> Sync for SpinLock<T> {}

impl<T> SpinLock<T> {
    pub const fn new(value: T) -> Self {
        Self {
            raw: pm_util_lock_t::new(),
            value: UnsafeCell::new(value),
        }
    }

    pub fn lock(&self) -> SpinLockGuard<'_, T> {
        self.raw.acquire();
        SpinLockGuard { lock: self }
    }

    pub fn try_lock(&self) -> Option<SpinLockGuard<'_, T>> {
        if self.raw.try_acquire() {
            Some(SpinLockGuard { lock: self })
        } else {
            None
        }
    }
}

pub struct SpinLockGuard<'a, T> {
    lock: &'a SpinLock<T>,
}

impl<T> Deref for SpinLockGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T {
        // SAFETY: guard existing means `raw` is held by us.
        unsafe { &*self.lock.value.get() }
    }
}

impl<T> DerefMut for SpinLockGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        // SAFETY: guard existing means `raw` is held by us, exclusively.
        unsafe { &mut *self.lock.value.get() }
    }
}

impl<T> Drop for SpinLockGuard<'_, T> {
    fn drop(&mut self) {
        self.lock.raw.release();
    }
}

/// Same primitive as `SpinLock<T>` — see module doc. Diverges only if a
/// host target later wants real OS-level blocking instead of spinning.
pub type Mutex<T> = SpinLock<T>;
pub type MutexGuard<'a, T> = SpinLockGuard<'a, T>;
