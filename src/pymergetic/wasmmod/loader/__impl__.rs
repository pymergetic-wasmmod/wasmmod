//! pymergetic.wasmmod.loader — turns `.wasm` bytes into directly-callable
//! `pymergetic.wasmmod.registry` entries: load -> instantiate -> attach
//! the shared heap -> enumerate exports -> claim one trampoline adapter
//! per export -> `registry.export_set`. This is the one place in the
//! tree that knows WAMR exists; the registry itself never does (see
//! docs/REGISTRY.md "Value convention" and this session's design notes
//! in docs/SOURCETREE.md's decision log).
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::{CStr, c_char, c_void};

use crate::util::lock::Mutex;
use crate::wasmmod::registry::{
    pm_wasmmod_registry_container_kind_t, pm_wasmmod_registry_export_kind_t,
    pm_wasmmod_registry_fn_t, pm_wasmmod_registry_handle_t, pm_wasmmod_registry_set_version,
    pm_wasmmod_registry_set_wasm_test_runner, pm_wasmmod_registry_test_register_wasm,
    pm_wasmmod_registry_valkind_t, pm_wasmmod_registry_value_t,
};

// ---------------------------------------------------------------------
// WAMR — hand-written externs for the one subset of
// third_party/wamr/core/iwasm/include/wasm_export.h this loader needs.
// Not a bindgen mirror of *our own* module (that convention is for
// pymergetic-owned faces, see mem/__exports__.rs) — WAMR is vendored,
// so this is the loader's own private FFI boundary, same posture as
// any other hand-written extern block onto a wrapped third-party C lib.
// ---------------------------------------------------------------------
mod wamr {
    use core::ffi::{c_char, c_void};

    #[repr(C)]
    pub struct wasm_module_marker {
        _opaque: [u8; 0],
    }
    pub type wasm_module_t = *mut wasm_module_marker;

    #[repr(C)]
    pub struct wasm_module_inst_marker {
        _opaque: [u8; 0],
    }
    pub type wasm_module_inst_t = *mut wasm_module_inst_marker;

    #[repr(C)]
    pub struct wasm_function_inst_marker {
        _opaque: [u8; 0],
    }
    pub type wasm_function_inst_t = *mut wasm_function_inst_marker;

    #[repr(C)]
    pub struct wasm_exec_env_marker {
        _opaque: [u8; 0],
    }
    pub type wasm_exec_env_t = *mut wasm_exec_env_marker;

    #[repr(C)]
    pub struct wasm_shared_heap_marker {
        _opaque: [u8; 0],
    }
    pub type wasm_shared_heap_t = *mut wasm_shared_heap_marker;

    /// Mirrors `wasm_val_t` (see wasm_export.h): `kind` + a union of the
    /// four core value types. `#[repr(C)]` gives this the identical
    /// layout gcc/clang give the real struct (`u8` then padding to the
    /// union's 8-byte alignment) — no manual padding field needed.
    #[repr(C)]
    #[derive(Clone, Copy)]
    pub struct wasm_val_t {
        pub kind: u8,
        pub of: wasm_val_of_t,
    }

    #[repr(C)]
    #[derive(Clone, Copy)]
    pub union wasm_val_of_t {
        pub i32: i32,
        pub i64: i64,
        pub f32: f32,
        pub f64: f64,
    }

    pub const WASM_I32: u8 = 0;
    pub const WASM_I64: u8 = 1;
    pub const WASM_F32: u8 = 2;
    pub const WASM_F64: u8 = 3;

    /// Mirrors `package_type_t` (wasm_export.h) — what
    /// `wasm_runtime_get_file_package_type` returns, used to tell a
    /// real `.aot` file apart from a plain `.wasm` bytecode module
    /// *before* `wasm_runtime_load`, so the loader can publish the
    /// correct `pm_wasmmod_registry_container_kind_t` instead of
    /// hardcoding one.
    #[allow(dead_code)] // documents the other half of the match below; every non-AOT value (this one included) takes that match's `_` arm
    pub const WASM_MODULE_BYTECODE: u32 = 0;
    pub const WASM_MODULE_AOT: u32 = 1;

    /// Mirrors `wasm_export_t`'s first two fields (`name`, `kind`) plus
    /// enough trailing space for its `union { ...} u` (four
    /// pointer-sized members in the real struct) — this loader never
    /// reads that union, it only re-derives param/result shapes via
    /// `wasm_func_get_param/result_*` on the looked-up function
    /// instance, so one pointer-sized filler field is enough.
    #[repr(C)]
    pub struct wasm_export_t {
        pub name: *const c_char,
        pub kind: u32,
        pub reserved: *mut c_void,
    }

    pub const WASM_IMPORT_EXPORT_KIND_FUNC: u32 = 0;

    /// Mirrors `SharedHeapInitArgs`.
    #[repr(C)]
    pub struct SharedHeapInitArgs {
        pub size: u32,
        pub pre_allocated_addr: *mut c_void,
    }

