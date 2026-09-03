//! pymergetic.wasmmod.registry — module tests (`__tests__.rs`).
//!
//! Same ptr+len discipline as the muscle: byte-span assertions, no
//! closures, no `String`/`Vec` state, no `std::sync::Mutex` — a fixed
//! stub struct under the table's own lock discipline (raw acquire/
//! release). Tests run on the cargo host build (cfg(test)); the in-
//! kernel rsx compile skips this face by design (the build card's
//! splice skips cfg(test)-guarded path-includes).

use super::*;

unsafe fn zlen(p: *const u8) -> usize {
    unsafe {
        let mut n = 0usize;
        while *p.add(n) != 0 {
            n += 1;
        }
        n
    }
}

unsafe fn span_is(want: *const u8, buf: *const u8, len: u32) -> bool {
    unsafe { span_eq(want, zlen(want), buf, len as usize) }
}

fn version_set_and_query_roundtrip() {
    unsafe {
        let h = pm_wasmmod_registry_publish(
            b"test.ver\0".as_ptr(),
            8,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        assert_ne!(h.index, u32::MAX);
        assert_eq!(
            pm_wasmmod_registry_set_version(
                b"test.ver\0".as_ptr(),
                8,
                b"1.2.3\0".as_ptr(),
                5,
            ),
            1
        );
        let mut ver = [0u8; 32];
        let mut ver_len = 32u32;
        assert_eq!(
            pm_wasmmod_registry_version(b"test.ver\0".as_ptr(), 8, ver.as_mut_ptr(), &mut ver_len),
            1
        );
        assert!(span_is(b"1.2.3\0".as_ptr(), ver.as_ptr(), ver_len));
        let h = pm_wasmmod_registry_publish_ver(
            b"test.ver2\0".as_ptr(),
            9,
            pm_wasmmod_registry_container_kind_t::Wasm,
            b"9.0.0\0".as_ptr(),
            5,
        );
        assert_ne!(h.index, u32::MAX);
        let mut ver2 = [0u8; 32];
        let mut ver2_len = 32u32;
        assert_eq!(
            pm_wasmmod_registry_version(
                b"test.ver2\0".as_ptr(),
                9,
                ver2.as_mut_ptr(),
                &mut ver2_len
            ),
            1
        );
        assert!(span_is(b"9.0.0\0".as_ptr(), ver2.as_ptr(), ver2_len));
    }
}

fn export_sig_and_enumerate_roundtrip() {
    unsafe {
        let handle = pm_wasmmod_registry_publish(
            b"test.enum_sig\0".as_ptr(),
            13,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        assert_eq!(
            pm_wasmmod_registry_export_set_sig(
                handle,
                b"add\0".as_ptr(),
                3,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                b"int(int, int)\0".as_ptr(),
                13,
            ),
            1
        );
        assert!(pm_wasmmod_registry_module_count() >= 1);
        let mut name = [0u8; 64];
        let mut name_len = 64u32;
        assert_eq!(
            pm_wasmmod_registry_module_at(0, name.as_mut_ptr(), &mut name_len),
            1
        );
        assert_eq!(
            pm_wasmmod_registry_export_count(b"test.enum_sig\0".as_ptr(), 13),
            1
        );
        let mut ename = [0u8; 32];
        let mut ename_len = 32u32;
        let mut kind = pm_wasmmod_registry_export_kind_t::Obj;
        let mut sig = [0u8; 64];
        let mut sig_len = 64u32;
        assert_eq!(
            pm_wasmmod_registry_export_at(
                b"test.enum_sig\0".as_ptr(),
                13,
                0,
                ename.as_mut_ptr(),
                &mut ename_len,
                &mut kind,
                sig.as_mut_ptr(),
                &mut sig_len,
            ),
            1
        );
        assert_eq!(kind, pm_wasmmod_registry_export_kind_t::Fn);
        assert!(span_is(b"add\0".as_ptr(), ename.as_ptr(), ename_len));
        assert!(span_is(b"int(int, int)\0".as_ptr(), sig.as_ptr(), sig_len));
    }
}

fn mod_export_ensures_resident() {
    unsafe {
        assert_eq!(
            pm_wasmmod_registry_mod_export(
                b"test.mod_exp\0".as_ptr(),
                12,
                b"ping\0".as_ptr(),
                4,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                b"int(void)\0".as_ptr(),
                9,
            ),
            1
        );
        assert_ne!(pm_wasmmod_registry_has(b"test.mod_exp\0".as_ptr(), 12), 0);
        assert_eq!(
            pm_wasmmod_registry_container(b"test.mod_exp\0".as_ptr(), 12),
            pm_wasmmod_registry_container_kind_t::Resident as i32
        );
    }
}

fn publish_then_resolve_roundtrips() {
    unsafe {
        let handle = pm_wasmmod_registry_publish(
            b"test.pub_resolve\0".as_ptr(),
            16,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        let sentinel = 0x1234usize as *mut c_void;
        assert_eq!(
            pm_wasmmod_registry_export_set(
                handle,
                b"f\0".as_ptr(),
                1,
                pm_wasmmod_registry_export_kind_t::Fn,
                sentinel,
            ),
            1
        );
        assert_eq!(
            pm_wasmmod_registry_resolve_native(
                b"test.pub_resolve\0".as_ptr(),
                16,
                b"f\0".as_ptr(),
                1,
            ),
            sentinel
        );
    }
}

fn unpublish_invalidates_the_old_handle_even_after_slot_reuse() {
    unsafe {
        let first = pm_wasmmod_registry_publish(
            b"test.gen_a\0".as_ptr(),
            10,
            pm_wasmmod_registry_container_kind_t::Wasm,
        );
        assert_eq!(pm_wasmmod_registry_unpublish(first), 1);
        let second = pm_wasmmod_registry_publish(
            b"test.gen_b\0".as_ptr(),
            10,
            pm_wasmmod_registry_container_kind_t::Elf,
        );
        // Slot reused (same index), but the generation moved — the old
        // handle must not be mistaken for the new occupant.
        assert_eq!(first.index, second.index);
        assert_ne!(first.generation, second.generation);
        assert_eq!(
            pm_wasmmod_registry_export_set(
                first,
                b"x\0".as_ptr(),
                1,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::null_mut(),
            ),
            0
        );
    }
}

fn resolve_native_is_null_for_unknown_module_or_export() {
    unsafe {
        assert!(
            pm_wasmmod_registry_resolve_native(
                b"test.does_not_exist\0".as_ptr(),
                19,
                b"f\0".as_ptr(),
                1,
            )
            .is_null()
        );
        let _ = pm_wasmmod_registry_publish(
            b"test.known_no_export\0".as_ptr(),
            20,
            pm_wasmmod_registry_container_kind_t::Aot,
        );
        assert!(
            pm_wasmmod_registry_resolve_native(
                b"test.known_no_export\0".as_ptr(),
                20,
                b"missing\0".as_ptr(),
                7,
            )
            .is_null()
        );
    }
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

    unsafe {
        let handle = pm_wasmmod_registry_publish(
            b"test.call\0".as_ptr(),
            9,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        assert_eq!(
            pm_wasmmod_registry_export_set(
                handle,
                b"add_one\0".as_ptr(),
                7,
                pm_wasmmod_registry_export_kind_t::Fn,
                add_one as *mut c_void,
            ),
            1
        );

        let arg = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: pm_wasmmod_registry_value_of_t { i32: 41 },
        };
        let mut result = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status = pm_wasmmod_registry_call(
            b"test.call\0".as_ptr(),
            9,
            b"add_one\0".as_ptr(),
            7,
            &arg,
            1,
            &mut result,
            1,
        );
        assert_eq!(status, 0);
        assert_eq!(unsafe { result.of.i32 }, 42);
    }
}

fn call_is_negative_one_for_unknown_module_or_export() {
    unsafe {
        let status = pm_wasmmod_registry_call(
            b"test.call_missing\0".as_ptr(),
            17,
            b"f\0".as_ptr(),
            1,
            core::ptr::null(),
            0,
            core::ptr::null_mut(),
            0,
        );
        assert_eq!(status, -1);
    }
}

fn gc_visit_only_sees_live_obj_exports() {
    /* ctx is the caller-owned pointer round-tripped back into the
     * callback; here a plain counter a fixed test static owns — no
     * Vec, no static mut references (the 2024 edition denies them). */
    struct Seen {
        token: *mut c_void,
        hits: u32,
    }
    struct SeenCell(UnsafeCell<Seen>);
    unsafe impl Sync for SeenCell {}
    static SEEN: SeenCell = SeenCell(UnsafeCell::new(Seen {
        token: core::ptr::null_mut(),
        hits: 0,
    }));

    extern "C" fn collect(token: *mut c_void, ctx: *mut c_void) {
        let seen = unsafe { &mut *(ctx as *mut Seen) };
        seen.hits += 1;
        seen.token = token;
    }

    unsafe {
        let handle = pm_wasmmod_registry_publish(
            b"test.gc\0".as_ptr(),
            7,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        let token = 0x9999usize as *mut c_void;
        assert_eq!(
            pm_wasmmod_registry_export_set(
                handle,
                b"o\0".as_ptr(),
                1,
                pm_wasmmod_registry_export_kind_t::Obj,
                token,
            ),
            1
        );
        assert_eq!(
            pm_wasmmod_registry_export_set(
                handle,
                b"f\0".as_ptr(),
                1,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
            ),
            1
        );

        let cell = SEEN.0.get();
        (*cell).hits = 0;
        (*cell).token = core::ptr::null_mut();
        pm_wasmmod_registry_gc_visit(collect, cell as *mut c_void);
        /* Membership, not exact count: the table is shared with every
         * other card's PM_TYPE_DEFINE/registration ctors, so other Obj
         * exports may be visited too. Our token must be among them. */
        assert_eq!((*cell).token, token);
        assert!((*cell).hits >= 1, "the Obj export is visited");
    }
}

/* ---- bench registry ------------------------------------------------------
 *
 * A bench is `fn(iterations) -> i32`; the registry times `iterations` ops.
 * The clock is a per-seat fill set through `pm_wasmmod_registry_set_bench_clock`.
 * These cases use a deterministic fake clock so ns/op is exact: the clock
 * advances 1024 us per call, which is 1024*1000 ns for any non-zero op count.
 * The fake clock state is a fixed cell (no static mut references). */

struct FakeClock(UnsafeCell<u64>);
unsafe impl Sync for FakeClock {}
static FAKE_CLOCK: FakeClock = FakeClock(UnsafeCell::new(0));

unsafe extern "C" fn fake_clock_us() -> u64 {
    unsafe {
        let c = FAKE_CLOCK.0.get();
        *c += 1024;
        *c
    }
}

/// One op: must be called exactly `iterations` times across warmup + measure.
unsafe extern "C" fn case_bench_add(iterations: u64) -> i32 {
    unsafe {
        let mut i = 0u64;
        while i < iterations {
            core::hint::black_box(1u64.wrapping_add(1));
            i += 1;
        }
    }
    0
}

unsafe extern "C" fn case_bench_fail(iterations: u64) -> i32 {
    let _ = iterations;
    -1
}

const BENCH_FQN: &[u8] = b"pymergetic.wasmmod.registry.__bench_delegate\0";

fn bench_register_local() {
    unsafe {
        assert_eq!(
            pm_wasmmod_registry_bench_register(
                BENCH_FQN.as_ptr(),
                43,
                b"add\0".as_ptr(),
                3,
                Some(case_bench_add),
            ),
            1
        );
        assert_eq!(
            pm_wasmmod_registry_bench_register(
                BENCH_FQN.as_ptr(),
                43,
                b"fail\0".as_ptr(),
                4,
                Some(case_bench_fail),
            ),
            1
        );
    }
}

fn bench_roundtrip_register_and_count() {
    unsafe {
        assert_eq!(pm_wasmmod_registry_bench_count(BENCH_FQN.as_ptr(), 43), 2);
    }
}

fn bench_clock_owned_by_registry() {
    unsafe {
        // No clock installed → bench reports "no clock" (-1), not a number.
        let rc = pm_wasmmod_registry_bench_run(
            BENCH_FQN.as_ptr(),
            43,
            b"add\0".as_ptr(),
            3,
            10,
        );
        assert_eq!(rc, -1);

        pm_wasmmod_registry_set_bench_clock(Some(fake_clock_us));
        // fake_clock ticks +1024 us per call. Warmup does not touch the clock,
        // so the measured lap is exactly the two `bench_now()` grabs in
        // time_bench_ns → 1024 us of growth → 1024*1000/16 = 64000 ns/op.
        let rc = pm_wasmmod_registry_bench_run(
            BENCH_FQN.as_ptr(),
            43,
            b"add\0".as_ptr(),
            3,
            16,
        );
        assert_eq!(rc, 64000);

        // A failing bench surfaces as FAILED (-3), not a number.
        let rc = pm_wasmmod_registry_bench_run(
            BENCH_FQN.as_ptr(),
            43,
            b"fail\0".as_ptr(),
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
    unsafe { crate::util::mod_test::case_rc(case_bench_register_times_and_clears_body) }
}
unsafe extern "C" fn case_bench_register_times_and_clears_body() -> i32 {
    unsafe {
        *FAKE_CLOCK.0.get() = 0;
        bench_register_local();
        bench_roundtrip_register_and_count();
        bench_clock_owned_by_registry();
    }
    0
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "bench_register_times_and_clears",
    case_bench_register_times_and_clears
);
#[test]
fn test_bench_register_times_and_clears() {
    assert_eq!(unsafe { case_bench_register_times_and_clears() }, 0);
}

/* ---- wasm-guest bench dispatcher -------------------------------------------
 * A `WasmExport` bench is timed through the loader's bench-runner hook, the
 * same way a guest pack test goes through `WASM_TEST_RUNNER`. This proves the
 * fill actually routes (and that a missing runner surfaces FAILED, not a
 * silent pass / fake number). Stub state is a fixed cell (no std Mutex, no
 * static mut references). */

#[derive(Clone, Copy)]
struct Stub {
    export: [u8; 32],
    export_len: usize,
    iterations: u64,
    calls: u32,
}

struct StubCell(UnsafeCell<Stub>);
unsafe impl Sync for StubCell {}
static STUB: StubCell = StubCell(UnsafeCell::new(Stub {
    export: [0; 32],
    export_len: 0,
    iterations: 0,
    calls: 0,
}));

/// Mirrors what the loader trampoline does: drive the guest export.
unsafe extern "C" fn stub_bench_runner(
    _fqn: *const u8,
    _fqn_len: u32,
    export_name: *const u8,
    export_len: u32,
    iterations: u64,
) -> i32 {
    unsafe {
        let s = STUB.0.get();
        let n = if (export_len as usize) < 32 {
            export_len as usize
        } else {
            32
        };
        core::ptr::copy_nonoverlapping(export_name, (*s).export.as_mut_ptr(), n);
        (*s).export_len = n;
        (*s).iterations = iterations;
        (*s).calls += 1;
        let mut i = 0u64;
        while i < iterations {
            core::hint::black_box(2u64.wrapping_mul(2));
            i += 1;
        }
    }
    0
}

fn stub_reset() {
    unsafe {
        let s = STUB.0.get();
        (*s).export = [0; 32];
        (*s).export_len = 0;
        (*s).iterations = 0;
        (*s).calls = 0;
    }
}

const WASM_BENCH_FQN: &[u8] = b"pymergetic.wasmmod.registry.__bench_wasm_delegate\0";

unsafe extern "C" fn case_wasm_bench_dispatches_through_runner() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_wasm_bench_dispatches_through_runner_body) }
}
unsafe extern "C" fn case_wasm_bench_dispatches_through_runner_body() -> i32 {
    unsafe {
        // A guest registers into a module the loader already published.
        let _ = pm_wasmmod_registry_ensure(
            WASM_BENCH_FQN.as_ptr(),
            48,
            pm_wasmmod_registry_container_kind_t::Resident,
        );

        // Register a guest bench (export name, not a native fn pointer).
        assert_eq!(
            pm_wasmmod_registry_bench_register_wasm(
                WASM_BENCH_FQN.as_ptr(),
                48,
                b"guest_add\0".as_ptr(),
                9,
                b"guest_bench_add\0".as_ptr(),
                15,
            ),
            1
        );

        // No clock installed yet → "no clock", not a number.
        let rc = pm_wasmmod_registry_bench_run(
            WASM_BENCH_FQN.as_ptr(),
            48,
            b"guest_add\0".as_ptr(),
            9,
            10,
        );
        assert_eq!(rc, -1);

        pm_wasmmod_registry_set_bench_clock(Some(fake_clock_us));
        // No bench-runner installed → guest bench FAILED (-3), not a fake
        // number and not a silent pass.
        stub_reset();
        let rc = pm_wasmmod_registry_bench_run(
            WASM_BENCH_FQN.as_ptr(),
            48,
            b"guest_add\0".as_ptr(),
            9,
            16,
        );
        assert_eq!(rc, -3);
        assert_eq!((*STUB.0.get()).calls, 0);

        // Install the runner → the export is invoked (warmup=2 + measure=16
        // calls), and we get the same ns/op a native bench would: the fake
        // clock grows 1024 us across the two grabs → 64000 ns/op @ 16.
        pm_wasmmod_registry_set_wasm_bench_runner(Some(stub_bench_runner));
        stub_reset();
        let rc = pm_wasmmod_registry_bench_run(
            WASM_BENCH_FQN.as_ptr(),
            48,
            b"guest_add\0".as_ptr(),
            9,
            16,
        );
        assert_eq!(rc, 64000);
        let s = &*STUB.0.get();
        assert_eq!(s.calls, 2, "warmup + measured lap each drive the export");
        assert!(span_eq(
            b"guest_bench_add\0".as_ptr(),
            15,
            s.export.as_ptr(),
            s.export_len,
        ));
        assert_eq!(s.iterations, 16, "the measured lap's iteration count");

        // Tear down: drop the runner and clock so later cases are pristine.
        pm_wasmmod_registry_set_wasm_bench_runner(None);
        pm_wasmmod_registry_set_bench_clock(None);
    }
    0
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

/* Host-test wrappers: each case is a plain fn + one registered
 * unsafe extern "C" trampoline (no closures — the subset has none).
 * Every trampoline goes through `case_rc`: TEST_LOCK serializes against
 * cargo's parallel #[test] threads and the registry harness, and the
 * catch_unwind keeps a failed assertion from aborting the binary
 * (an extern "C" fn cannot unwind). */

unsafe extern "C" fn case_version_set_and_query_roundtrip() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_version_set_and_query_roundtrip_body) }
}
unsafe extern "C" fn case_version_set_and_query_roundtrip_body() -> i32 {
    version_set_and_query_roundtrip();
    0
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
    unsafe { crate::util::mod_test::case_rc(case_export_sig_and_enumerate_roundtrip_body) }
}
unsafe extern "C" fn case_export_sig_and_enumerate_roundtrip_body() -> i32 {
    export_sig_and_enumerate_roundtrip();
    0
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
    unsafe { crate::util::mod_test::case_rc(case_mod_export_ensures_resident_body) }
}
unsafe extern "C" fn case_mod_export_ensures_resident_body() -> i32 {
    mod_export_ensures_resident();
    0
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
    unsafe { crate::util::mod_test::case_rc(case_publish_then_resolve_roundtrips_body) }
}
unsafe extern "C" fn case_publish_then_resolve_roundtrips_body() -> i32 {
    publish_then_resolve_roundtrips();
    0
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "publish_then_resolve_roundtrips", case_publish_then_resolve_roundtrips);
#[test]
fn test_publish_then_resolve_roundtrips() {
    assert_eq!(unsafe { case_publish_then_resolve_roundtrips() }, 0);
}

unsafe extern "C" fn case_unpublish_invalidates_the_old_handle_even_after_slot_reuse() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_unpublish_invalidates_the_old_handle_even_after_slot_reuse_body) }
}
unsafe extern "C" fn case_unpublish_invalidates_the_old_handle_even_after_slot_reuse_body() -> i32 {
    unpublish_invalidates_the_old_handle_even_after_slot_reuse();
    0
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "unpublish_invalidates_the_old_handle_even_after_slot_reuse", case_unpublish_invalidates_the_old_handle_even_after_slot_reuse);
#[test]
fn test_unpublish_invalidates_the_old_handle_even_after_slot_reuse() {
    assert_eq!(unsafe { case_unpublish_invalidates_the_old_handle_even_after_slot_reuse() }, 0);
}

