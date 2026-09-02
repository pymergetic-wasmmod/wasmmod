//! pymergetic.util.version — module benchmarks (`__bench__.rs`).
//!
//! A bench is `unsafe extern "C" fn(iterations: u64) -> i32`: do `iterations`
//! units of work inside one call, return `0` = ok. The registry owns warmup +
//! the measured lap and divides wall time by `iterations` for ns/op. Benches
//! are informational and never gate; the seat's clock fill decides whether one
//! reports a number or "no clock". Same shape as `util/version/__tests__.rs`
//! but for timing, on this `impl = rs` module's own muscle language.
//!
//! Kernel-discipline: the same file compiles under cargo (dev bench runs)
//! and under the kernel's rsx (ksweep sweeps this unit too — it is authored
//! companion muscle, so it embeds and sweeps). No `use super::` (the unit
//! compiles standalone; the exports face is `extern "C"` here), explicit
//! ptr+len pairs, no slice `.len()`.

unsafe extern "C" {
    fn pm_util_version_cmp(a0: *const u8, a1: u32, a2: *const u8, a3: u32) -> i32;
    fn pm_util_version_satisfies(a0: *const u8, a1: u32, a2: *const u8, a3: u32) -> i32;
}

/// `iterations` semver comparisons — the hot path of pack dep pinning.
unsafe extern "C" fn bench_compare(iterations: u64) -> i32 {
    if iterations == 0 {
        return 0;
    }
    // Synthetic but representative: a pin range next to a plausible installed
    // version, seen thousands of times during a pack graph resolve.
    let a: *const u8 = b"1.24.3\0".as_ptr();
    let b: *const u8 = b"^1.0.0\0".as_ptr();
    let mut acc: i64 = 0;
    let mut i: u64 = 0;
    while i < iterations {
        acc = acc.wrapping_add(unsafe {
            pm_util_version_cmp(a, 6, b, 6) as i64
        });
        i += 1;
    }
    // Not a correctness gate — just keep the loop from being optimized away.
    // (i64::MIN spelled out: rsx maps iN::MIN to the stdint limit macro, but
    // the literal is one less thing the kernel compiler must know.)
    if acc == -9223372036854775808i64 {
        return 1;
    }
    0
}

/// `iterations` pin satisfactions — the decider when a pack's dep is fixed.
unsafe extern "C" fn bench_satisfies(iterations: u64) -> i32 {
    if iterations == 0 {
        return 0;
    }
    let have: *const u8 = b"1.24.3\0".as_ptr();
    let need: *const u8 = b">=1.0.0\0".as_ptr();
    let mut acc: i64 = 0;
    let mut i: u64 = 0;
    while i < iterations {
        acc = acc.wrapping_add(unsafe {
            pm_util_version_satisfies(have, 6, need, 7) as i64
        });
        i += 1;
    }
    if acc == -9223372036854775808i64 {
        return 1;
    }
    0
}

crate::PM_MOD_BENCH_RS!("pymergetic.util.version", "compare_semver", bench_compare);
crate::PM_MOD_BENCH_RS!("pymergetic.util.version", "satisfies_pin", bench_satisfies);

#[test]
fn benches_run_clean() {
    assert_eq!(unsafe { bench_compare(10000) }, 0);
    assert_eq!(unsafe { bench_satisfies(10000) }, 0);
}