    unsafe extern "C" {
        pub fn wasm_runtime_init() -> bool;
        pub fn wasm_runtime_init_thread_env() -> bool;

        pub fn wasm_runtime_load(
            buf: *mut u8,
            size: u32,
            error_buf: *mut c_char,
            error_buf_size: u32,
        ) -> wasm_module_t;
        pub fn wasm_runtime_unload(module: wasm_module_t);

        pub fn wasm_runtime_instantiate(
            module: wasm_module_t,
            default_stack_size: u32,
            host_managed_heap_size: u32,
            error_buf: *mut c_char,
            error_buf_size: u32,
        ) -> wasm_module_inst_t;
        pub fn wasm_runtime_deinstantiate(module_inst: wasm_module_inst_t);

        pub fn wasm_runtime_get_export_count(module: wasm_module_t) -> i32;
        pub fn wasm_runtime_get_export_type(
            module: wasm_module_t,
            export_index: i32,
            export_type: *mut wasm_export_t,
        );

        pub fn wasm_runtime_lookup_function(
            module_inst: wasm_module_inst_t,
            name: *const c_char,
        ) -> wasm_function_inst_t;

        pub fn wasm_runtime_create_exec_env(
            module_inst: wasm_module_inst_t,
            stack_size: u32,
        ) -> wasm_exec_env_t;
        pub fn wasm_runtime_destroy_exec_env(exec_env: wasm_exec_env_t);

        pub fn wasm_runtime_call_wasm_a(
            exec_env: wasm_exec_env_t,
            function: wasm_function_inst_t,
            num_results: u32,
            results: *mut wasm_val_t,
            num_args: u32,
            args: *mut wasm_val_t,
        ) -> bool;

        pub fn wasm_runtime_create_shared_heap(
            init_args: *mut SharedHeapInitArgs,
        ) -> wasm_shared_heap_t;
        pub fn wasm_runtime_attach_shared_heap(
            module_inst: wasm_module_inst_t,
            shared_heap: wasm_shared_heap_t,
        ) -> bool;
        pub fn wasm_runtime_detach_shared_heap(module_inst: wasm_module_inst_t);

        pub fn wasm_runtime_addr_app_to_native(
            module_inst: wasm_module_inst_t,
            app_offset: u64,
        ) -> *mut c_void;
        pub fn wasm_runtime_addr_native_to_app(
            module_inst: wasm_module_inst_t,
            native_ptr: *mut c_void,
        ) -> u64;
        pub fn wasm_runtime_shared_heap_malloc(
            module_inst: wasm_module_inst_t,
            size: u64,
            p_native_addr: *mut *mut c_void,
        ) -> u64;
        pub fn wasm_runtime_shared_heap_free(module_inst: wasm_module_inst_t, ptr: u64);

        pub fn wasm_runtime_get_exception(module_inst: wasm_module_inst_t) -> *const c_char;

        pub fn wasm_runtime_get_file_package_type(buf: *const u8, size: u32) -> u32;
    }
}

/// Portable address (mirrors registry/__types__.h). C callers use the
/// header typedef; Rust tests use these mirrors.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_addr_t {
    pub space: u32,
    pub off: u64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_buf_t {
    pub ptr: pm_addr_t,
    pub len: u32,
}

pub const PM_ADDR_SPACE_NATIVE: u32 = 0;
pub const PM_ADDR_SPACE_SHARED: u32 = 1;
pub const PM_ADDR_SPACE_MODULE: u32 = 2;

// ---------------------------------------------------------------------
// Shared heap: WAMR runtime-managed (not pre_allocated).
// Pre-allocated heaps cannot `shared_heap_malloc`/`free` (WAMR walks
// the chain for a heap_handle; prealloc has none). Runtime-managed
// keeps CALLGRAPH buffer allocs working; util.mem remains available
// for other host arenas. Size must be a whole OS page multiple.
// ---------------------------------------------------------------------
const SHARED_HEAP_SIZE: u32 = 1024 * 1024; // 1 MiB

struct SharedHeapState {
    heap: wamr::wasm_shared_heap_t,
}

// SAFETY: opaque WAMR address; only touched under SHARED_HEAP's Mutex.
unsafe impl Send for SharedHeapState {}

static SHARED_HEAP: Mutex<Option<SharedHeapState>> = Mutex::new(None);

// ---------------------------------------------------------------------
// Adapter pool: a small fixed set of hand-written trampoline stubs.
// Every stub has the exact same body (`adapter_invoke`) — what makes
// them distinct is only their *address*, which is what lets N
// concurrently-loaded exports each get a real, distinguishable
// `pm_wasmmod_registry_fn_t` to publish, despite a bare C function
// pointer having nowhere to close over which wasm instance/function it
// should call. "Claim a slot" = hand out one of these N addresses and
// remember which exec_env/function it now means, until unload releases
// it back to the pool.
// ---------------------------------------------------------------------
const MAX_ADAPTER_SLOTS: usize = 8;
const MAX_VALUES: usize = 8; // args/results per call this milestone's proof needs to cover; raise if a future fixture needs more

#[derive(Clone, Copy)]
struct AdapterSlot {
    claimed: bool,
    instance: wamr::wasm_module_inst_t,
    exec_env: wamr::wasm_exec_env_t,
    func: wamr::wasm_function_inst_t,
}

