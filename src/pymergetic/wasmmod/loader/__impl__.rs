//! pymergetic.wasmmod.loader — turns `.wasm` bytes into directly-callable
//! `pymergetic.wasmmod.registry` entries: load -> instantiate -> attach
//! the shared heap -> enumerate exports -> claim one trampoline adapter
//! per export -> `registry.export_set`. This is the one place in the
//! tree that knows WAMR exists; the registry itself never does (see
//! docs/REGISTRY.md "Value convention" and this session's design notes
//! in docs/SOURCETREE.md's decision log).
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use alloc::boxed::Box;
use alloc::vec::Vec;
use core::ffi::{c_char, c_void, CStr};

use crate::util::lock::Mutex;
use crate::util::mem::{pm_util_mem_alloc, pm_util_mem_arena_create, pm_util_mem_arena_overhead, pm_util_mem_arena_t};
use crate::wasmmod::registry::{
    pm_wasmmod_registry_container_kind_t, pm_wasmmod_registry_export_kind_t, pm_wasmmod_registry_fn_t,
    pm_wasmmod_registry_handle_t, pm_wasmmod_registry_valkind_t, pm_wasmmod_registry_value_t,
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
        pub fn wasm_runtime_get_export_type(module: wasm_module_t, export_index: i32, export_type: *mut wasm_export_t);

        pub fn wasm_runtime_lookup_function(module_inst: wasm_module_inst_t, name: *const c_char) -> wasm_function_inst_t;

        pub fn wasm_runtime_create_exec_env(module_inst: wasm_module_inst_t, stack_size: u32) -> wasm_exec_env_t;
        pub fn wasm_runtime_destroy_exec_env(exec_env: wasm_exec_env_t);

        pub fn wasm_runtime_call_wasm_a(
            exec_env: wasm_exec_env_t,
            function: wasm_function_inst_t,
            num_results: u32,
            results: *mut wasm_val_t,
            num_args: u32,
            args: *mut wasm_val_t,
        ) -> bool;

        pub fn wasm_runtime_create_shared_heap(init_args: *mut SharedHeapInitArgs) -> wasm_shared_heap_t;
        pub fn wasm_runtime_attach_shared_heap(module_inst: wasm_module_inst_t, shared_heap: wasm_shared_heap_t) -> bool;
        pub fn wasm_runtime_detach_shared_heap(module_inst: wasm_module_inst_t);

        pub fn wasm_runtime_get_exception(module_inst: wasm_module_inst_t) -> *const c_char;

        pub fn wasm_runtime_get_file_package_type(buf: *const u8, size: u32) -> u32;
    }
}

// ---------------------------------------------------------------------
// Shared heap: one `pm_util_mem_arena_t` reserved once at init, one
// fixed-size block carved from it via `pm_util_mem_alloc` and handed to
// WAMR as `SharedHeapInitArgs::pre_allocated_addr` — see the plan's
// "Buffer/string marshaling" decision. `backing` is never freed (this
// is a once-per-process reservation, same lifetime as the runtime
// itself); the arena and the shared heap it backs both live exactly as
// long as `backing` does, so keeping the `Box` alive here *is* keeping
// them alive.
// ---------------------------------------------------------------------
const SHARED_HEAP_SIZE: u32 = 1024 * 1024; // 1 MiB; must be a whole multiple of the OS page size (WAMR rejects otherwise)

struct SharedHeapState {
    heap: wamr::wasm_shared_heap_t,
    #[allow(dead_code)] // never read back; kept alive purely so `backing`'s bytes (and the arena/shared-heap pointers into them) stay valid
    arena: *mut pm_util_mem_arena_t,
    #[allow(dead_code)]
    backing: Box<[u8]>,
}

// SAFETY: `heap`/`arena` are opaque addresses (WAMR's own runtime
// object and this crate's own arena, respectively), never thread-affine
// data — every access goes through SHARED_HEAP's Mutex.
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

