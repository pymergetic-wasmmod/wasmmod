//! pymergetic.wasmmod.registry — module tests (`__tests__.rs`).

use super::*;
use crate::util::mod_test::case;

fn publish(fqn: &str, kind: pm_wasmmod_registry_container_kind_t) -> pm_wasmmod_registry_handle_t {
    TABLE.lock().publish(fqn, kind)
}

/// Ensure a module is live, mirroring what the native registrar does via
/// `ensure_resident` (the wasm registrar does not auto-ensure).
fn pub_ensure(fqn: &str) {
    TABLE.lock().ensure_resident(fqn);
}

fn version_set_and_query_roundtrip() {
    let _ = publish("test.ver", pm_wasmmod_registry_container_kind_t::Resident);
    assert!(TABLE.lock().set_version("test.ver", "1.2.3"));
    assert_eq!(TABLE.lock().version_of("test.ver"), Some("1.2.3"));
    let h = TABLE.lock().publish_ver(
        "test.ver2",
        pm_wasmmod_registry_container_kind_t::Wasm,
        "9.0.0",
    );
    assert_ne!(h.index, u32::MAX);
    assert_eq!(TABLE.lock().version_of("test.ver2"), Some("9.0.0"));
}

fn export_sig_and_enumerate_roundtrip() {
    let handle = publish(
        "test.enum_sig",
        pm_wasmmod_registry_container_kind_t::Resident,
    );
    assert!(TABLE.lock().export_set_sig(
        handle,
        "add",
        pm_wasmmod_registry_export_kind_t::Fn,
        core::ptr::dangling_mut::<c_void>(),
        Some("int(int, int)"),
    ));
    assert!(pm_wasmmod_registry_module_count() >= 1);
    let mut name = [0u8; 64];
    let mut name_len = 64u32;
    assert_eq!(
        unsafe { pm_wasmmod_registry_module_at(0, name.as_mut_ptr(), &mut name_len) },
        1
    );
    let n = unsafe { pm_wasmmod_registry_export_count(b"test.enum_sig".as_ptr(), 13) };
    assert_eq!(n, 1);
    let mut ename = [0u8; 32];
    let mut ename_len = 32u32;
    let mut kind = pm_wasmmod_registry_export_kind_t::Obj;
    let mut sig = [0u8; 64];
    let mut sig_len = 64u32;
    assert_eq!(
        unsafe {
            pm_wasmmod_registry_export_at(
                b"test.enum_sig".as_ptr(),
                13,
                0,
                ename.as_mut_ptr(),
                &mut ename_len,
                &mut kind,
                sig.as_mut_ptr(),
                &mut sig_len,
            )
        },
        1
    );
    assert_eq!(kind, pm_wasmmod_registry_export_kind_t::Fn);
    assert_eq!(&ename[..ename_len as usize], b"add");
    assert_eq!(&sig[..sig_len as usize], b"int(int, int)");
}

fn mod_export_ensures_resident() {
    assert_eq!(
        unsafe {
            pm_wasmmod_registry_mod_export(
                b"test.mod_exp".as_ptr(),
                12,
                b"ping".as_ptr(),
                4,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                b"int(void)".as_ptr(),
                9,
            )
        },
        1
    );
    assert!(unsafe { pm_wasmmod_registry_has(b"test.mod_exp".as_ptr(), 12) } != 0);
    assert_eq!(
        TABLE.lock().container_of("test.mod_exp"),
        Some(pm_wasmmod_registry_container_kind_t::Resident)
    );
}

fn publish_then_resolve_roundtrips() {
    let handle = publish("test.pub_resolve", pm_wasmmod_registry_container_kind_t::Resident);
    let sentinel = 0x1234usize as *mut c_void;
    assert!(TABLE.lock().export_set(handle, "f", pm_wasmmod_registry_export_kind_t::Fn, sentinel));
    assert_eq!(TABLE.lock().resolve_native("test.pub_resolve", "f"), sentinel);
}

