//! pymergetic.util.lock — cooperative-friendly mutual exclusion.
//!
//! One raw primitive (`pm_util_lock_t` + acquire/release/try_acquire, real C
//! ABI, no generics — C can take this lock directly). The ergonomic
//! Rust-side generic wrapper (`SpinLock<T>` / `Mutex<T>`) lives in the
//! crate-glue `util/lock.rs` (generics/trait impls/lifetimes are outside
//! the rsx subset — this file is the card muscle and must compile through
//! it; the wrapper is cargo-only product code and never crosses the C
//! face).
//!
//! Pure spin, no OS blocking call anywhere — has to work identically on
//! bare-metal (no OS to block on) and host. The acquire is a swap
//! (test-and-set): same exclusion semantics as CAS, one memory order to
//! argue about, and it lowers to a single `lock xchg`-class instruction on
//! every seat's compiler.

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use core::sync::atomic::{AtomicU32, Ordering};

const UNLOCKED: u32 = 0;
const LOCKED: u32 = 1;

/// Raw C-ABI primitive: no payload, just the exclusion bit. This is what
/// the cargo-side `SpinLock<T>` (util/lock.rs) wraps internally too — one
/// mechanism, not two.
#[repr(C)]
pub struct pm_util_lock_t {
    locked: AtomicU32,
}

impl pm_util_lock_t {
    pub const fn new() -> pm_util_lock_t {
        pm_util_lock_t {
            locked: AtomicU32::new(UNLOCKED),
        }
    }

    pub(crate) fn acquire(&self) {
        while self.locked.swap(LOCKED, Ordering::Acquire) != UNLOCKED {
            core::hint::spin_loop();
        }
    }

    pub(crate) fn try_acquire(&self) -> bool {
        self.locked.swap(LOCKED, Ordering::Acquire) == UNLOCKED
    }

    pub(crate) fn release(&self) {
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

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_init, "void(pm_util_lock_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_acquire, "void(pm_util_lock_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_release, "void(pm_util_lock_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.util.lock", pm_util_lock_try_acquire, "int32_t(pm_util_lock_t *)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
