//! pymergetic.util.version — module tests (`__tests__.rs`).
//! Cases register via `PM_MOD_TEST_RS!` into the registry test table.

use super::{pm_util_version_cmp, pm_util_version_satisfies};

unsafe extern "C" fn case_cmp_orders_semver() -> i32 {
    unsafe {
        if pm_util_version_cmp(b"1.0.0".as_ptr(), 5, b"1.0.1".as_ptr(), 5) != -1 {
            return 1;
        }
        if pm_util_version_cmp(b"2.0.0".as_ptr(), 5, b"1.9.9".as_ptr(), 5) != 1 {
            return 1;
        }
        if pm_util_version_cmp(b"1.0.0".as_ptr(), 5, b"1.0.0-a".as_ptr(), 7) != 1 {
            return 1;
        }
        if pm_util_version_cmp(b"0.2.0".as_ptr(), 5, b"0.2.0a2".as_ptr(), 7) != 1 {
            return 1;
        }
        if pm_util_version_cmp(b"0.2.0a2".as_ptr(), 7, b"0.2.0a1".as_ptr(), 7) != 1 {
            return 1;
        }
    }
    0
}

unsafe extern "C" fn case_satisfies_pins() -> i32 {
    unsafe {
        if pm_util_version_satisfies(b"1.2.3".as_ptr(), 5, b"*".as_ptr(), 1) != 1 {
            return 1;
        }
        if pm_util_version_satisfies(b"1.2.3".as_ptr(), 5, b"1.2.3".as_ptr(), 5) != 1 {
            return 1;
        }
        if pm_util_version_satisfies(b"1.2.3".as_ptr(), 5, b">=1.0.0".as_ptr(), 7) != 1 {
            return 1;
        }
        if pm_util_version_satisfies(b"1.2.3".as_ptr(), 5, b">=2.0.0".as_ptr(), 7) != 0 {
            return 1;
        }
        if pm_util_version_satisfies(b"1.2.3".as_ptr(), 5, b"^1.0.0".as_ptr(), 6) != 1 {
            return 1;
        }
        if pm_util_version_satisfies(b"2.0.0".as_ptr(), 5, b"^1.0.0".as_ptr(), 6) != 0 {
            return 1;
        }
        if pm_util_version_satisfies(b"0.2.0a2".as_ptr(), 7, b"0.2.0a2".as_ptr(), 7) != 1 {
            return 1;
        }
        if pm_util_version_satisfies(b"0.2.0a2".as_ptr(), 7, b">=0.2.0".as_ptr(), 7) != 0 {
            return 1;
        }
        if pm_util_version_satisfies(b"0.2.0".as_ptr(), 5, b">=0.2.0a2".as_ptr(), 9) != 1 {
            return 1;
        }
    }
    0
}

crate::PM_MOD_TEST_RS!(
    "pymergetic.util.version",
    "cmp_orders_semver",
    case_cmp_orders_semver
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.version",
    "satisfies_pins",
    case_satisfies_pins
);

#[test]
fn cmp_orders_semver() {
    assert_eq!(unsafe { case_cmp_orders_semver() }, 0);
}

#[test]
fn satisfies_pins() {
    assert_eq!(unsafe { case_satisfies_pins() }, 0);
}