unsafe extern "C" fn case_resolve_native_is_null_for_unknown_module_or_export() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_resolve_native_is_null_for_unknown_module_or_export_body) }
}
unsafe extern "C" fn case_resolve_native_is_null_for_unknown_module_or_export_body() -> i32 {
    resolve_native_is_null_for_unknown_module_or_export();
    0
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "resolve_native_is_null_for_unknown_module_or_export", case_resolve_native_is_null_for_unknown_module_or_export);
#[test]
fn test_resolve_native_is_null_for_unknown_module_or_export() {
    assert_eq!(unsafe { case_resolve_native_is_null_for_unknown_module_or_export() }, 0);
}

unsafe extern "C" fn case_call_roundtrips_through_the_fn_t_convention() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_call_roundtrips_through_the_fn_t_convention_body) }
}
unsafe extern "C" fn case_call_roundtrips_through_the_fn_t_convention_body() -> i32 {
    call_roundtrips_through_the_fn_t_convention();
    0
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "call_roundtrips_through_the_fn_t_convention", case_call_roundtrips_through_the_fn_t_convention);
#[test]
fn test_call_roundtrips_through_the_fn_t_convention() {
    assert_eq!(unsafe { case_call_roundtrips_through_the_fn_t_convention() }, 0);
}

unsafe extern "C" fn case_call_is_negative_one_for_unknown_module_or_export() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_call_is_negative_one_for_unknown_module_or_export_body) }
}
unsafe extern "C" fn case_call_is_negative_one_for_unknown_module_or_export_body() -> i32 {
    call_is_negative_one_for_unknown_module_or_export();
    0
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "call_is_negative_one_for_unknown_module_or_export", case_call_is_negative_one_for_unknown_module_or_export);
#[test]
fn test_call_is_negative_one_for_unknown_module_or_export() {
    assert_eq!(unsafe { case_call_is_negative_one_for_unknown_module_or_export() }, 0);
}