fn unpublish_invalidates_the_old_handle_even_after_slot_reuse() {
    let first = publish("test.gen_a", pm_wasmmod_registry_container_kind_t::Wasm);
    assert!(TABLE.lock().unpublish(first));
    let second = publish("test.gen_b", pm_wasmmod_registry_container_kind_t::Elf);
    // Slot reused (same index), but the generation moved — the old
    // handle must not be mistaken for the new occupant.
    assert_eq!(first.index, second.index);
    assert_ne!(first.generation, second.generation);
    assert!(!TABLE.lock().export_set(first, "x", pm_wasmmod_registry_export_kind_t::Fn, core::ptr::null_mut()));
}

fn resolve_native_is_null_for_unknown_module_or_export() {
    assert!(TABLE.lock().resolve_native("test.does_not_exist", "f").is_null());
    let handle = publish("test.known_no_export", pm_wasmmod_registry_container_kind_t::Aot);
    let _ = handle;
    assert!(TABLE.lock().resolve_native("test.known_no_export", "missing").is_null());
}

fn call_roundtrips_through_the_fn_t_convention() {
    unsafe extern "C" fn add_one(
        args: *const pm_wasmmod_registry_value_t,
        nargs: u32,
        results: *mut pm_wasmmod_registry_value_t,
        nresults: u32,
    ) -> i32 {
        assert_eq!(nargs, 1);
        assert_eq!(nresults, 1);
        let arg = unsafe { &*args };
        let sum = unsafe { arg.of.i32 } + 1;
        unsafe {
            (*results).kind = pm_wasmmod_registry_valkind_t::I32;
            (*results).of.i32 = sum;
        }
        0
    }

    let handle = publish("test.call", pm_wasmmod_registry_container_kind_t::Resident);
    assert!(TABLE.lock().export_set(
        handle,
        "add_one",
        pm_wasmmod_registry_export_kind_t::Fn,
        add_one as *mut c_void,
    ));

    let arg = pm_wasmmod_registry_value_t {
        kind: pm_wasmmod_registry_valkind_t::I32,
        of: pm_wasmmod_registry_value_of_t { i32: 41 },
    };
    let mut result = pm_wasmmod_registry_value_t {
        kind: pm_wasmmod_registry_valkind_t::I32,
        of: pm_wasmmod_registry_value_of_t { i32: 0 },
    };
    let status = unsafe {
        pm_wasmmod_registry_call(
            "test.call".as_ptr(),
            "test.call".len() as u32,
            "add_one".as_ptr(),
            "add_one".len() as u32,
            &arg,
            1,
            &mut result,
            1,
        )
    };
    assert_eq!(status, 0);
    assert_eq!(unsafe { result.of.i32 }, 42);
}

fn call_is_negative_one_for_unknown_module_or_export() {
    let status = unsafe {
        pm_wasmmod_registry_call(
            "test.call_missing".as_ptr(),
            "test.call_missing".len() as u32,
            "f".as_ptr(),
            1,
            core::ptr::null(),
            0,
            core::ptr::null_mut(),
            0,
        )
    };
    assert_eq!(status, -1);
}

fn gc_visit_only_sees_live_obj_exports() {
    let handle = publish("test.gc", pm_wasmmod_registry_container_kind_t::Resident);
    let token = 0x9999usize as *mut c_void;
    assert!(TABLE.lock().export_set(handle, "o", pm_wasmmod_registry_export_kind_t::Obj, token));
    assert!(TABLE.lock().export_set(
        handle,
        "f",
        pm_wasmmod_registry_export_kind_t::Fn,
        std::ptr::dangling_mut::<c_void>()
    ));

    // No static needed: `ctx` is exactly for this — a caller-owned
    // pointer round-tripped back into the callback, here a `Vec` the
    // test collects into instead of a `static mut` (denied outright
    // under the 2024 edition, and this is the intended pattern
    // anyway, not a workaround).
    extern "C" fn collect(token: *mut c_void, ctx: *mut c_void) {
        let seen = unsafe { &mut *(ctx as *mut Vec<*mut c_void>) };
        seen.push(token);
    }

    let mut seen: Vec<*mut c_void> = Vec::new();
    TABLE.lock().gc_visit(collect, &mut seen as *mut _ as *mut c_void);
    assert!(seen.contains(&token));
}