impl AdapterSlot {
    const EMPTY: AdapterSlot = AdapterSlot {
        claimed: false,
        instance: core::ptr::null_mut(),
        exec_env: core::ptr::null_mut(),
        func: core::ptr::null_mut(),
    };
}

// SAFETY: same reasoning as SharedHeapState — opaque WAMR addresses,
// only ever touched under ADAPTER_SLOTS' Mutex.
unsafe impl Send for AdapterSlot {}

static ADAPTER_SLOTS: Mutex<[AdapterSlot; MAX_ADAPTER_SLOTS]> =
    Mutex::new([AdapterSlot::EMPTY; MAX_ADAPTER_SLOTS]);

fn claim_adapter_slot(
    instance: wamr::wasm_module_inst_t,
    exec_env: wamr::wasm_exec_env_t,
    func: wamr::wasm_function_inst_t,
) -> Option<usize> {
    let mut slots = ADAPTER_SLOTS.lock();
    for (index, slot) in slots.iter_mut().enumerate() {
        if !slot.claimed {
            *slot = AdapterSlot {
                claimed: true,
                instance,
                exec_env,
                func,
            };
            return Some(index);
        }
    }
    None
}

fn release_adapter_slot(index: usize) {
    ADAPTER_SLOTS.lock()[index] = AdapterSlot::EMPTY;
}

fn to_wasm_val(v: &pm_wasmmod_registry_value_t) -> wamr::wasm_val_t {
    match v.kind {
        pm_wasmmod_registry_valkind_t::I32 => wamr::wasm_val_t {
            kind: wamr::WASM_I32,
            of: wamr::wasm_val_of_t {
                i32: unsafe { v.of.i32 },
            },
        },
        pm_wasmmod_registry_valkind_t::I64 => wamr::wasm_val_t {
            kind: wamr::WASM_I64,
            of: wamr::wasm_val_of_t {
                i64: unsafe { v.of.i64 },
            },
        },
        pm_wasmmod_registry_valkind_t::F32 => wamr::wasm_val_t {
            kind: wamr::WASM_F32,
            of: wamr::wasm_val_of_t {
                f32: unsafe { v.of.f32 },
            },
        },
        pm_wasmmod_registry_valkind_t::F64 => wamr::wasm_val_t {
            kind: wamr::WASM_F64,
            of: wamr::wasm_val_of_t {
                f64: unsafe { v.of.f64 },
            },
        },
    }
}

fn from_wasm_val(v: &wamr::wasm_val_t) -> pm_wasmmod_registry_value_t {
    use crate::wasmmod::registry::pm_wasmmod_registry_value_of_t as Of;
    match v.kind {
        wamr::WASM_I64 => pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I64,
            of: Of {
                i64: unsafe { v.of.i64 },
            },
        },
        wamr::WASM_F32 => pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::F32,
            of: Of {
                f32: unsafe { v.of.f32 },
            },
        },
        wamr::WASM_F64 => pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::F64,
            of: Of {
                f64: unsafe { v.of.f64 },
            },
        },
        // WASM_I32 and anything unrecognized (never produced by our own
        // to_wasm_val, but a future signature shape might add one)
        // both fall back to i32 rather than panicking mid-call.
        _ => pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: Of {
                i32: unsafe { v.of.i32 },
            },
        },
    }
}

/// The one body every adapter stub below calls into — translate
/// `Value`s to `wasm_val_t`s, call through WAMR, translate back.
fn adapter_invoke(
    slot_index: usize,
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    if nargs as usize > MAX_VALUES || nresults as usize > MAX_VALUES {
        return -1;
    }
    let (instance, exec_env, func) = {
        let slots = ADAPTER_SLOTS.lock();
        let slot = &slots[slot_index];
        if !slot.claimed {
            return -1;
        }
        (slot.instance, slot.exec_env, slot.func)
    };

    // WAMR's hardware bound-check needs a per-*OS-thread* signal/altstack
    // setup, done automatically only for the one thread that happened to
    // call wasm_runtime_init() (see pm_wasmmod_loader_init) — any other
    // thread calling into wasm execution (the registry's whole point:
    // "callable in any direction", not just from the thread that loaded
    // it) needs this explicitly, once. Idempotent per-thread on WAMR's
    // side (a thread-local bool short-circuits a second call), so an
    // unconditional call here — on every call, not just the first per
    // thread — costs one cheap check, not a real re-init.
    let _ = unsafe { wamr::wasm_runtime_init_thread_env() };

    let mut wasm_args = [wamr::wasm_val_t {
        kind: 0,
        of: wamr::wasm_val_of_t { i64: 0 },
    }; MAX_VALUES];
    for (i, slot) in wasm_args.iter_mut().enumerate().take(nargs as usize) {
        // SAFETY: caller (pm_wasmmod_registry_call, via the registry's
        // Fn convention) contracts `args`/`nargs` to describe a valid
        // array of at least `nargs` values for the call's duration.
        let arg = unsafe { &*args.add(i) };
        *slot = to_wasm_val(arg);
    }

    let mut wasm_results = [wamr::wasm_val_t {
        kind: 0,
        of: wamr::wasm_val_of_t { i64: 0 },
    }; MAX_VALUES];
    let ok = unsafe {
        wamr::wasm_runtime_call_wasm_a(
            exec_env,
            func,
            nresults,
            wasm_results.as_mut_ptr(),
            nargs,
            wasm_args.as_mut_ptr(),
        )
    };
    if !ok {
        // The exception message itself isn't surfaced through the Fn
        // convention's plain i32 status (no error-string channel in
        // this milestone's design) — reading it here is purely so a
        // debugger stopped on this line can inspect *why*, without
        // adding a whole new export-facing error-reporting story yet.
        let _exception = wamr_exception(instance);
        return -1;
    }

    for (i, wasm_result) in wasm_results.iter().enumerate().take(nresults as usize) {
        // SAFETY: same contract as `args` above, for `results`/`nresults`.
        unsafe { *results.add(i) = from_wasm_val(wasm_result) };
    }
    0
}

