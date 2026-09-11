//! pymergetic.wasmmod.loader — module tests (`__tests__.rs`).
//!
//! Type discipline: every registry shape here comes from the loader's
//! own spliced `registry_types` face (the same bytes the in-kernel rsx
//! compile splices), and the registry entry points are re-declared as
//! a test-local extern block typed against those same shapes — one
//! instance of each type in this module, no cross-module type
//! identity to drift. (The `case_rc` trampolines keep the subset's
//! no-closure rule; panics stay contained by `catch_unwind`.)

use super::registry_types::{
    pm_wasmmod_registry_handle_t, pm_wasmmod_registry_valkind_t,
    pm_wasmmod_registry_value_of_t, pm_wasmmod_registry_value_t,
};
use super::{
    wasm_runtime_get_file_package_type, WASM_MODULE_AOT, WASM_MODULE_BYTECODE,
    pm_wasmmod_loader_init, pm_wasmmod_loader_load, pm_wasmmod_loader_unload,
    FQN_CAP, IMAGE_DEFAULT, IMAGE_KNOB,
};
use crate::util::mod_test::case;

/* Registry entry points, typed against the loader's spliced shapes
 * (same repr(C) ABI as the registry's own — the linker binds the one
 * symbol either way). */
unsafe extern "C" {
    fn pm_wasmmod_registry_call(
        fqn_ptr: *const u8,
        fqn_len: u32,
        export_ptr: *const u8,
        export_len: u32,
        args: *const pm_wasmmod_registry_value_t,
        nargs: u32,
        results: *mut pm_wasmmod_registry_value_t,
        nresults: u32,
    ) -> i32;
    fn pm_wasmmod_registry_has(fqn_ptr: *const u8, fqn_len: u32) -> i32;
    fn pm_wasmmod_registry_test_count(fqn_ptr: *const u8, fqn_len: u32) -> u32;
    fn pm_wasmmod_registry_test_run_all(fqn_ptr: *const u8, fqn_len: u32) -> i32;
    fn pm_wasmmod_registry_resolve_native(
        fqn_ptr: *const u8,
        fqn_len: u32,
        export_ptr: *const u8,
        export_len: u32,
    ) -> *mut core::ffi::c_void;
}

// Hand-assembled minimal WASM binary — no wasm toolchain dependency
// for this milestone's proof, just the module byte-for-byte per the
// core spec's binary format. Equivalent .wat:
//
//   (module
//     (func (export "answer") (result i32) i32.const 42)
//     (func (export "add_one") (param i32) (result i32)
//       local.get 0 i32.const 1 i32.add))
#[rustfmt::skip]
const ANSWER_ADD_ONE_WASM: &[u8] = &[
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, // magic, version
    // type section: type0 () -> i32, type1 (i32) -> i32
    0x01, 0x0A, 0x02, 0x60, 0x00, 0x01, 0x7F, 0x60, 0x01, 0x7F, 0x01, 0x7F,
    // function section: func0 uses type0, func1 uses type1
    0x03, 0x03, 0x02, 0x00, 0x01,
    // memory section: one zero-page memory (unexported) — WAMR's
    // wasm_runtime_attach_shared_heap requires a default memory
    // instance to exist at all, even an empty one, to check for
    // overlap against
    0x05, 0x03, 0x01, 0x00, 0x00,
    // export section: "answer" -> func0, "add_one" -> func1
    0x07, 0x14, 0x02,
    0x06, 0x61, 0x6E, 0x73, 0x77, 0x65, 0x72, 0x00, 0x00, // "answer", kind=func, index=0
    0x07, 0x61, 0x64, 0x64, 0x5F, 0x6F, 0x6E, 0x65, 0x00, 0x01, // "add_one", kind=func, index=1
    // code section: func0 { i32.const 42 }, func1 { local.get 0; i32.const 1; i32.add }
    0x0A, 0x0E, 0x02,
    0x04, 0x00, 0x41, 0x2A, 0x0B,
    0x07, 0x00, 0x20, 0x00, 0x41, 0x01, 0x6A, 0x0B,
];

