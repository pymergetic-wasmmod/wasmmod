//! pymergetic.util.lock — cooperative-friendly mutual exclusion.
//!
//! One raw primitive (`pm_util_lock_t` + acquire/release/try_acquire, real C
//! ABI, no generics — C can take this lock directly) plus an ergonomic
//! Rust-side generic wrapper (`SpinLock<T>` / `Mutex<T>`) built on the
//! exact same atomic, not a second mechanism.
//!
//! Pure spin, no OS blocking call anywhere — has to work identically on
//! bare-metal (no OS to block on) and host. `Mutex<T>` is the same
//! primitive as `SpinLock<T>`; the name only marks "coarse-grained,
//! infrequent" call sites (registry's table lock, e.g.) vs "tight hot
//! path" ones — no behavioral difference yet, and it should stay that
//! way until something concrete needs host-side blocking instead of
//! spinning.

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::sync::atomic::{AtomicU32, Ordering};

const UNLOCKED: u32 = 0;
const LOCKED: u32 = 1;

/// Raw C-ABI primitive: no payload, just the exclusion bit. This is what
/// `SpinLock<T>` wraps internally too — one mechanism, not two.
#[repr(C)]
pub struct pm_util_lock_t {
    locked: AtomicU32,
}

impl Default for pm_util_lock_t {
    fn default() -> Self {
        Self::new()
    }
}

impl pm_util_lock_t {
    pub const fn new() -> Self {
        Self {
            locked: AtomicU32::new(UNLOCKED),
        }
    }

    fn acquire(&self) {
        while self
            .locked
            .compare_exchange_weak(UNLOCKED, LOCKED, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            while self.locked.load(Ordering::Relaxed) != UNLOCKED {
                core::hint::spin_loop();
            }
        }
    }

    fn try_acquire(&self) -> bool {
        self.locked
            .compare_exchange(UNLOCKED, LOCKED, Ordering::Acquire, Ordering::Relaxed)
            .is_ok()
    }

    fn release(&self) {
        self.locked.store(UNLOCKED, Ordering::Release);
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_lock_init(lock: *mut pm_util_lock_t) {
    unsafe { core::ptr::write(lock, pm_util_lock_t::new()) };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_lock_acquire(lock: *mut pm_util_lock_t) {
    unsafe { (*lock).acquire() };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_lock_release(lock: *mut pm_util_lock_t) {
    unsafe { (*lock).release() };
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_lock_try_acquire(lock: *mut pm_util_lock_t) -> i32 {
    unsafe { (*lock).try_acquire() as i32 }
}

/// Ergonomic Rust-side wrapper: a real payload sits behind the same raw
/// primitive above. Not exported to C — C consumers manage their own
/// payload next to a bare `pm_util_lock_t`, same as any real-world lock.
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

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_init, "void(pm_util_lock_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_acquire, "void(pm_util_lock_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_release, "void(pm_util_lock_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_try_acquire, "int32_t(pm_util_lock_t *)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