unsafe extern "C" fn case_version_set_and_query_roundtrip() -> i32 {
    case(|| version_set_and_query_roundtrip())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "version_set_and_query_roundtrip",
    case_version_set_and_query_roundtrip
);
#[test]
fn test_version_set_and_query_roundtrip() {
    assert_eq!(unsafe { case_version_set_and_query_roundtrip() }, 0);
}

unsafe extern "C" fn case_export_sig_and_enumerate_roundtrip() -> i32 {
    case(|| export_sig_and_enumerate_roundtrip())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "export_sig_and_enumerate_roundtrip",
    case_export_sig_and_enumerate_roundtrip
);
#[test]
fn test_export_sig_and_enumerate_roundtrip() {
    assert_eq!(unsafe { case_export_sig_and_enumerate_roundtrip() }, 0);
}

unsafe extern "C" fn case_mod_export_ensures_resident() -> i32 {
    case(|| mod_export_ensures_resident())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "mod_export_ensures_resident",
    case_mod_export_ensures_resident
);
#[test]
fn test_mod_export_ensures_resident() {
    assert_eq!(unsafe { case_mod_export_ensures_resident() }, 0);
}

unsafe extern "C" fn case_publish_then_resolve_roundtrips() -> i32 {
    case(|| publish_then_resolve_roundtrips())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "publish_then_resolve_roundtrips", case_publish_then_resolve_roundtrips);
#[test]
fn test_publish_then_resolve_roundtrips() {
    assert_eq!(unsafe { case_publish_then_resolve_roundtrips() }, 0);
}

unsafe extern "C" fn case_unpublish_invalidates_the_old_handle_even_after_slot_reuse() -> i32 {
    case(|| unpublish_invalidates_the_old_handle_even_after_slot_reuse())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "unpublish_invalidates_the_old_handle_even_after_slot_reuse", case_unpublish_invalidates_the_old_handle_even_after_slot_reuse);
#[test]
fn test_unpublish_invalidates_the_old_handle_even_after_slot_reuse() {
    assert_eq!(unsafe { case_unpublish_invalidates_the_old_handle_even_after_slot_reuse() }, 0);
}

unsafe extern "C" fn case_resolve_native_is_null_for_unknown_module_or_export() -> i32 {
    case(|| resolve_native_is_null_for_unknown_module_or_export())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "resolve_native_is_null_for_unknown_module_or_export", case_resolve_native_is_null_for_unknown_module_or_export);
#[test]
fn test_resolve_native_is_null_for_unknown_module_or_export() {
    assert_eq!(unsafe { case_resolve_native_is_null_for_unknown_module_or_export() }, 0);
}

unsafe extern "C" fn case_call_roundtrips_through_the_fn_t_convention() -> i32 {
    case(|| call_roundtrips_through_the_fn_t_convention())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "call_roundtrips_through_the_fn_t_convention", case_call_roundtrips_through_the_fn_t_convention);
#[test]
fn test_call_roundtrips_through_the_fn_t_convention() {
    assert_eq!(unsafe { case_call_roundtrips_through_the_fn_t_convention() }, 0);
}

unsafe extern "C" fn case_call_is_negative_one_for_unknown_module_or_export() -> i32 {
    case(|| call_is_negative_one_for_unknown_module_or_export())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "call_is_negative_one_for_unknown_module_or_export", case_call_is_negative_one_for_unknown_module_or_export);
#[test]
fn test_call_is_negative_one_for_unknown_module_or_export() {
    assert_eq!(unsafe { case_call_is_negative_one_for_unknown_module_or_export() }, 0);
}

unsafe extern "C" fn case_gc_visit_only_sees_live_obj_exports() -> i32 {
    case(|| gc_visit_only_sees_live_obj_exports())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "gc_visit_only_sees_live_obj_exports", case_gc_visit_only_sees_live_obj_exports);
#[test]
fn test_gc_visit_only_sees_live_obj_exports() {
    assert_eq!(unsafe { case_gc_visit_only_sees_live_obj_exports() }, 0);
}

// --- bench registry ------------------------------------------------------
//
// A bench is `fn(iterations) -> i32`; the registry times `iterations` ops.
// The clock is a per-seat fill set through `pm_wasmmod_registry_set_bench_clock`.
// These cases use a deterministic fake clock so ns/op is exact: the clock
// advances 1024 us per call, which is 1024*1000 ns for any non-zero op count.

