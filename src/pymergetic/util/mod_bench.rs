//! Host harness for module `__bench__.*` registered via `PM_MOD_BENCH_*`.
//!
//! Mirrors `util/mod_test.rs` but for benches: install a monotonic clock, walk
//! the live registry bench table, and print the ns/op report. Benches are
//! informational — the numbers, not the build, are the deliverable — so this
//! asserts only that every bench *ran* (`0` not-clean), never any fast/slow
//! threshold. Filter with `WASMMOD_BENCH_FQN=dotted.fqn` / `WASMMOD_BENCH_ITERS`
//! to limit scope.
//!
//! The clock fill is a shared global, so this reuses [`mod_test::TEST_LOCK`]
//! (the same lock the module-test cases take) rather than a private one —
//! otherwise it would race the registry's own bench cases, which install and
//! clear a fake clock under that lock. We must not hold it while invoking
//! benches (non-reentrant), so benches are not too serialized: they run inside
//! the registry's `bench_run_all`, which takes no lock of ours.

use crate::util::mod_test::TEST_LOCK;
use crate::wasmmod::registry::{
    pm_wasmmod_registry_bench_count, pm_wasmmod_registry_bench_run_all,
    pm_wasmmod_registry_module_at, pm_wasmmod_registry_module_count,
    pm_wasmmod_registry_set_bench_clock,
};

/// `std::time::Instant`-style monotonic clock, the std seat's fill for
/// `set_bench_clock`. Not a `__bench__` card — this is the harness's own
/// std-only fill (never part of the `no_std` staticlib).
pub unsafe extern "C" fn bench_clock_us() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    match SystemTime::now().duration_since(UNIX_EPOCH) {
        Ok(d) => d.as_micros() as u64,
        Err(_) => 0,
    }
}

#[test]
fn registry_mod_benches_all() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let iterations = std::env::var("WASMMOD_BENCH_ITERS")
        .ok()
        .and_then(|s| s.trim().parse::<u64>().ok())
        .filter(|n| *n > 0)
        .unwrap_or(200_000);
    unsafe { pm_wasmmod_registry_set_bench_clock(Some(bench_clock_us)) };

    let filter = std::env::var("WASMMOD_BENCH_FQN").ok();
    let n = pm_wasmmod_registry_module_count();
    let mut ran = 0u32;
    let mut bad = 0i32;
    for i in 0..n {
        let mut buf = [0u8; 256];
        let mut len = buf.len() as u32;
        if unsafe { pm_wasmmod_registry_module_at(i, buf.as_mut_ptr(), &mut len) } == 0 || len == 0
        {
            continue;
        }
        let fqn = core::str::from_utf8(&buf[..len as usize]).unwrap_or("");
        if let Some(ref want) = filter {
            if fqn != want.as_str() {
                continue;
            }
        }
        let bc = unsafe { pm_wasmmod_registry_bench_count(fqn.as_ptr(), fqn.len() as u32) };
        if bc == 0 {
            continue;
        }
        ran += bc;
        let mut report = [0u8; 2048];
        let mut rlen = report.len() as u32;
        bad += unsafe {
            pm_wasmmod_registry_bench_run_all(
                fqn.as_ptr(),
                fqn.len() as u32,
                iterations,
                report.as_mut_ptr(),
                &mut rlen,
            )
        };
        if rlen > report.len() as u32 {
            rlen = report.len() as u32;
        }
        let s = core::str::from_utf8(&report[..rlen as usize]).unwrap_or("<bad utf8>");
        println!("{}", s.trim_end());
    }
    unsafe { pm_wasmmod_registry_set_bench_clock(None) };

    if filter.is_some() && ran == 0 {
        panic!("WASMMOD_BENCH_FQN matched no module with benches");
    }
    assert_eq!(bad, 0, "{bad} module bench(es) failed to run ({ran} ran)");
}
