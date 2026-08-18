//! pymergetic.util.version — module benchmarks (`__bench__.rs`).
//!
//! A bench is `unsafe extern "C" fn(iterations: u64) -> i32`: do `iterations`
//! units of work inside one call, return `0` = ok. The registry owns warmup +
//! the measured lap and divides wall time by `iterations` for ns/op. Benches
//! are informational and never gate; the seat's clock fill decides whether one
//! reports a number or "no clock". Same shape as `util/version/__tests__.rs`
//! but for timing, on this `impl = rs` module's own muscle language.

use super::{pm_util_version_cmp, pm_util_version_satisfies};

/// `iterations` semver comparisons — the hot path of pack dep pinning.
unsafe extern "C" fn bench_compare(iterations: u64) -> i32 {
    if iterations == 0 {
        return 0;
    }
    // Synthetic but representative: a pin range next to a plausible installed
    // version, seen thousands of times during a pack graph resolve.
    let a: &[u8] = b"1.24.3";
    let b: &[u8] = b"^1.0.0";
    let mut acc: i64 = 0;
    let mut i: u64 = 0;
    while i < iterations {
        acc = acc.wrapping_add(unsafe {
            pm_util_version_cmp(
                a.as_ptr(),
                a.len() as u32,
                b.as_ptr(),
                b.len() as u32,
            ) as i64
        });
        i += 1;
    }
    // Not a correctness gate — just keep the loop from being optimized away.
    if acc == i64::MIN {
        return 1;
    }
    0
}

/// `iterations` pin satisfactions — the decider when a pack's dep is fixed.
unsafe extern "C" fn bench_satisfies(iterations: u64) -> i32 {
    if iterations == 0 {
        return 0;
    }
    let have: &[u8] = b"1.24.3";
    let need: &[u8] = b">=1.0.0";
    let mut acc: i64 = 0;
    let mut i: u64 = 0;
    while i < iterations {
        acc = acc.wrapping_add(unsafe {
            pm_util_version_satisfies(
                have.as_ptr(),
                have.len() as u32,
                need.as_ptr(),
                need.len() as u32,
            ) as i64
        });
        i += 1;
    }
    if acc == i64::MIN {
        return 1;
    }
    0
}

crate::PM_MOD_BENCH_RS!("pymergetic.util.version", "compare_semver", bench_compare);
crate::PM_MOD_BENCH_RS!("pymergetic.util.version", "satisfies_pin", bench_satisfies);

#[test]
fn benches_run_clean() {
    assert_eq!(unsafe { bench_compare(10_000) }, 0);
    assert_eq!(unsafe { bench_satisfies(10_000) }, 0);
}
