//! Host harness for module `__tests__.*` registered via `PM_MOD_TEST_*`.
//!
//! Walks the live registry test table and runs every case. Filter with
//! `WASMMOD_TEST_FQN=dotted.fqn` to run one module.
//!
//! [`case`] takes [`TEST_LOCK`] so cargo's parallel `#[test]` threads cannot
//! race the shared registry / temp AOT files. The harness itself must **not**
//! hold that lock while invoking cases (non-reentrant).

use std::sync::Mutex;

use crate::wasmmod::registry::{
    pm_wasmmod_registry_module_at, pm_wasmmod_registry_module_count,
    pm_wasmmod_registry_test_count, pm_wasmmod_registry_test_run_all,
};

/// Serializes module test cases across cargo's thread pool.
pub static TEST_LOCK: Mutex<()> = Mutex::new(());

/// WAMR installs guard pages and an alternate signal stack on each caller-created
/// thread. Rust's test runner reuses stack addresses across short-lived threads;
/// leaving that setup behind can fault in WAMR's next stack-page probe.
struct ThreadEnvCleanup;

impl Drop for ThreadEnvCleanup {
    fn drop(&mut self) {
        if unsafe { crate::wasmmod::loader::wasm_runtime_thread_env_inited() } {
            unsafe { crate::wasmmod::loader::wasm_runtime_destroy_thread_env() };
        }
    }
}

/// Run `f`; map panic → fail (`1`), success → `0`. For wrapping assert!-heavy cases.
pub fn case(f: impl FnOnce()) -> i32 {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _thread_env = ThreadEnvCleanup;
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
        Ok(()) => 0,
        Err(_) => 1,
    }
}

/// Closure-free `case`: same TEST_LOCK serialization + panic containment,
/// but the body returns its own rc (`0` = ok) instead of asserting. This is
/// the form the rsx-subset cards use — their `case_*` trampolines have no
/// closures to pass here, and an `extern "C"` fn cannot unwind a panic, so
/// the catch_unwind is what keeps a failing assertion from aborting the
/// whole test binary.
pub unsafe fn case_rc(f: unsafe extern "C" fn() -> i32) -> i32 {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let _thread_env = ThreadEnvCleanup;
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| unsafe { f() })) {
        Ok(rc) => rc,
        Err(_) => 1,
    }
}

#[test]
fn registry_mod_tests_all() {
    let filter = std::env::var("WASMMOD_TEST_FQN").ok();
    let n = pm_wasmmod_registry_module_count();
    let mut ran = 0u32;
    let mut fails = 0i32;
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
        let tc = unsafe { pm_wasmmod_registry_test_count(fqn.as_ptr(), fqn.len() as u32) };
        if tc == 0 {
            continue;
        }
        ran += tc;
        fails += unsafe { pm_wasmmod_registry_test_run_all(fqn.as_ptr(), fqn.len() as u32) };
    }
    if filter.is_some() && ran == 0 {
        panic!("WASMMOD_TEST_FQN matched no module with tests");
    }
    assert_eq!(fails, 0, "{fails} module test case(s) failed ({ran} ran)");
}