macro_rules! adapter_slot_fn {
    ($name:ident, $index:expr) => {
        unsafe extern "C" fn $name(
            args: *const pm_wasmmod_registry_value_t,
            nargs: u32,
            results: *mut pm_wasmmod_registry_value_t,
            nresults: u32,
        ) -> i32 {
            adapter_invoke($index, args, nargs, results, nresults)
        }
    };
}

adapter_slot_fn!(adapter_slot_0, 0);
adapter_slot_fn!(adapter_slot_1, 1);
adapter_slot_fn!(adapter_slot_2, 2);
adapter_slot_fn!(adapter_slot_3, 3);
adapter_slot_fn!(adapter_slot_4, 4);
adapter_slot_fn!(adapter_slot_5, 5);
adapter_slot_fn!(adapter_slot_6, 6);
adapter_slot_fn!(adapter_slot_7, 7);

// One real `pm_wasmmod_registry_fn_t` address per slot — index i here
// must always be exactly adapter_slot_i, since claim_adapter_slot's
// returned index is used directly into this array.
static ADAPTER_FNS: [pm_wasmmod_registry_fn_t; MAX_ADAPTER_SLOTS] = [
    adapter_slot_0,
    adapter_slot_1,
    adapter_slot_2,
    adapter_slot_3,
    adapter_slot_4,
    adapter_slot_5,
    adapter_slot_6,
    adapter_slot_7,
];

// ---------------------------------------------------------------------
// Per-loaded-module bookkeeping, indexed 1:1 by the registry handle's
// own `index` (registry reuses a dead slot's index before growing, so
// this Vec's occupancy tracks the registry's exactly).
// ---------------------------------------------------------------------
struct LoadedModule {
    fqn: String,
    generation: u32,
    module: wamr::wasm_module_t,
    instance: wamr::wasm_module_inst_t,
    exec_env: wamr::wasm_exec_env_t,
    // WAMR requires the byte buffer passed to wasm_runtime_load to
    // remain referenceable (and possibly mutated in place) until
    // wasm_runtime_unload — this Vec is that buffer's one owner.
    #[allow(dead_code)]
    bytes: Vec<u8>,
    slots: Vec<usize>,
}

// SAFETY: same reasoning as AdapterSlot/SharedHeapState — opaque WAMR
// addresses, only ever touched under LOADED's Mutex.
unsafe impl Send for LoadedModule {}

static LOADED: Mutex<Vec<Option<LoadedModule>>> = Mutex::new(Vec::new());

fn str_from_raw<'a>(ptr: *const u8, len: u32) -> Option<&'a str> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: caller (an extern "C" fn below) contracts ptr/len to
    // describe a valid, live byte range for the duration of this call.
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
    core::str::from_utf8(bytes).ok()
}

fn wamr_exception(module_inst: wamr::wasm_module_inst_t) -> &'static str {
    // SAFETY: module_inst is a live instance for the duration of this
    // call; wasm_runtime_get_exception's returned pointer is either
    // NULL or a runtime-owned nul-terminated string valid at least
    // that long.
    let ptr = unsafe { wamr::wasm_runtime_get_exception(module_inst) };
    if ptr.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(ptr) }.to_str().unwrap_or("")
}

/// Tears down everything a partially-built load already created, in
/// reverse order — the one rollback path both a real failure midway
/// through `load()` and `unload()`'s happy path share.
fn teardown(
    module: wamr::wasm_module_t,
    instance: wamr::wasm_module_inst_t,
    exec_env: wamr::wasm_exec_env_t,
    slots: &[usize],
) {
    for &slot in slots {
        release_adapter_slot(slot);
    }
    if !exec_env.is_null() {
        unsafe { wamr::wasm_runtime_destroy_exec_env(exec_env) };
    }
    if !instance.is_null() {
        unsafe { wamr::wasm_runtime_detach_shared_heap(instance) };
        unsafe { wamr::wasm_runtime_deinstantiate(instance) };
    }
    if !module.is_null() {
        unsafe { wamr::wasm_runtime_unload(module) };
    }
}