static ADAPTER_SLOTS: Mutex<[AdapterSlot; MAX_ADAPTER_SLOTS]> = Mutex::new([AdapterSlot::EMPTY; MAX_ADAPTER_SLOTS]);

fn claim_adapter_slot(
    instance: wamr::wasm_module_inst_t,
    exec_env: wamr::wasm_exec_env_t,
    func: wamr::wasm_function_inst_t,
) -> Option<usize> {
    let mut slots = ADAPTER_SLOTS.lock();
    for (index, slot) in slots.iter_mut().enumerate() {
        if !slot.claimed {
            *slot = AdapterSlot { claimed: true, instance, exec_env, func };
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
        pm_wasmmod_registry_valkind_t::I32 => {
            wamr::wasm_val_t { kind: wamr::WASM_I32, of: wamr::wasm_val_of_t { i32: unsafe { v.of.i32 } } }
        }
        pm_wasmmod_registry_valkind_t::I64 => {
            wamr::wasm_val_t { kind: wamr::WASM_I64, of: wamr::wasm_val_of_t { i64: unsafe { v.of.i64 } } }
        }
        pm_wasmmod_registry_valkind_t::F32 => {
            wamr::wasm_val_t { kind: wamr::WASM_F32, of: wamr::wasm_val_of_t { f32: unsafe { v.of.f32 } } }
        }
        pm_wasmmod_registry_valkind_t::F64 => {
            wamr::wasm_val_t { kind: wamr::WASM_F64, of: wamr::wasm_val_of_t { f64: unsafe { v.of.f64 } } }
        }
    }
}

fn from_wasm_val(v: &wamr::wasm_val_t) -> pm_wasmmod_registry_value_t {
    use crate::wasmmod::registry::pm_wasmmod_registry_value_of_t as Of;
    match v.kind {
        wamr::WASM_I64 => {
            pm_wasmmod_registry_value_t { kind: pm_wasmmod_registry_valkind_t::I64, of: Of { i64: unsafe { v.of.i64 } } }
        }
        wamr::WASM_F32 => {
            pm_wasmmod_registry_value_t { kind: pm_wasmmod_registry_valkind_t::F32, of: Of { f32: unsafe { v.of.f32 } } }
        }
        wamr::WASM_F64 => {
            pm_wasmmod_registry_value_t { kind: pm_wasmmod_registry_valkind_t::F64, of: Of { f64: unsafe { v.of.f64 } } }
        }
        // WASM_I32 and anything unrecognized (never produced by our own
        // to_wasm_val, but a future signature shape might add one)
        // both fall back to i32 rather than panicking mid-call.
        _ => pm_wasmmod_registry_value_t { kind: pm_wasmmod_registry_valkind_t::I32, of: Of { i32: unsafe { v.of.i32 } } },
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

    let mut wasm_args = [wamr::wasm_val_t { kind: 0, of: wamr::wasm_val_of_t { i64: 0 } }; MAX_VALUES];
    for (i, slot) in wasm_args.iter_mut().enumerate().take(nargs as usize) {
        // SAFETY: caller (pm_wasmmod_registry_call, via the registry's
        // Fn convention) contracts `args`/`nargs` to describe a valid
        // array of at least `nargs` values for the call's duration.
        let arg = unsafe { &*args.add(i) };
        *slot = to_wasm_val(arg);
    }

    let mut wasm_results = [wamr::wasm_val_t { kind: 0, of: wamr::wasm_val_of_t { i64: 0 } }; MAX_VALUES];
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
fn teardown(module: wamr::wasm_module_t, instance: wamr::wasm_module_inst_t, exec_env: wamr::wasm_exec_env_t, slots: &[usize]) {
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

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_init() -> i32 {
    let mut guard = SHARED_HEAP.lock();
    if guard.is_some() {
        return 0; // idempotent — see pm_wasmmod_registry_init's own posture
    }

    if !unsafe { wamr::wasm_runtime_init() } {
        return -1;
    }

    let overhead = unsafe { pm_util_mem_arena_overhead() };
    // Generous slack, not just `+ 4096`: same posture as
    // pymergetic.util.mem's own heap_grow_pool sizing fix — TLSF's
    // segmented-fit rounding needs headroom proportional to the
    // request, not a flat constant, to reliably find a big enough
    // block for one single SHARED_HEAP_SIZE-sized allocation.
    let backing_size = overhead + SHARED_HEAP_SIZE as usize * 2;
    let mut backing: Box<[u8]> = alloc::vec![0u8; backing_size].into_boxed_slice();

    let arena = unsafe { pm_util_mem_arena_create(backing.as_mut_ptr() as *mut c_void, backing.len()) };
    if arena.is_null() {
        return -1;
    }

    let shared_heap_ptr = unsafe { pm_util_mem_alloc(arena, SHARED_HEAP_SIZE as usize) };
    if shared_heap_ptr.is_null() {
        return -1;
    }

    let mut init_args = wamr::SharedHeapInitArgs { size: SHARED_HEAP_SIZE, pre_allocated_addr: shared_heap_ptr };
    let heap = unsafe { wamr::wasm_runtime_create_shared_heap(&mut init_args) };
    if heap.is_null() {
        return -1;
    }

    *guard = Some(SharedHeapState { heap, arena, backing });
    0
}

const DEFAULT_STACK_SIZE: u32 = 32 * 1024;
const DEFAULT_HEAP_SIZE: u32 = 64 * 1024;

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_load(
    fqn_ptr: *const u8,
    fqn_len: u32,
    bytes_ptr: *const u8,
    bytes_len: u32,
) -> pm_wasmmod_registry_handle_t {
    const INVALID: pm_wasmmod_registry_handle_t = pm_wasmmod_registry_handle_t { index: u32::MAX, generation: 0 };

    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else { return INVALID };
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
    let mut bytes: Vec<u8> = unsafe { core::slice::from_raw_parts(bytes_ptr, bytes_len as usize) }.to_vec();

    // Real detection, not a hardcoded guess: `.aot` and `.wasm` are
    // told apart from the bytes themselves (magic-number sniff inside
    // WAMR), before `wasm_runtime_load` — the same call handles both
    // container kinds internally, so nothing else about the load path
    // below needs to branch on this; only the registry publish call
    // does. Anything WAMR doesn't recognize falls back to Wasm rather
    // than a new failure mode — wasm_runtime_load's own validation is
    // still the real gatekeeper for genuinely malformed input.
    let container = match unsafe { wamr::wasm_runtime_get_file_package_type(bytes.as_ptr(), bytes.len() as u32) } {
        wamr::WASM_MODULE_AOT => pm_wasmmod_registry_container_kind_t::Aot,
        _ => pm_wasmmod_registry_container_kind_t::Wasm,
    };

    let mut error_buf = [0u8; 256];
    let module = unsafe { wamr::wasm_runtime_load(bytes.as_mut_ptr(), bytes.len() as u32, error_buf.as_mut_ptr() as *mut c_char, error_buf.len() as u32) };
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

    let handle = unsafe { crate::wasmmod::registry::pm_wasmmod_registry_publish(fqn.as_ptr(), fqn.len() as u32, container) };
    if handle.index == u32::MAX {
        teardown(module, instance, exec_env, &[]);
        return INVALID;
    }

    let mut claimed_slots: Vec<usize> = Vec::new();
    let export_count = unsafe { wamr::wasm_runtime_get_export_count(module) };
    let mut load_failed = false;
    for export_index in 0..export_count {
        let mut export = wamr::wasm_export_t { name: core::ptr::null(), kind: 0, reserved: core::ptr::null_mut() };
        unsafe { wamr::wasm_runtime_get_export_type(module, export_index, &mut export) };
        if export.kind != wamr::WASM_IMPORT_EXPORT_KIND_FUNC || export.name.is_null() {
            continue;
        }

        let func = unsafe { wamr::wasm_runtime_lookup_function(instance, export.name) };
        if func.is_null() {
            continue; // shouldn't happen for a real func export, but never worth aborting the whole load over
        }

        let Some(slot_index) = claim_adapter_slot(instance, exec_env, func) else {
            load_failed = true; // pool exhausted — roll the whole load back rather than publish a partial export set
            break;
        };
        claimed_slots.push(slot_index);

        let name = unsafe { CStr::from_ptr(export.name) }.to_bytes();
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
    loaded[index] = Some(LoadedModule { generation: handle.generation, module, instance, exec_env, bytes, slots: claimed_slots });

    handle
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_unload(handle: pm_wasmmod_registry_handle_t) -> i32 {
    let taken = {
        let mut loaded = LOADED.lock();
        match loaded.get_mut(handle.index as usize) {
            Some(slot @ Some(_)) if slot.as_ref().unwrap().generation == handle.generation => slot.take(),
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

    teardown(loaded.module, loaded.instance, loaded.exec_env, &loaded.slots);
    0
}

#[cfg(test)]
mod tests {
    use super::*;

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
        let pid = std::process::id();
        let wasm_path = std::env::temp_dir().join(alloc::format!("pm_wasmmod_loader_test_{pid}.wasm"));
        let aot_path = std::env::temp_dir().join(alloc::format!("pm_wasmmod_loader_test_{pid}.aot"));
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

        let aot_bytes = std::fs::read(&aot_path).expect("read wamrc's .aot output");
        let _ = std::fs::remove_file(&wasm_path);
        let _ = std::fs::remove_file(&aot_path);
        aot_bytes
    }

    #[test]
    fn load_call_unload_roundtrips_end_to_end() {
        init_once();
        let bytes = fixture_bytes();
        let handle = unsafe { pm_wasmmod_loader_load("test.loader.e2e".as_ptr(), "test.loader.e2e".len() as u32, bytes.as_ptr(), bytes.len() as u32) };
        assert_ne!(handle.index, u32::MAX, "load should succeed on a well-formed fixture");

        // no-arg / one-result export
        let mut result = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                "test.loader.e2e".as_ptr(),
                "test.loader.e2e".len() as u32,
                "answer".as_ptr(),
                "answer".len() as u32,
                core::ptr::null(),
                0,
                &mut result,
                1,
            )
        };
        assert_eq!(status, 0);
        assert_eq!(unsafe { result.of.i32 }, 42);

        // numeric-arg / numeric-result export
        let arg = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 41 },
        };
        let mut result2 = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status2 = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                "test.loader.e2e".as_ptr(),
                "test.loader.e2e".len() as u32,
                "add_one".as_ptr(),
                "add_one".len() as u32,
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
        assert_eq!(unsafe { crate::wasmmod::registry::pm_wasmmod_registry_has("test.loader.e2e".as_ptr(), "test.loader.e2e".len() as u32) }, 0);
        let status_after_unload = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                "test.loader.e2e".as_ptr(),
                "test.loader.e2e".len() as u32,
                "answer".as_ptr(),
                "answer".len() as u32,
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

    #[test]
    fn load_call_unload_roundtrips_through_real_pmm_pack() {
        init_once();
        let wasm_bytes = pack_hello_example();
        assert_eq!(
            unsafe { wamr::wasm_runtime_get_file_package_type(wasm_bytes.as_ptr(), wasm_bytes.len() as u32) },
            wamr::WASM_MODULE_BYTECODE
        );

        let fqn = "test.loader.pmm_pack_e2e";
        let handle = unsafe { pm_wasmmod_loader_load(fqn.as_ptr(), fqn.len() as u32, wasm_bytes.as_ptr(), wasm_bytes.len() as u32) };
        assert_ne!(handle.index, u32::MAX, "load should succeed on a real pmm-packed .wasm");

        // hello() -> 42
        let mut result = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                fqn.as_ptr(),
                fqn.len() as u32,
                "hello".as_ptr(),
                "hello".len() as u32,
                core::ptr::null(),
                0,
                &mut result,
                1,
            )
        };
        assert_eq!(status, 0);
        assert_eq!(unsafe { result.of.i32 }, 42);

        // add(41, 1) -> 42
        let args = [
            pm_wasmmod_registry_value_t {
                kind: pm_wasmmod_registry_valkind_t::I32,
                of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 41 },
            },
            pm_wasmmod_registry_value_t {
                kind: pm_wasmmod_registry_valkind_t::I32,
                of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 1 },
            },
        ];
        let mut result2 = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status2 = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                fqn.as_ptr(),
                fqn.len() as u32,
                "add".as_ptr(),
                "add".len() as u32,
                args.as_ptr(),
                args.len() as u32,
                &mut result2,
                1,
            )
        };
        assert_eq!(status2, 0);
        assert_eq!(unsafe { result2.of.i32 }, 42);

        assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, 0);
        assert_eq!(unsafe { crate::wasmmod::registry::pm_wasmmod_registry_has(fqn.as_ptr(), fqn.len() as u32) }, 0);
    }

    #[test]
    fn load_call_unload_roundtrips_through_real_aot() {
        init_once();
        let aot_bytes = compile_to_aot(&fixture_bytes());
        // Sanity-check the fixture actually *is* AOT before trusting
        // the rest of this test — if wamrc's own magic number ever
        // changes, better to fail loudly here than have the loader's
        // detection silently degrade to treating it as Wasm.
        assert_eq!(
            unsafe { wamr::wasm_runtime_get_file_package_type(aot_bytes.as_ptr(), aot_bytes.len() as u32) },
            wamr::WASM_MODULE_AOT
        );

        let fqn = "test.loader.aot_e2e";
        let handle = unsafe { pm_wasmmod_loader_load(fqn.as_ptr(), fqn.len() as u32, aot_bytes.as_ptr(), aot_bytes.len() as u32) };
        assert_ne!(handle.index, u32::MAX, "load should succeed on a real .aot file");

        let mut result = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                fqn.as_ptr(),
                fqn.len() as u32,
                "answer".as_ptr(),
                "answer".len() as u32,
                core::ptr::null(),
                0,
                &mut result,
                1,
            )
        };
        assert_eq!(status, 0);
        assert_eq!(unsafe { result.of.i32 }, 42);

        let arg = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 41 },
        };
        let mut result2 = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: crate::wasmmod::registry::pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        let status2 = unsafe {
            crate::wasmmod::registry::pm_wasmmod_registry_call(
                fqn.as_ptr(),
                fqn.len() as u32,
                "add_one".as_ptr(),
                "add_one".len() as u32,
                &arg,
                1,
                &mut result2,
                1,
            )
        };
        assert_eq!(status2, 0);
        assert_eq!(unsafe { result2.of.i32 }, 42);

        assert_eq!(unsafe { pm_wasmmod_loader_unload(handle) }, 0);
        assert_eq!(unsafe { crate::wasmmod::registry::pm_wasmmod_registry_has(fqn.as_ptr(), fqn.len() as u32) }, 0);
    }

    #[test]
    fn load_of_garbage_bytes_fails_cleanly_without_touching_the_registry() {
        init_once();
        let bytes = [0u8, 1, 2, 3];
        let handle = unsafe { pm_wasmmod_loader_load("test.loader.garbage".as_ptr(), "test.loader.garbage".len() as u32, bytes.as_ptr(), bytes.len() as u32) };
        assert_eq!(handle.index, u32::MAX);
        assert_eq!(unsafe { crate::wasmmod::registry::pm_wasmmod_registry_has("test.loader.garbage".as_ptr(), "test.loader.garbage".len() as u32) }, 0);
    }
}