fn fixture_bytes() -> Vec<u8> {
    ANSWER_ADD_ONE_WASM.to_vec()
}

fn init_once() {
    assert_eq!(unsafe { pm_wasmmod_loader_init() }, 0);
    assert_eq!(unsafe { pm_wasmmod_loader_init() }, 0); // idempotent
}

/// Compiles `wasm_bytes` to a real `.aot` file via the `wamrc`
/// built by `build.rs` (against the system LLVM — see build.rs's
/// header comment) and returns the resulting `.aot` bytes. Panics
/// with `wamrc`'s own stderr on failure — a real compiler failure
/// here is a genuine regression to surface, not something to
/// paper over with a skip.
fn compile_to_aot(wasm_bytes: &[u8]) -> Vec<u8> {
    use core::sync::atomic::{AtomicU64, Ordering};
    static SEQ: AtomicU64 = AtomicU64::new(0);
    let tag = SEQ.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let wasm_path =
        std::env::temp_dir().join(alloc::format!("pm_wasmmod_loader_test_{pid}_{tag}.wasm"));
    let aot_path =
        std::env::temp_dir().join(alloc::format!("pm_wasmmod_loader_test_{pid}_{tag}.aot"));
    std::fs::write(&wasm_path, wasm_bytes).expect("write temp .wasm fixture");

    let output = std::process::Command::new(env!("WAMRC_PATH"))
        .arg("-o")
        .arg(&aot_path)
        .arg(&wasm_path)
        .output()
        .expect("spawn wamrc (built unconditionally by build.rs)");
    assert!(
        output.status.success(),
        "wamrc failed to compile the fixture: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let aot_bytes = std::fs::read(&aot_path).unwrap_or_else(|e| {
        panic!(
            "read wamrc's .aot output at {}: {e}; stderr={}",
            aot_path.display(),
            String::from_utf8_lossy(&output.stderr)
        )
    });

    let _ = std::fs::remove_file(&wasm_path);
    let _ = std::fs::remove_file(&aot_path);
    aot_bytes
}

fn value_i32(v: i32) -> pm_wasmmod_registry_value_t {
    pm_wasmmod_registry_value_t {
        kind: pm_wasmmod_registry_valkind_t::I32,
        of: pm_wasmmod_registry_value_of_t { i32: v },
    }
}

/// Span helper: NUL-terminated test name -> (ptr, len) pair.
fn zspan(s: &'static str) -> (*const u8, u32) {
    (s.as_ptr(), s.len() as u32)
}

fn load_call_unload_roundtrips_end_to_end() {
    init_once();
    let bytes = fixture_bytes();
    let (fqn_p, fqn_len) = zspan("test.loader.e2e");
    let handle = unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, bytes.as_ptr(), bytes.len() as u32) };
    assert_ne!(handle.index, u32::MAX, "load should succeed on a well-formed fixture");

    // no-arg / one-result export
    let mut result = value_i32(0);
    let (answer_p, answer_len) = zspan("answer");
    let status = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            answer_p,
            answer_len,
            core::ptr::null(),
            0,
            &mut result,
            1,
        )
    };
    assert_eq!(status, 0);
    assert_eq!(unsafe { result.of.i32 }, 42);

    // numeric-arg / numeric-result export
    let arg = value_i32(41);
    let mut result2 = value_i32(0);
    let (add_one_p, add_one_len) = zspan("add_one");
    let status2 = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            add_one_p,
            add_one_len,
            &arg,
            1,
            &mut result2,
            1,
        )
    };
    assert_eq!(status2, 0);
    assert_eq!(unsafe { result2.of.i32 }, 42);

    assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, 0);

    // Stale handle behavior: unpublished from the registry, a
    // second unload of the same handle is rejected, not a
    // double-free.
    assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, -1);
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);
    let status_after_unload = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            answer_p,
            answer_len,
            core::ptr::null(),
            0,
            &mut result,
            1,
        )
    };
    assert_eq!(status_after_unload, -1);
}