/// Registry guest-test hook: call a `() -> i32` export on the loaded instance.
unsafe extern "C" fn loader_run_wasm_test(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_ptr: *const u8,
    export_len: u32,
) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return -1;
    };
    let Some(export) = str_from_raw(export_ptr, export_len) else {
        return -1;
    };
    let (instance, exec_env) = {
        let loaded = LOADED.lock();
        match loaded.iter().flatten().find(|m| m.fqn == fqn) {
            Some(m) => (m.instance, m.exec_env),
            None => return -1,
        }
    };
    let mut name = export.as_bytes().to_vec();
    name.push(0);
    let func =
        unsafe { wamr::wasm_runtime_lookup_function(instance, name.as_ptr() as *const c_char) };
    if func.is_null() {
        return -1;
    }
    let _ = unsafe { wamr::wasm_runtime_init_thread_env() };
    let mut wasm_results = [wamr::wasm_val_t {
        kind: 0,
        of: wamr::wasm_val_of_t { i64: 0 },
    }; 1];
    let ok = unsafe {
        wamr::wasm_runtime_call_wasm_a(
            exec_env,
            func,
            1,
            wasm_results.as_mut_ptr(),
            0,
            core::ptr::null_mut(),
        )
    };
    if !ok {
        let _exception = wamr_exception(instance);
        return -1;
    }
    unsafe { wasm_results[0].of.i32 }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_init() -> i32 {
    let mut guard = SHARED_HEAP.lock();
    if guard.is_some() {
        // Still (re)install the guest-test hook — init is idempotent but
        // the runner must be live whenever a heap exists.
        unsafe {
            pm_wasmmod_registry_set_wasm_test_runner(Some(loader_run_wasm_test));
        }
        return 0;
    }

    if !unsafe { wamr::wasm_runtime_init() } {
        return -1;
    }

    let mut init_args = wamr::SharedHeapInitArgs {
        size: SHARED_HEAP_SIZE,
        pre_allocated_addr: core::ptr::null_mut(),
    };
    let heap = unsafe { wamr::wasm_runtime_create_shared_heap(&mut init_args) };
    if heap.is_null() {
        return -1;
    }

    *guard = Some(SharedHeapState { heap });
    unsafe {
        pm_wasmmod_registry_set_wasm_test_runner(Some(loader_run_wasm_test));
    }
    0
}

const DEFAULT_STACK_SIZE: u32 = 32 * 1024;
const DEFAULT_HEAP_SIZE: u32 = 64 * 1024;

fn read_uleb(p: &mut &[u8]) -> Option<u32> {
    let mut result = 0u32;
    let mut shift = 0u32;
    while !p.is_empty() {
        let b = p[0];
        *p = &p[1..];
        result |= u32::from(b & 0x7f) << shift;
        if b & 0x80 == 0 {
            return Some(result);
        }
        shift += 7;
        if shift > 28 {
            return None;
        }
    }
    None
}

fn read_u16_le(p: &[u8]) -> Option<u16> {
    if p.len() < 2 {
        return None;
    }
    Some(u16::from(p[0]) | (u16::from(p[1]) << 8))
}

/// Find a Wasm custom section by name (id 0). Also works when the buffer is a
/// plain wasm embedded in larger artifacts that still start with `\0asm`.
fn find_wasm_custom_section<'a>(bytes: &'a [u8], name: &str) -> Option<&'a [u8]> {
    if bytes.len() < 8 || &bytes[0..4] != b"\0asm" {
        return None;
    }
    let mut p = &bytes[8..];
    while !p.is_empty() {
        let id = p[0];
        p = &p[1..];
        let size = read_uleb(&mut p)? as usize;
        if p.len() < size {
            return None;
        }
        let sec = &p[..size];
        p = &p[size..];
        if id != 0 {
            continue;
        }
        let mut q = sec;
        let nlen = read_uleb(&mut q)? as usize;
        if q.len() < nlen {
            continue;
        }
        if &q[..nlen] == name.as_bytes() {
            return Some(&q[nlen..]);
        }
    }
    None
}

fn read_u32_le(p: &[u8]) -> Option<u32> {
    if p.len() < 4 {
        return None;
    }
    Some(u32::from(p[0]) | (u32::from(p[1]) << 8) | (u32::from(p[2]) << 16) | (u32::from(p[3]) << 24))
}

/// WAMR AOT custom section (type 100) — mirrors `mp_wasm_aot_find_section`.
fn find_aot_custom_section<'a>(bytes: &'a [u8], name: &str) -> Option<&'a [u8]> {
    if bytes.len() < 8 || &bytes[0..4] != b"\0aot" {
        return None;
    }
    let want = name.as_bytes();
    let mut p = 8usize;
    while p + 8 <= bytes.len() {
        let typ = read_u32_le(&bytes[p..])?;
        let size = read_u32_le(&bytes[p + 4..])? as usize;
        let content_off = p + 8;
        if content_off + size > bytes.len() || size > 0x1000_0000 {
            return None;
        }
        let content = &bytes[content_off..content_off + size];
        if typ == 100 && size >= 6 {
            let sub = read_u32_le(content)?;
            if sub == 0 {
                let slen = read_u16_le(&content[4..])? as usize;
                if 6 + slen <= content.len() {
                    let nb = &content[6..6 + slen];
                    let bare = if !nb.is_empty() && *nb.last().unwrap() == 0 {
                        &nb[..nb.len() - 1]
                    } else {
                        nb
                    };
                    if bare == want {
                        return Some(&content[6 + slen..]);
                    }
                }
            }
        }
        p = (content_off + size + 3) & !3;
    }
    None
}