unsafe extern "C" fn case_gc_visit_only_sees_live_obj_exports() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_gc_visit_only_sees_live_obj_exports_body) }
}
unsafe extern "C" fn case_gc_visit_only_sees_live_obj_exports_body() -> i32 {
    gc_visit_only_sees_live_obj_exports();
    0
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "gc_visit_only_sees_live_obj_exports", case_gc_visit_only_sees_live_obj_exports);
#[test]
fn test_gc_visit_only_sees_live_obj_exports() {
    assert_eq!(unsafe { case_gc_visit_only_sees_live_obj_exports() }, 0);
}

/* ---- loud-refusal ring --------------------------------------------------
 * The dark-export audit: a face whose sig exceeds SIG_CAP used to vanish
 * silently (mod_export returned false, the ctor discarded it). Now every
 * refused registration lands in the introspectable ring, and the manifest
 * test (below) fails while the ring is non-empty. */

fn refusal_ring_records_a_sig_over_cap() {
    unsafe {
        /* This case is entered twice per cargo run — once through its
         * PM_MOD_TEST_RS registration (registry_mod_tests_all) and once
         * through its #[test] wrapper — on parallel threads. A
         * ring-count delta between before/after would be racy (the
         * second entry's refusal dedups into the first's row), so the
         * culprit row is found by scanning, not by counting deltas.
         * No reset either: the ring is empty at test start (SIG_CAP
         * holds every real registration), and a reset from one thread
         * could wipe the other's evidence mid-scan. */
        /* 300-byte sig — over any cap the tree could plausibly ship */
        let long_sig = [b'x'; 300];
        assert_eq!(
            pm_wasmmod_registry_mod_export(
                b"test.refusal\0".as_ptr(),
                12,
                b"over_cap\0".as_ptr(),
                8,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                long_sig.as_ptr(),
                300,
            ),
            0
        );
        let n = pm_wasmmod_registry_refusal_count();
        assert!(n >= 1);
        /* scan the ring for the culprit row (fqn + name + reason) */
        let mut fqn = [0u8; 64];
        let mut fqn_len = 64u32;
        let mut name = [0u8; 96];
        let mut name_len = 96u32;
        let mut reason = 0u8;
        let mut count = 0u32;
        let mut found = false;
        let mut i = 0u32;
        while i < n {
            if pm_wasmmod_registry_refusal_at(
                i,
                fqn.as_mut_ptr(),
                &mut fqn_len,
                name.as_mut_ptr(),
                &mut name_len,
                &mut reason,
                &mut count,
            ) == 1
                && span_is(b"test.refusal\0".as_ptr(), fqn.as_ptr(), fqn_len)
                && span_is(b"over_cap\0".as_ptr(), name.as_ptr(), name_len)
                && reason == 1
            {
                found = true;
                break;
            }
            i += 1;
        }
        assert!(found);
        assert!(count >= 1);
        /* dedup: the same refusal again bumps count, not the ring */
        assert_eq!(
            pm_wasmmod_registry_mod_export(
                b"test.refusal\0".as_ptr(),
                12,
                b"over_cap\0".as_ptr(),
                8,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                long_sig.as_ptr(),
                300,
            ),
            0
        );
        let n2 = pm_wasmmod_registry_refusal_count();
        assert_eq!(n2, n);
    }
}

