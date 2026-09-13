//! pymergetic.wasmmod.boot tests — boot count + unwind (no full registry needed).

extern "C" {
    fn pm_mod_boot_count() -> u32;
    fn pm_mod_boot_fqn(idx: u32) -> *const core::ffi::c_char;
    fn pm_mod_boot_unwind();
}

#[test]
fn boot_count_non_negative() {
    // The module count should be non-negative (u32 is always >= 0).
    // It may be 0 for a bare harness or N for a harness that has already
    // registered modules — either is valid.
    let _n: u32 = pm_mod_boot_count();
}

#[test]
fn boot_count_is_idempotent() {
    let a = pm_mod_boot_count();
    let b = pm_mod_boot_count();
    assert_eq!(a, b);
}

#[test]
fn boot_fqn_null_when_out_of_range() {
    let n = pm_mod_boot_count();
    // Out-of-range index must return null
    let fqn = pm_mod_boot_fqn(n);
    assert!(fqn.is_null());
    // Even further out of range
    let fqn2 = pm_mod_boot_fqn(u32::MAX);
    assert!(fqn2.is_null());
}

#[test]
fn boot_fqn_in_range_returns_cstr() {
    let n = pm_mod_boot_count();
    if n > 0 {
        for i in 0..n {
            let fqn = pm_mod_boot_fqn(i);
            assert!(!fqn.is_null(), "fqn for index {i} should not be null when count={n}");
            // It should be a valid C string (first byte must not be NUL if non-empty)
            let first = unsafe { *fqn };
            assert_ne!(first, 0);
        }
    }
    // If n == 0 the loop is skipped — nothing to test against.
}

#[test]
fn boot_unwind_does_not_crash() {
    // unwind tears down all modules; safe to call even when nothing is registered.
    pm_mod_boot_unwind();
}

#[test]
fn boot_unwind_idempotent() {
    pm_mod_boot_unwind();
    // Second unwind must not crash
    pm_mod_boot_unwind();
}

#[test]
fn boot_count_after_unwind() {
    // After unwind the count should be 0
    pm_mod_boot_unwind();
    let n = pm_mod_boot_count();
    assert_eq!(n, 0);
}