/// Packs `examples/hello` for real via the new `*.pmm.toml`-based packer
/// (`dev/tools/src/pymergetic/wasmmod/tools/pack.py` + `pmm.py` +
/// `faces.py`) and returns the resulting `.wasm` bytes. Panics with the
/// packer's own stderr on failure — same "surface real regressions, no
/// silent skip" stance as `compile_to_aot`.
fn pack_hello_example() -> Vec<u8> {
    let manifest_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
    let tools_src = manifest_dir.join("dev/tools/src");
    let card_dir = manifest_dir.join("examples/hello/src/pymergetic/wasmmod_examples/hello");

    let pid = std::process::id();
    let out_path = std::env::temp_dir().join(alloc::format!("pm_wasmmod_pmm_pack_test_{pid}.wasm"));

    let output = std::process::Command::new("python3")
        .arg("-m")
        .arg("pymergetic.wasmmod.tools")
        .arg("pack")
        .arg(&card_dir)
        .arg("-o")
        .arg(&out_path)
        .env("PYTHONPATH", &tools_src)
        .output()
        .expect("spawn python3 -m pymergetic.wasmmod.tools (dev/tools packer)");
    assert!(
        output.status.success(),
        "pmm packer failed to build examples/hello: {}",
        String::from_utf8_lossy(&output.stderr)
    );

    let wasm_bytes = std::fs::read(&out_path).expect("read packer's .wasm output");
    let _ = std::fs::remove_file(&out_path);
    wasm_bytes
}

fn load_call_unload_roundtrips_through_real_pmm_pack() {
    init_once();
    let wasm_bytes = pack_hello_example();
    assert!(
        wasm_bytes.len() <= IMAGE_DEFAULT as usize,
        "pack must fit what the image knob takes by default"
    );
    assert_eq!(
        unsafe { wasm_runtime_get_file_package_type(wasm_bytes.as_ptr(), wasm_bytes.len() as u32) },
        WASM_MODULE_BYTECODE
    );

    let fqn = "test.loader.pmm_pack_e2e";
    let (fqn_p, fqn_len) = zspan("test.loader.pmm_pack_e2e");
    let handle = unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, wasm_bytes.as_ptr(), wasm_bytes.len() as u32) };
    assert_ne!(handle.index, u32::MAX, "load should succeed on a real pmm-packed .wasm");

    // hello() -> 42
    let mut result = value_i32(0);
    let (hello_p, hello_len) = zspan("hello");
    let status = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            hello_p,
            hello_len,
            core::ptr::null(),
            0,
            &mut result,
            1,
        )
    };
    assert_eq!(status, 0);
    assert_eq!(unsafe { result.of.i32 }, 42);

    // add(41, 1) -> 42
    let args = [value_i32(41), value_i32(1)];
    let mut result2 = value_i32(0);
    let (add_p, add_len) = zspan("add");
    let status2 = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            add_p,
            add_len,
            args.as_ptr(),
            args.len() as u32,
            &mut result2,
            1,
        )
    };
    assert_eq!(status2, 0);
    assert_eq!(unsafe { result2.of.i32 }, 42);

    // Packed __tests__.c cases land in ModEntry.tests (not product exports).
    assert_eq!(unsafe { pm_wasmmod_registry_test_count(fqn_p, fqn_len) }, 2);
    assert_eq!(unsafe { pm_wasmmod_registry_test_run_all(fqn_p, fqn_len) }, 0);
    // Test symbols must not be product faces.
    let (tname_p, tname_len) = zspan("test_hello_returns_42");
    assert!(
        unsafe { pm_wasmmod_registry_resolve_native(fqn_p, fqn_len, tname_p, tname_len) }.is_null()
    );

    assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, 0);
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);
    let _ = fqn;
}