static mut FAKE_CLOCK: u64 = 0;

unsafe extern "C" fn fake_clock_us() -> u64 {
    unsafe {
        FAKE_CLOCK += 1024;
        FAKE_CLOCK
    }
}

/// One op: must be called exactly `iterations` times across warmup + measure.
unsafe extern "C" fn case_bench_add(iterations: u64) -> i32 {
    for _ in 0..iterations {
        core::hint::black_box(1u64.wrapping_add(1));
    }
    0
}

unsafe extern "C" fn case_bench_fail(iterations: u64) -> i32 {
    let _ = iterations;
    -1
}

fn bench_fqn() -> &'static str {
    "pymergetic.wasmmod.registry.__bench_delegate"
}

/// Register `add`/`fail` under `bench_fqn`.
fn bench_register_local() {
    unsafe {
        assert_eq!(
            pm_wasmmod_registry_bench_register(
                bench_fqn().as_ptr(),
                bench_fqn().len() as u32,
                b"add".as_ptr(),
                3,
                Some(case_bench_add),
            ),
            1
        );
        assert_eq!(
            pm_wasmmod_registry_bench_register(
                bench_fqn().as_ptr(),
                bench_fqn().len() as u32,
                b"fail".as_ptr(),
                4,
                Some(case_bench_fail),
            ),
            1
        );
    }
}

fn bench_roundtrip_register_and_count() {
    unsafe {
        assert_eq!(
            pm_wasmmod_registry_bench_count(bench_fqn().as_ptr(), bench_fqn().len() as u32),
            2
        );
    }
}

fn bench_clock_owned_by_registry() {
    unsafe {
        // No clock installed → bench reports "no clock" (-1), not a number.
        let rc = pm_wasmmod_registry_bench_run(
            bench_fqn().as_ptr(),
            bench_fqn().len() as u32,
            b"add".as_ptr(),
            3,
            10,
        );
        assert_eq!(rc, -1);

        pm_wasmmod_registry_set_bench_clock(Some(fake_clock_us));
        // fake_clock ticks +1024 us per call. Warmup does not touch the clock,
        // so the measured lap is exactly the two `bench_now()` grabs in
        // time_bench_ns → 1024 us of growth → 1024*1000/16 = 64000 ns/op.
        let rc = pm_wasmmod_registry_bench_run(
            bench_fqn().as_ptr(),
            bench_fqn().len() as u32,
            b"add".as_ptr(),
            3,
            16,
        );
        assert_eq!(rc, 64000);

        // A failing bench surfaces as FAILED (-3), not a number.
        let rc = pm_wasmmod_registry_bench_run(
            bench_fqn().as_ptr(),
            bench_fqn().len() as u32,
            b"fail".as_ptr(),
            4,
            8,
        );
        assert_eq!(rc, -3);
        pm_wasmmod_registry_set_bench_clock(None);
    }
}

/// One serial case: register, count, then the no-clock / timed / FAILED paths.
/// Kept as a single `#[test]` because the fake clock is a shared static and
/// the registry table is global — batching would race both.
unsafe extern "C" fn case_bench_register_times_and_clears() -> i32 {
    unsafe { FAKE_CLOCK = 0 };
    case(|| {
        bench_register_local();
        bench_roundtrip_register_and_count();
        bench_clock_owned_by_registry();
    })
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "bench_register_times_and_clears", case_bench_register_times_and_clears);
#[test]
fn test_bench_register_times_and_clears() {
    assert_eq!(unsafe { case_bench_register_times_and_clears() }, 0);
}

// --- wasm-guest bench dispatcher -------------------------------------------
// A `WasmExport` bench is timed through the loader's bench-runner hook, the
// same way a guest pack test goes through `WASM_TEST_RUNNER`. This proves the
// fill actually routes (and that a missing runner surfaces FAILED, not a
// silent pass / fake number).

/// Stub state: recorded export + iterations it was asked to drive, call count.
/// Mutex-guarded (std is available in tests) so no `static mut` refs.
static STUB: std::sync::Mutex<Stub> = std::sync::Mutex::new(Stub {
    export: [0; 32],
    export_len: 0,
    iterations: 0,
    calls: 0,
});