fn find_custom_section<'a>(bytes: &'a [u8], name: &str) -> Option<&'a [u8]> {
    find_wasm_custom_section(bytes, name).or_else(|| find_aot_custom_section(bytes, name))
}

fn parse_mppk(payload: &[u8]) -> Option<&str> {
    if payload.len() < 8 || &payload[0..4] != b"MPPK" {
        return None;
    }
    let fmt = read_u16_le(&payload[4..])?;
    if fmt != 1 {
        return None;
    }
    let n = read_u16_le(&payload[6..])? as usize;
    if 8 + n > payload.len() || n == 0 {
        return None;
    }
    core::str::from_utf8(&payload[8..8 + n]).ok()
}

fn parse_mpsr_pkg_version(payload: &[u8]) -> Option<&str> {
    if payload.len() < 14 || &payload[0..4] != b"MPSR" {
        return None;
    }
    let name_len = read_u16_le(&payload[8..])? as usize;
    if 10 + name_len + 2 > payload.len() {
        return None;
    }
    let p = &payload[10 + name_len..];
    let pv_len = read_u16_le(p)? as usize;
    if 2 + pv_len > p.len() || pv_len == 0 {
        return None;
    }
    core::str::from_utf8(&p[2..2 + pv_len]).ok()
}

fn extract_pkg_version(bytes: &[u8]) -> Option<&str> {
    if let Some(p) = find_custom_section(bytes, "wasmmod.pkg")
        && let Some(v) = parse_mppk(p)
    {
        return Some(v);
    }
    if let Some(p) = find_custom_section(bytes, "wasmmod.source")
        && let Some(v) = parse_mpsr_pkg_version(p)
    {
        return Some(v);
    }
    None
}

/// One entry from ``wasmmod.tests`` (MPTE).
struct PackTest {
    /// Registry case name (what `test_run` / µPy `wasmmod.test` use).
    name: String,
    /// Wasm export symbol to look up.
    export: String,
}

fn parse_mpte(payload: &[u8]) -> Option<Vec<PackTest>> {
    if payload.len() < 10 || &payload[0..4] != b"MPTE" {
        return None;
    }
    let ver = read_u16_le(&payload[4..])?;
    if ver != 1 {
        return None;
    }
    let n = read_u32_le(&payload[6..])? as usize;
    if n > 1024 {
        return None;
    }
    let mut i = 10usize;
    let mut out = Vec::with_capacity(n);
    for _ in 0..n {
        // module (advisory; load fqn wins at register time)
        let ml = read_u16_le(payload.get(i..)?)? as usize;
        i += 2;
        if i + ml > payload.len() {
            return None;
        }
        i += ml;
        let nl = read_u16_le(payload.get(i..)?)? as usize;
        i += 2;
        if i + nl > payload.len() || nl == 0 {
            return None;
        }
        let name = String::from(core::str::from_utf8(&payload[i..i + nl]).ok()?);
        i += nl;
        let el = read_u16_le(payload.get(i..)?)? as usize;
        i += 2;
        if i + el > payload.len() || el == 0 {
            return None;
        }
        let export = String::from(core::str::from_utf8(&payload[i..i + el]).ok()?);
        i += el;
        out.push(PackTest { name, export });
    }
    Some(out)
}