fn load_call_unload_roundtrips_through_real_aot() {
    init_once();
    let aot_bytes = compile_to_aot(&fixture_bytes());
    // Sanity-check the fixture actually *is* AOT before trusting
    // the rest of this test — if wamrc's own magic number ever
    // changes, better to fail loudly here than have the loader's
    // detection silently degrade to treating it as Wasm.
    assert!(
        aot_bytes.len() <= IMAGE_DEFAULT as usize,
        "aot fixture must fit what the image knob takes by default"
    );
    assert_eq!(
        unsafe { wasm_runtime_get_file_package_type(aot_bytes.as_ptr(), aot_bytes.len() as u32) },
        WASM_MODULE_AOT
    );

    let (fqn_p, fqn_len) = zspan("test.loader.aot_e2e");
    let handle = unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, aot_bytes.as_ptr(), aot_bytes.len() as u32) };
    assert_ne!(handle.index, u32::MAX, "load should succeed on a real .aot file");

    let mut result = value_i32(0);
    let (answer_p, answer_len) = zspan("answer");
    let status = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            answer_p,
            answer_len,
            core::ptr::null(),
            0,
            &mut result,
            1,
        )
    };
    assert_eq!(status, 0);
    assert_eq!(unsafe { result.of.i32 }, 42);

    let arg = value_i32(41);
    let mut result2 = value_i32(0);
    let (add_one_p, add_one_len) = zspan("add_one");
    let status2 = unsafe {
        pm_wasmmod_registry_call(
            fqn_p,
            fqn_len,
            add_one_p,
            add_one_len,
            &arg,
            1,
            &mut result2,
            1,
        )
    };
    assert_eq!(status2, 0);
    assert_eq!(unsafe { result2.of.i32 }, 42);

    assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, 0);
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);
}

fn load_of_garbage_bytes_fails_cleanly_without_touching_the_registry() {
    init_once();
    let bytes = [0u8, 1, 2, 3];
    let (fqn_p, fqn_len) = zspan("test.loader.garbage");
    let handle = unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, bytes.as_ptr(), bytes.len() as u32) };
    assert_eq!(handle.index, u32::MAX);
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);
}

/// A binary past what this seat will take is refused loudly (never
/// truncated): the INVALID handle and no registry trace.
fn oversized_binary_is_refused_not_truncated() {
    init_once();
    let big: Vec<u8> = vec![0u8; IMAGE_DEFAULT as usize + 1];
    let (fqn_p, fqn_len) = zspan("test.loader.oversized");
    let handle = unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, big.as_ptr(), big.len() as u32) };
    assert_eq!(handle.index, u32::MAX);
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);
}

/// What a seat will take is the knob, not the shape of a row: with it moved
/// under a real pack the load refuses, and the same pack goes in once the
/// knob is back. (The image is an allocation of the pack's own length now, so
/// the knob is the only thing capping it.)
fn the_image_knob_moves_what_a_load_will_take() {
    init_once();
    let wasm_bytes = pack_hello_example();
    let (fqn_p, fqn_len) = zspan("test.loader.image_knob");

    unsafe { (*IMAGE_KNOB.0.get()).soft = 1024 };
    let refused =
        unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, wasm_bytes.as_ptr(), wasm_bytes.len() as u32) };
    assert_eq!(refused.index, u32::MAX, "a pack over the knob must refuse");
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);

    unsafe { (*IMAGE_KNOB.0.get()).soft = IMAGE_DEFAULT };
    let handle =
        unsafe { pm_wasmmod_loader_load(fqn_p, fqn_len, wasm_bytes.as_ptr(), wasm_bytes.len() as u32) };
    assert_ne!(handle.index, u32::MAX, "the same pack loads once the knob is back");
    assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, 0);
    assert_eq!(unsafe { pm_wasmmod_registry_has(fqn_p, fqn_len) }, 0);
}

