//! pymergetic.util.lock — module tests (`__tests__.rs`).
//!
//! Raw-primitive tests in the rsx subset (ptr discipline, no closures, no
//! generics — the ergonomic wrapper's tests are cargo-side, like the
//! wrapper itself). Cases register via `PM_MOD_TEST_RS!` into the registry
//! test table.

use super::*;

unsafe extern "C" fn case_try_acquire_toggles() -> i32 {
    unsafe {
        let mut lock: pm_util_lock_t = pm_util_lock_t::new();
        if pm_util_lock_try_acquire(&mut lock) != 1 {
            return 1;
        }
        /* held: a second try must fail */
        if pm_util_lock_try_acquire(&mut lock) != 0 {
            return 1;
        }
        pm_util_lock_release(&mut lock);
        if pm_util_lock_try_acquire(&mut lock) != 1 {
            return 1;
        }
        pm_util_lock_release(&mut lock);
    }
    0
}

unsafe extern "C" fn case_acquire_release_roundtrip() -> i32 {
    unsafe {
        let mut lock: pm_util_lock_t = pm_util_lock_t::new();
        pm_util_lock_acquire(&mut lock);
        /* held: try_acquire must report contention */
        if pm_util_lock_try_acquire(&mut lock) != 0 {
            return 1;
        }
        pm_util_lock_release(&mut lock);
        if pm_util_lock_try_acquire(&mut lock) != 1 {
            return 1;
        }
        pm_util_lock_release(&mut lock);
    }
    0
}

unsafe extern "C" fn case_init_resets_a_held_lock() -> i32 {
    unsafe {
        let mut lock: pm_util_lock_t = pm_util_lock_t::new();
        pm_util_lock_acquire(&mut lock);
        /* re-init is the constructor discipline: state back to unlocked */
        pm_util_lock_init(&mut lock);
        if pm_util_lock_try_acquire(&mut lock) != 1 {
            return 1;
        }
        pm_util_lock_release(&mut lock);
    }
    0
}

crate::PM_MOD_TEST_RS!(
    "pymergetic.util.lock",
    "try_acquire_toggles",
    case_try_acquire_toggles
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.lock",
    "acquire_release_roundtrip",
    case_acquire_release_roundtrip
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.lock",
    "init_resets_a_held_lock",
    case_init_resets_a_held_lock
);

#[test]
fn try_acquire_toggles() {
    assert_eq!(unsafe { case_try_acquire_toggles() }, 0);
}

#[test]
fn acquire_release_roundtrip() {
    assert_eq!(unsafe { case_acquire_release_roundtrip() }, 0);
}

#[test]
fn init_resets_a_held_lock() {
    assert_eq!(unsafe { case_init_resets_a_held_lock() }, 0);
}