/// C/ELF path: bake version from artifact bytes onto an already-published fqn.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_bake_pkg_version(
    fqn_ptr: *const u8,
    fqn_len: u32,
    bytes_ptr: *const u8,
    bytes_len: u32,
) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    if bytes_ptr.is_null() || bytes_len == 0 {
        return 0;
    }
    let bytes = unsafe { core::slice::from_raw_parts(bytes_ptr, bytes_len as usize) };
    let Some(ver) = extract_pkg_version(bytes) else {
        return 0;
    };
    unsafe {
        pm_wasmmod_registry_set_version(fqn.as_ptr(), fqn.len() as u32, ver.as_ptr(), ver.len() as u32)
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_load(
    fqn_ptr: *const u8,
    fqn_len: u32,
    bytes_ptr: *const u8,
    bytes_len: u32,
) -> pm_wasmmod_registry_handle_t {
    const INVALID: pm_wasmmod_registry_handle_t = pm_wasmmod_registry_handle_t {
        index: u32::MAX,
        generation: 0,
    };

    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return INVALID;
    };
    if bytes_ptr.is_null() {
        return INVALID;
    }
    let heap = match SHARED_HEAP.lock().as_ref() {
        Some(state) => state.heap,
        // pm_wasmmod_loader_init() must run first — see the plan's
        // loader/registry split, "loader owns WAMR instantiation".
        None => return INVALID,
    };

    // SAFETY: caller contracts bytes_ptr/bytes_len to describe a valid,
    // live byte range for the duration of this call; copied out into
    // our own owned Vec immediately below, so no lifetime dependency
    // on the caller's buffer beyond this point.
    let mut bytes: Vec<u8> =
        unsafe { core::slice::from_raw_parts(bytes_ptr, bytes_len as usize) }.to_vec();

    // Real detection, not a hardcoded guess: `.aot` and `.wasm` are
    // told apart from the bytes themselves (magic-number sniff inside
    // WAMR), before `wasm_runtime_load` — the same call handles both
    // container kinds internally, so nothing else about the load path
    // below needs to branch on this; only the registry publish call
    // does. Anything WAMR doesn't recognize falls back to Wasm rather
    // than a new failure mode — wasm_runtime_load's own validation is
    // still the real gatekeeper for genuinely malformed input.
    let container = match unsafe {
        wamr::wasm_runtime_get_file_package_type(bytes.as_ptr(), bytes.len() as u32)
    } {
        wamr::WASM_MODULE_AOT => pm_wasmmod_registry_container_kind_t::Aot,
        _ => pm_wasmmod_registry_container_kind_t::Wasm,
    };

    let mut error_buf = [0u8; 256];
    let module = unsafe {
        wamr::wasm_runtime_load(
            bytes.as_mut_ptr(),
            bytes.len() as u32,
            error_buf.as_mut_ptr() as *mut c_char,
            error_buf.len() as u32,
        )
    };
    if module.is_null() {
        return INVALID;
    }

    let instance = unsafe {
        wamr::wasm_runtime_instantiate(
            module,
            DEFAULT_STACK_SIZE,
            DEFAULT_HEAP_SIZE,
            error_buf.as_mut_ptr() as *mut c_char,
            error_buf.len() as u32,
        )
    };
    if instance.is_null() {
        teardown(module, core::ptr::null_mut(), core::ptr::null_mut(), &[]);
        return INVALID;
    }

    if !unsafe { wamr::wasm_runtime_attach_shared_heap(instance, heap) } {
        teardown(module, instance, core::ptr::null_mut(), &[]);
        return INVALID;
    }

    let exec_env = unsafe { wamr::wasm_runtime_create_exec_env(instance, DEFAULT_STACK_SIZE) };
    if exec_env.is_null() {
        teardown(module, instance, core::ptr::null_mut(), &[]);
        return INVALID;
    }

    let handle = unsafe {
        crate::wasmmod::registry::pm_wasmmod_registry_publish(
            fqn.as_ptr(),
            fqn.len() as u32,
            container,
        )
    };
    if handle.index == u32::MAX {
        teardown(module, instance, exec_env, &[]);
        return INVALID;
    }
    // Bake package version from wasmmod.pkg (or source fallback) into the registry.
    if let Some(ver) = extract_pkg_version(&bytes) {
        unsafe {
            let _ = pm_wasmmod_registry_set_version(
                fqn.as_ptr(),
                fqn.len() as u32,
                ver.as_ptr(),
                ver.len() as u32,
            );
        }
    }

    let pack_tests = find_custom_section(&bytes, "wasmmod.tests")
        .and_then(parse_mpte)
        .unwrap_or_default();

    let mut claimed_slots: Vec<usize> = Vec::new();
    let mut tests_hit: usize = 0;
    let export_count = unsafe { wamr::wasm_runtime_get_export_count(module) };
    let mut load_failed = false;
    for export_index in 0..export_count {
        let mut export = wamr::wasm_export_t {
            name: core::ptr::null(),
            kind: 0,
            reserved: core::ptr::null_mut(),
        };
        unsafe { wamr::wasm_runtime_get_export_type(module, export_index, &mut export) };
        if export.kind != wamr::WASM_IMPORT_EXPORT_KIND_FUNC || export.name.is_null() {
            continue;
        }

        let func = unsafe { wamr::wasm_runtime_lookup_function(instance, export.name) };
        if func.is_null() {
            continue; // shouldn't happen for a real func export, but never worth aborting the whole load over
        }

        let name = unsafe { CStr::from_ptr(export.name) }.to_bytes();
        let name_str = core::str::from_utf8(name).unwrap_or("");

        // Pack tests are not product faces — register export name only;
        // registry runs them via loader_run_wasm_test (no trampoline pool).
        if let Some(t) = pack_tests.iter().find(|t| t.export == name_str) {
            let ok = unsafe {
                pm_wasmmod_registry_test_register_wasm(
                    fqn.as_ptr(),
                    fqn.len() as u32,
                    t.name.as_ptr(),
                    t.name.len() as u32,
                    t.export.as_ptr(),
                    t.export.len() as u32,
                )
            };
            if ok == 0 {
                load_failed = true;
                break;
            }
            tests_hit += 1;
            continue;
        }

        let Some(slot_index) = claim_adapter_slot(instance, exec_env, func) else {
            load_failed = true; // pool exhausted — roll the whole load back rather than publish a partial export set
            break;
        };
        claimed_slots.push(slot_index);

        let ok = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_export_set(
                handle,
                name.as_ptr(),
                name.len() as u32,
                pm_wasmmod_registry_export_kind_t::Fn,
                ADAPTER_FNS[slot_index] as *mut c_void,
            )
        };
        if ok == 0 {
            load_failed = true;
            break;
        }
    }

    if !load_failed && tests_hit != pack_tests.len() {
        // Section listed an export that is missing from the module.
        load_failed = true;
    }

    if load_failed {
        crate::wasmmod::registry::pm_wasmmod_registry_unpublish(handle);
        teardown(module, instance, exec_env, &claimed_slots);
        return INVALID;
    }

    let mut loaded = LOADED.lock();
    let index = handle.index as usize;
    if loaded.len() <= index {
        loaded.resize_with(index + 1, || None);
    }
    loaded[index] = Some(LoadedModule {
        fqn: String::from(fqn),
        generation: handle.generation,
        module,
        instance,
        exec_env,
        bytes,
        slots: claimed_slots,
    });

    handle
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_unload(handle: pm_wasmmod_registry_handle_t) -> i32 {
    let taken = {
        let mut loaded = LOADED.lock();
        match loaded.get_mut(handle.index as usize) {
            Some(slot @ Some(_)) if slot.as_ref().unwrap().generation == handle.generation => {
                slot.take()
            }
            _ => None,
        }
    };
    let Some(loaded) = taken else { return -1 };

    if crate::wasmmod::registry::pm_wasmmod_registry_unpublish(handle) == 0 {
        // Handle was already stale at the registry's own layer — put
        // our record back rather than silently drop it out from under
        // whatever still thinks it owns this slot.
        let mut guard = LOADED.lock();
        guard[handle.index as usize] = Some(loaded);
        return -1;
    }

    teardown(
        loaded.module,
        loaded.instance,
        loaded.exec_env,
        &loaded.slots,
    );
    0
}