/// An fqn longer than the fixed row is likewise refused, not clipped
/// into a key that could later collide with a shorter prefix.
fn oversized_fqn_is_refused_not_clipped() {
    init_once();
    let bytes = fixture_bytes();
    let long = "test.loader.".to_string() + &"x".repeat(FQN_CAP);
    let handle = unsafe { pm_wasmmod_loader_load(long.as_ptr(), long.len() as u32, bytes.as_ptr(), bytes.len() as u32) };
    assert_eq!(handle.index, u32::MAX);
}

unsafe extern "C" fn case_load_call_unload_roundtrips_end_to_end() -> i32 {
    case(|| load_call_unload_roundtrips_end_to_end())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "load_call_unload_roundtrips_end_to_end", case_load_call_unload_roundtrips_end_to_end);
#[test]
fn test_load_call_unload_roundtrips_end_to_end() {
    assert_eq!(unsafe { case_load_call_unload_roundtrips_end_to_end() }, 0);
}

unsafe extern "C" fn case_load_call_unload_roundtrips_through_real_pmm_pack() -> i32 {
    case(|| load_call_unload_roundtrips_through_real_pmm_pack())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "load_call_unload_roundtrips_through_real_pmm_pack", case_load_call_unload_roundtrips_through_real_pmm_pack);
#[test]
fn test_load_call_unload_roundtrips_through_real_pmm_pack() {
    assert_eq!(unsafe { case_load_call_unload_roundtrips_through_real_pmm_pack() }, 0);
}

unsafe extern "C" fn case_load_call_unload_roundtrips_through_real_aot() -> i32 {
    case(|| load_call_unload_roundtrips_through_real_aot())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "load_call_unload_roundtrips_through_real_aot", case_load_call_unload_roundtrips_through_real_aot);
#[test]
fn test_load_call_unload_roundtrips_through_real_aot() {
    assert_eq!(unsafe { case_load_call_unload_roundtrips_through_real_aot() }, 0);
}

unsafe extern "C" fn case_load_of_garbage_bytes_fails_cleanly_without_touching_the_registry() -> i32 {
    case(|| load_of_garbage_bytes_fails_cleanly_without_touching_the_registry())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "load_of_garbage_bytes_fails_cleanly_without_touching_the_registry", case_load_of_garbage_bytes_fails_cleanly_without_touching_the_registry);
#[test]
fn test_load_of_garbage_bytes_fails_cleanly_without_touching_the_registry() {
    assert_eq!(unsafe { case_load_of_garbage_bytes_fails_cleanly_without_touching_the_registry() }, 0);
}

unsafe extern "C" fn case_oversized_binary_is_refused_not_truncated() -> i32 {
    case(|| oversized_binary_is_refused_not_truncated())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "oversized_binary_is_refused_not_truncated", case_oversized_binary_is_refused_not_truncated);
#[test]
fn test_oversized_binary_is_refused_not_truncated() {
    assert_eq!(unsafe { case_oversized_binary_is_refused_not_truncated() }, 0);
}

unsafe extern "C" fn case_the_image_knob_moves_what_a_load_will_take() -> i32 {
    case(|| the_image_knob_moves_what_a_load_will_take())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "the_image_knob_moves_what_a_load_will_take", case_the_image_knob_moves_what_a_load_will_take);
#[test]
fn test_the_image_knob_moves_what_a_load_will_take() {
    assert_eq!(unsafe { case_the_image_knob_moves_what_a_load_will_take() }, 0);
}

unsafe extern "C" fn case_oversized_fqn_is_refused_not_clipped() -> i32 {
    case(|| oversized_fqn_is_refused_not_clipped())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.loader", "oversized_fqn_is_refused_not_clipped", case_oversized_fqn_is_refused_not_clipped);
#[test]
fn test_oversized_fqn_is_refused_not_clipped() {
    assert_eq!(unsafe { case_oversized_fqn_is_refused_not_clipped() }, 0);
}