#[derive(Clone)]
struct Stub {
    export: [u8; 32],
    export_len: usize,
    iterations: u64,
    calls: u32,
}

/// Mirrors what the loader trampoline does: drive the guest export.
unsafe extern "C" fn stub_bench_runner(
    _fqn: *const u8,
    _fqn_len: u32,
    export_name: *const u8,
    export_len: u32,
    iterations: u64,
) -> i32 {
    unsafe {
        let mut s = STUB.lock().unwrap_or_else(|e| e.into_inner());
        s.export_len = (export_len as usize).min(s.export.len());
        core::ptr::copy_nonoverlapping(
            export_name,
            s.export.as_mut_ptr(),
            s.export_len,
        );
        s.iterations = iterations;
        s.calls += 1;
        for _ in 0..iterations {
            core::hint::black_box(2u64.wrapping_mul(2));
        }
    }
    0
}

fn stub_snapshot() -> Stub {
    STUB.lock().unwrap_or_else(|e| e.into_inner()).clone()
}

fn stub_reset() {
    *STUB.lock().unwrap_or_else(|e| e.into_inner()) = Stub {
        export: [0; 32],
        export_len: 0,
        iterations: 0,
        calls: 0,
    };
}

fn wasm_bench_fqn() -> &'static str {
    "pymergetic.wasmmod.registry.__bench_wasm_delegate"
}

unsafe extern "C" fn case_wasm_bench_dispatches_through_runner() -> i32 {
    case(|| {
        unsafe {
            // A guest registers into a module the loader already published.
            pub_ensure(wasm_bench_fqn());

            // Register a guest bench (export name, not a native fn pointer).
            assert_eq!(
                pm_wasmmod_registry_bench_register_wasm(
                    wasm_bench_fqn().as_ptr(),
                    wasm_bench_fqn().len() as u32,
                    b"guest_add".as_ptr(),
                    9,
                    b"guest_bench_add".as_ptr(),
                    15,
                ),
                1
            );

            // No clock installed yet → "no clock", not a number.
            let rc = pm_wasmmod_registry_bench_run(
                wasm_bench_fqn().as_ptr(),
                wasm_bench_fqn().len() as u32,
                b"guest_add".as_ptr(),
                9,
                10,
            );
            assert_eq!(rc, -1);

            pm_wasmmod_registry_set_bench_clock(Some(fake_clock_us));
            // No bench-runner installed → guest bench FAILED (-3), not a fake
            // number and not a silent pass.
            stub_reset();
            let rc = pm_wasmmod_registry_bench_run(
                wasm_bench_fqn().as_ptr(),
                wasm_bench_fqn().len() as u32,
                b"guest_add".as_ptr(),
                9,
                16,
            );
            assert_eq!(rc, -3);
            assert_eq!(stub_snapshot().calls, 0);

            // Install the runner → the export is invoked (warmup=2 + measure=16
            // calls), and we get the same ns/op a native bench would: the fake
            // clock grows 1024 us across the two grabs → 64000 ns/op @ 16.
            pm_wasmmod_registry_set_wasm_bench_runner(Some(stub_bench_runner));
            stub_reset();
            let rc = pm_wasmmod_registry_bench_run(
                wasm_bench_fqn().as_ptr(),
                wasm_bench_fqn().len() as u32,
                b"guest_add".as_ptr(),
                9,
                16,
            );
            assert_eq!(rc, 64000);
            let s = stub_snapshot();
            assert_eq!(s.calls, 2, "warmup + measured lap each drive the export");
            assert_eq!(&s.export[..s.export_len], b"guest_bench_add");
            assert_eq!(s.iterations, 16, "the measured lap's iteration count");

            // Tear down: drop the runner and clock so later cases are pristine.
            pm_wasmmod_registry_set_wasm_bench_runner(None);
            pm_wasmmod_registry_set_bench_clock(None);
        }
    })
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "wasm_bench_dispatches_through_runner",
    case_wasm_bench_dispatches_through_runner
);
#[test]
fn test_wasm_bench_dispatches_through_runner() {
    assert_eq!(unsafe { case_wasm_bench_dispatches_through_runner() }, 0);
}