fn loaded_instance(handle: pm_wasmmod_registry_handle_t) -> Option<wamr::wasm_module_inst_t> {
    let loaded = LOADED.lock();
    match loaded.get(handle.index as usize) {
        Some(Some(m)) if m.generation == handle.generation => Some(m.instance),
        _ => None,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_addr_to_native(
    handle: pm_wasmmod_registry_handle_t,
    addr: pm_addr_t,
) -> *mut c_void {
    if addr.space == PM_ADDR_SPACE_NATIVE {
        return addr.off as usize as *mut c_void;
    }
    let Some(instance) = loaded_instance(handle) else {
        return core::ptr::null_mut();
    };
    unsafe { wamr::wasm_runtime_addr_app_to_native(instance, addr.off) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_native_to_addr(
    handle: pm_wasmmod_registry_handle_t,
    space: u32,
    native: *const c_void,
) -> pm_addr_t {
    if space == PM_ADDR_SPACE_NATIVE {
        return pm_addr_t {
            space: PM_ADDR_SPACE_NATIVE,
            off: native as usize as u64,
        };
    }
    let Some(instance) = loaded_instance(handle) else {
        return pm_addr_t {
            space: PM_ADDR_SPACE_NATIVE,
            off: 0,
        };
    };
    let app = unsafe { wamr::wasm_runtime_addr_native_to_app(instance, native as *mut c_void) };
    pm_addr_t { space, off: app }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_shared_alloc(
    handle: pm_wasmmod_registry_handle_t,
    len: u32,
    out: *mut pm_buf_t,
) -> i32 {
    if out.is_null() || len == 0 {
        return -1;
    }
    let Some(instance) = loaded_instance(handle) else {
        return -1;
    };
    unsafe {
        let mut native: *mut c_void = core::ptr::null_mut();
        let app = wamr::wasm_runtime_shared_heap_malloc(instance, len as u64, &mut native);
        if app == 0 || native.is_null() {
            return -1;
        }
        *out = pm_buf_t {
            ptr: pm_addr_t {
                space: PM_ADDR_SPACE_SHARED,
                off: app,
            },
            len,
        };
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_shared_free(
    handle: pm_wasmmod_registry_handle_t,
    addr: pm_addr_t,
) -> i32 {
    if addr.space != PM_ADDR_SPACE_SHARED || addr.off == 0 {
        return -1;
    }
    let Some(instance) = loaded_instance(handle) else {
        return -1;
    };
    unsafe { wamr::wasm_runtime_shared_heap_free(instance, addr.off) };
    0
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_init, "int32_t(void)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_load, "pm_wasmmod_registry_handle_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_bake_pkg_version, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_unload, "int32_t(pm_wasmmod_registry_handle_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_addr_to_native, "void *(pm_wasmmod_registry_handle_t, pm_addr_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_native_to_addr, "pm_addr_t(pm_wasmmod_registry_handle_t, uint32_t, void *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_shared_alloc, "int32_t(pm_wasmmod_registry_handle_t, uint32_t, pm_buf_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.loader", pm_wasmmod_loader_shared_free, "int32_t(pm_wasmmod_registry_handle_t, pm_addr_t)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