fn long_valid_sig_registers_within_cap() {
    unsafe {
        /* 173 bytes — the exact stringified length of the tree's longest
         * face (pm_metal_jit_c_object_compile_target) that was dark at
         * SIG_CAP=160. At 256 it must register, not refuse. */
        let sig = [b'y'; 173];
        assert_eq!(
            pm_wasmmod_registry_mod_export(
                b"test.refusal\0".as_ptr(),
                12,
                b"long_valid\0".as_ptr(),
                10,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                sig.as_ptr(),
                173,
            ),
            1
        );
        let mut sig_out = [0u8; 256];
        let mut sig_out_len = 256u32;
        let mut name = [0u8; 32];
        let mut name_len = 32u32;
        let mut kind = pm_wasmmod_registry_export_kind_t::Obj;
        assert_eq!(
            pm_wasmmod_registry_export_at(
                b"test.refusal\0".as_ptr(),
                12,
                0,
                name.as_mut_ptr(),
                &mut name_len,
                &mut kind,
                sig_out.as_mut_ptr(),
                &mut sig_out_len,
            ),
            1
        );
        assert!(span_is(b"long_valid\0".as_ptr(), name.as_ptr(), name_len));
        assert_eq!(sig_out_len as usize, 173);
    }
}

unsafe extern "C" fn case_refusal_ring_is_loud_and_introspectable() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_refusal_ring_is_loud_and_introspectable_body) }
}
unsafe extern "C" fn case_refusal_ring_is_loud_and_introspectable_body() -> i32 {
    unsafe {
        refusal_ring_records_a_sig_over_cap();
        long_valid_sig_registers_within_cap();
    }
    0
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "refusal_ring_is_loud_and_introspectable",
    case_refusal_ring_is_loud_and_introspectable
);
#[test]
fn test_refusal_ring_is_loud_and_introspectable() {
    assert_eq!(unsafe { case_refusal_ring_is_loud_and_introspectable() }, 0);
}
