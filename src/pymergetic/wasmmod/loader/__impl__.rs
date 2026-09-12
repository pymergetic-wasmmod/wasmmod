//! pymergetic.wasmmod.loader — turns `.wasm` bytes into directly-callable
//! `pymergetic.wasmmod.registry` entries: load -> instantiate -> attach
//! the shared heap -> enumerate exports -> claim one trampoline adapter
//! per export -> `registry.export_set`. This is the one place in the
//! tree that knows WAMR exists; the registry itself never does (see
//! docs/REGISTRY.md "Value convention" and this session's design notes
//! in docs/SOURCETREE.md's decision log).
//!
//! Subset discipline (this file compiles through the kernel's own rsx
//! chain — ksweep), same posture as registry / net.search: ptr+len byte
//! spans instead of `String`/`&[u8]` slices, fixed-capacity rows with
//! explicit counts instead of `Vec`, one raw lock primitive instead of
//! `Mutex<T>`, and the 8 trampoline addresses hand-written instead of a
//! `macro_rules!` (what made them distinct was only their address, and
//! that is exactly what hand-writing preserves). The WAMR externs live
//! flat in this file, not behind `mod wamr {}` — the subset has no
//! nested modules, and the names are this card's private FFI boundary.
//!
//! `LOADED` stays under one lock for a whole load: the registry's own
//! table lock and the adapter pool's lock are always taken *without*
//! holding this one, so the lock order never inverts.
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use core::cell::UnsafeCell;
use core::ffi::c_void;
use core::ptr;

/* The registry's shared ABI shapes + the Value-convention entry points,
 * from the registry's `__types__.rs` face (the Rust twin of
 * `__types__.h` — same file the registry itself includes, no second
 * definition). For cargo this is an ordinary module include; for the
 * in-kernel rsx compile the build face splices the file into the unit. */
#[path = "../registry/__types__.rs"]
mod registry_types;

use registry_types::{
    pm_wasmmod_registry_container_kind_t, pm_wasmmod_registry_export_kind_t,
    pm_wasmmod_registry_handle_t, pm_wasmmod_registry_valkind_t, pm_wasmmod_registry_value_of_t,
    pm_wasmmod_registry_value_t,
};

/* The registry entry points this card calls, declared as the card's own
 * FFI boundary (same posture as the WAMR extern block below: the linker
 * resolves the symbols from the registry's object; the decl here is
 * what both cargo and the in-kernel rsx compile type against). */
/// Registry guest-test hook type — twin of the one the registry itself
/// owns (`registry/__impl__.rs`). A fn type is ABI-stable, so declaring
/// the twin here (instead of path-reaching the registry's private
/// module item) keeps this card's FFI boundary self-contained, exactly
/// like every other extern in this file.
pub type pm_wasmmod_registry_wasm_test_runner_t =
    unsafe extern "C" fn(*const u8, u32, *const u8, u32) -> i32;

unsafe extern "C" {
    fn pm_wasmmod_registry_publish(
        fqn_ptr: *const u8,
        fqn_len: u32,
        container: pm_wasmmod_registry_container_kind_t,
    ) -> pm_wasmmod_registry_handle_t;
    fn pm_wasmmod_registry_unpublish(handle: pm_wasmmod_registry_handle_t) -> i32;
    fn pm_wasmmod_registry_set_version(
        fqn_ptr: *const u8,
        fqn_len: u32,
        ver_ptr: *const u8,
        ver_len: u32,
    ) -> i32;
    fn pm_wasmmod_registry_export_set(
        handle: pm_wasmmod_registry_handle_t,
        name_ptr: *const u8,
        name_len: u32,
        kind: pm_wasmmod_registry_export_kind_t,
        ptr: *mut c_void,
    ) -> i32;
    fn pm_wasmmod_registry_test_register_wasm(
        fqn_ptr: *const u8,
        fqn_len: u32,
        name_ptr: *const u8,
        name_len: u32,
        export_ptr: *const u8,
        export_len: u32,
    ) -> i32;
    fn pm_wasmmod_registry_set_wasm_test_runner(f: Option<pm_wasmmod_registry_wasm_test_runner_t>);
}

// ---------------------------------------------------------------------
// WAMR — hand-written externs for the one subset of
// third_party/wamr/core/iwasm/include/wasm_export.h this loader needs.
// Not a bindgen mirror of *our own* module (that convention is for
// pymergetic-owned faces, see mem/__exports__.rs) — WAMR is vendored,
// so this is the loader's own private FFI boundary, same posture as
// any other hand-written extern block onto a wrapped third-party C lib.
// ---------------------------------------------------------------------
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
    pub name: *const i8,
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

    /* The runtime's own allocator, which is where an image buffer comes from:
     * every seat that can load a module has inited the runtime by definition
     * (pm_wasmmod_loader_init below), and on a board that allocator is the
     * seat's arena. A row used to carry the buffer as a fixed array instead —
     * see LOADED. */
    pub fn wasm_runtime_malloc(size: u32) -> *mut c_void;
    pub fn wasm_runtime_free(ptr: *mut c_void);

    pub fn wasm_runtime_load(
        buf: *mut u8,
        size: u32,
        error_buf: *mut i8,
        error_buf_size: u32,
    ) -> wasm_module_t;
    pub fn wasm_runtime_unload(module: wasm_module_t);

    pub fn wasm_runtime_instantiate(
        module: wasm_module_t,
        default_stack_size: u32,
        host_managed_heap_size: u32,
        error_buf: *mut i8,
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
        name: *const i8,
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

    pub fn wasm_runtime_get_exception(module_inst: wasm_module_inst_t) -> *const i8;

    pub fn wasm_runtime_get_file_package_type(buf: *const u8, size: u32) -> u32;
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
const SHARED_HEAP_SIZE: u32 = 1048576; // 1 MiB (1024 * 1024)

/* Copy so the `*cell` snapshot in shared_heap() can read the Option out
 * of the raw pointer (the registry's Option<fn> cells are Copy the same
 * way). */
#[derive(Clone, Copy)]
struct SharedHeapState {
    heap: wasm_shared_heap_t,
}

/* pymergetic.util.lock — the project's one lock card (RS muscle, C ABI),
 * declared the same way registry / net.search declare it: raw primitive,
 * caller payload. The lock card is impl = rs, so this is not a second
 * lock, it is the same one, reached through its C face. */
#[repr(C)]
struct pm_util_lock_t {
    locked: u32,
}

unsafe extern "C" {
    fn pm_util_lock_acquire(l: *mut pm_util_lock_t);
    fn pm_util_lock_release(l: *mut pm_util_lock_t);
}

/* Transparent static discipline (same as registry / net.search):
 * Mut<T> is a one-field tuple struct rsx maps to its inner type,
 * UnsafeCell::get hands out the pointer. */
struct Mut<T>(UnsafeCell<T>);
unsafe impl<T> Sync for Mut<T> {}

static SHARED_HEAP: Mut<Option<SharedHeapState>> = Mut(UnsafeCell::new(None));
static HEAP_LOCK: Mut<pm_util_lock_t> = Mut(UnsafeCell::new(pm_util_lock_t { locked: 0 }));

unsafe fn heap_lock() -> *mut pm_util_lock_t {
    HEAP_LOCK.0.get()
}

unsafe fn shared_heap() -> wasm_shared_heap_t {
    unsafe {
        pm_util_lock_acquire(heap_lock());
        let st = *(SHARED_HEAP.0.get());
        pm_util_lock_release(heap_lock());
        /* match-in-let with an early return, the asgi-proven shape — a
         * match tail in fn-return position is not in the subset. */
        let heap = match st {
            Some(s) => s.heap,
            None => return ptr::null_mut(),
        };
        heap
    }
}

// ---------------------------------------------------------------------
// Adapter pool: a small fixed set of hand-written trampoline stubs.
// Every stub has the exact same body (`adapter_invoke`) — what makes
// them distinct is only their *address*, which is what lets N
// concurrently-loaded exports each get a real, distinguishable
// `pm_wasmmod_registry_fn_t` to publish, despite a bare C function
// pointer having nowhere to close over which wasm instance/function it
// should call. "Claim a slot" = hand out one of these N addresses and
// remember which exec_env/function it now means, until unload releases
// it back to the pool. The `macro_rules!` that used to mint these N
// addresses is the subset's hand-written form — same bodies, same
// addresses, no macro.
// ---------------------------------------------------------------------
const MAX_ADAPTER_SLOTS: usize = 8;
const MAX_VALUES: usize = 8; // args/results per call this milestone's proof needs to cover; raise if a future fixture needs more
const INVALID_HANDLE_INDEX: u32 = 4294967295u32;

#[derive(Clone, Copy)]
struct AdapterSlot {
    claimed: bool,
    instance: wasm_module_inst_t,
    exec_env: wasm_exec_env_t,
    func: wasm_function_inst_t,
}

static ADAPTER_SLOTS: Mut<[AdapterSlot; MAX_ADAPTER_SLOTS]> = Mut(UnsafeCell::new([
    AdapterSlot {
        claimed: false,
        instance: core::ptr::null_mut(),
        exec_env: core::ptr::null_mut(),
        func: core::ptr::null_mut(),
    }; MAX_ADAPTER_SLOTS
]));
static ADAPTER_LOCK: Mut<pm_util_lock_t> = Mut(UnsafeCell::new(pm_util_lock_t { locked: 0 }));

unsafe fn adapter_lock() -> *mut pm_util_lock_t {
    ADAPTER_LOCK.0.get()
}

/* Pointer to the first element (not `*mut [T; N]` — a ptr-to-array return
 * needs the C declarator form `T (*f(void))[N]`, which the subset's fn-ret
 * render does not spell). */
unsafe fn adapter_slots() -> *mut AdapterSlot {
    ADAPTER_SLOTS.0.get() as *mut AdapterSlot
}

/// MAX_ADAPTER_SLOTS when the pool is exhausted (callers refuse loudly).
unsafe fn claim_adapter_slot(
    instance: wasm_module_inst_t,
    exec_env: wasm_exec_env_t,
    func: wasm_function_inst_t,
) -> usize {
    unsafe {
        pm_util_lock_acquire(adapter_lock());
        let slots = adapter_slots();
        let mut index = MAX_ADAPTER_SLOTS;
        let mut i = 0usize;
        while i < MAX_ADAPTER_SLOTS {
            if !(*slots.add(i)).claimed {
                *slots.add(i) = AdapterSlot {
                    claimed: true,
                    instance,
                    exec_env,
                    func,
                };
                index = i;
                break;
            }
            i += 1;
        }
        pm_util_lock_release(adapter_lock());
        index
    }
}

unsafe fn release_adapter_slot(index: usize) {
    unsafe {
        pm_util_lock_acquire(adapter_lock());
        let slots = adapter_slots();
        *slots.add(index) = AdapterSlot {
            claimed: false,
            instance: ptr::null_mut(),
            exec_env: ptr::null_mut(),
            func: ptr::null_mut(),
        };
        pm_util_lock_release(adapter_lock());
    }
}

fn to_wasm_val(v: *const pm_wasmmod_registry_value_t) -> wasm_val_t {
    let kind = unsafe { (*v).kind };
    if kind == pm_wasmmod_registry_valkind_t::I64 {
        return wasm_val_t {
            kind: WASM_I64,
            of: wasm_val_of_t {
                i64: unsafe { (*v).of.i64 },
            },
        };
    }
    if kind == pm_wasmmod_registry_valkind_t::F32 {
        return wasm_val_t {
            kind: WASM_F32,
            of: wasm_val_of_t {
                f32: unsafe { (*v).of.f32 },
            },
        };
    }
    if kind == pm_wasmmod_registry_valkind_t::F64 {
        return wasm_val_t {
            kind: WASM_F64,
            of: wasm_val_of_t {
                f64: unsafe { (*v).of.f64 },
            },
        };
    }
    // I32 and anything unrecognized (never produced by our own
    // to_wasm_val, but a future signature shape might add one) both
    // fall back to i32 rather than panicking mid-call.
    wasm_val_t {
        kind: WASM_I32,
        of: wasm_val_of_t {
            i32: unsafe { (*v).of.i32 },
        },
    }
}

fn from_wasm_val(v: *const wasm_val_t) -> pm_wasmmod_registry_value_t {
    let kind = unsafe { (*v).kind };
    if kind == WASM_I64 {
        return pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I64,
            of: pm_wasmmod_registry_value_of_t {
                i64: unsafe { (*v).of.i64 },
            },
        };
    }
    if kind == WASM_F32 {
        return pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::F32,
            of: pm_wasmmod_registry_value_of_t {
                f32: unsafe { (*v).of.f32 },
            },
        };
    }
    if kind == WASM_F64 {
        return pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::F64,
            of: pm_wasmmod_registry_value_of_t {
                f64: unsafe { (*v).of.f64 },
            },
        };
    }
    pm_wasmmod_registry_value_t {
        kind: pm_wasmmod_registry_valkind_t::I32,
        of: pm_wasmmod_registry_value_of_t {
            i32: unsafe { (*v).of.i32 },
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
    let instance: wasm_module_inst_t;
    let exec_env: wasm_exec_env_t;
    let func: wasm_function_inst_t;
    unsafe {
        pm_util_lock_acquire(adapter_lock());
        let slots = adapter_slots();
        if !(*slots.add(slot_index)).claimed {
            pm_util_lock_release(adapter_lock());
            return -1;
        }
        instance = (*slots.add(slot_index)).instance;
        exec_env = (*slots.add(slot_index)).exec_env;
        func = (*slots.add(slot_index)).func;
        pm_util_lock_release(adapter_lock());
    }

    // WAMR's hardware bound-check needs a per-*OS-thread* signal/altstack
    // setup, done automatically only for the one thread that happened to
    // call wasm_runtime_init() (see pm_wasmmod_loader_init) — any other
    // thread calling into wasm execution (the registry's whole point:
    // "callable in any direction", not just from the thread that loaded
    // it) needs this explicitly, once. Idempotent per-thread on WAMR's
    // side (a thread-local bool short-circuits a second call), so an
    // unconditional call here — on every call, not just the first per
    // thread — costs one cheap check, not a real re-init.
    let _ = unsafe { wasm_runtime_init_thread_env() };

    let mut wasm_args: [wasm_val_t; MAX_VALUES] = [wasm_val_t {
        kind: 0,
        of: wasm_val_of_t { i64: 0 },
    }; MAX_VALUES];
    let mut i = 0usize;
    while i < nargs as usize {
        // SAFETY: caller (pm_wasmmod_registry_call, via the registry's
        // Fn convention) contracts `args`/`nargs` to describe a valid
        // array of at least `nargs` values for the call's duration.
        wasm_args[i] = to_wasm_val(unsafe { args.add(i) });
        i += 1;
    }

    let mut wasm_results: [wasm_val_t; MAX_VALUES] = [wasm_val_t {
        kind: 0,
        of: wasm_val_of_t { i64: 0 },
    }; MAX_VALUES];
    let ok = unsafe {
        wasm_runtime_call_wasm_a(
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
        // debugger stopped on this line can inspect *why, without
        // adding a whole new export-facing error-reporting story yet.
        let _exception = wamr_exception(instance);
        return -1;
    }

    i = 0usize;
    while i < nresults as usize {
        // SAFETY: same contract as `args` above, for `results`/`nresults`.
        unsafe { *results.add(i) = from_wasm_val(wasm_results.as_ptr().add(i)) };
        i += 1;
    }
    0
}

unsafe extern "C" fn adapter_slot_0(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(0, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_1(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(1, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_2(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(2, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_3(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(3, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_4(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(4, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_5(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(5, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_6(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(6, args, nargs, results, nresults)
}

unsafe extern "C" fn adapter_slot_7(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    adapter_invoke(7, args, nargs, results, nresults)
}

/// One real `pm_wasmmod_registry_fn_t` address per slot — index i here
/// must always be exactly adapter_slot_i, since claim_adapter_slot's
/// returned index is used directly into this mapping. A match (not the
/// old fn-pointer array) so the initializer stays a plain cast the
/// subset lowers inside a function body — the static array-of-fn form
/// would need taken addresses in a static initializer.
unsafe fn adapter_fn_at(index: usize) -> *mut c_void {
    match index {
        0 => adapter_slot_0 as *mut c_void,
        1 => adapter_slot_1 as *mut c_void,
        2 => adapter_slot_2 as *mut c_void,
        3 => adapter_slot_3 as *mut c_void,
        4 => adapter_slot_4 as *mut c_void,
        5 => adapter_slot_5 as *mut c_void,
        6 => adapter_slot_6 as *mut c_void,
        7 => adapter_slot_7 as *mut c_void,
        _ => ptr::null_mut(),
    }
}

// ---------------------------------------------------------------------
// Per-loaded-module bookkeeping, indexed 1:1 by the registry handle's
// own `index` (registry reuses a dead slot's index before growing, so
// this table's occupancy tracks the registry's exactly). Fixed rows,
// same budget posture as the registry's TABLE: an overflow is a loud
// INVALID handle, never a silent drop.
// ---------------------------------------------------------------------
const MOD_MAX: usize = 128;
const FQN_CAP: usize = 64;

/* ---- how big an image this seat will take ---------------------------
 *
 * A knob, owned here and read where the load happens, linked onto the seat's
 * list only so a listing can find it. It used to be a reservation: the row
 * carried the image as a fixed 65536-byte array, because WAMR mutates the
 * buffer it is handed and keeps referencing it until unload, so the loader
 * owns one copy. 128 rows of that was 8.4MB of a seat's bss standing empty on
 * a seat that had loaded nothing at all.
 *
 * The row owns one allocation of exactly the image's length now, taken from
 * the runtime's allocator when the image arrives and given back when the
 * module unloads. The knob is what a seat will accept, and moving it is how a
 * seat takes a pack larger than 64KB — which it simply could not before. Zero
 * means it will take whatever the allocator gives.
 */
const IMAGE_DEFAULT: u32 = 65536; // 64 * 1024, what a row used to reserve

#[repr(C)]
#[derive(Clone, Copy)]
struct pm_util_limit_t {
    name: *const u8,
    /// The card this knob is under, and what it counts there. `name` is the
    /// two joined; a seat that asks a card for its knobs matches on `module`.
    module: *const u8,
    leaf: *const u8,
    soft: u32,
    hard: u32,
    dflt: u32,
    used: *const u32,
    next: *mut pm_util_limit_t,
    /* `int32_t (*apply)(pm_util_limit_t *)` on the C side: the hook for a
     * card whose storage has to be reshaped the moment the knob moves. This
     * one reads the new soft at the next load, so it stays null — spelled as
     * the opaque pointer it is on every target, because spelling the fn type
     * puts its typedef ahead of this struct in the in-kernel compile (rsx
     * lowers a fn-pointer type in an earlier pass than the struct it names)
     * and a card the kernel cannot rebuild is not a card. */
    apply: *mut c_void,
}

/* The card's face, not a second declaration of it: the crate already binds
 * pm_util_limits_attach (util/limits/__exports__.rs, generated from the live
 * registry), where the knob is an opaque pointer. This card is the one that
 * owns the struct, so the layout is here and the pointer crosses as the face
 * spells it. */
unsafe fn limits_attach(knob: *mut pm_util_limit_t) -> i32 {
    unsafe {
        crate::util::limits::pm_util_limits_attach(
            knob as *mut crate::util::limits::pm_util_limit_t,
        )
    }
}

static IMAGE_KNOB: Mut<pm_util_limit_t> = Mut(UnsafeCell::new(pm_util_limit_t {
    name: "pymergetic.wasmmod.loader.image\0".as_ptr(),
    module: "pymergetic.wasmmod.loader\0".as_ptr(),
    leaf: "image\0".as_ptr(),
    soft: IMAGE_DEFAULT,
    hard: 0,
    dflt: IMAGE_DEFAULT,
    used: core::ptr::null(),
    next: core::ptr::null_mut(),
    apply: core::ptr::null_mut(),
}));

/// Room for an image of `n` bytes? A knob at zero never refuses; past that
/// the allocator is the only other thing that can say no.
unsafe fn image_room(n: u32) -> bool {
    let soft = unsafe { (*IMAGE_KNOB.0.get()).soft };
    soft == 0 || n <= soft
}

/// Give a row's image back: every way out of a load that does not end with
/// the row owning it, and unload once WAMR has let go of the buffer.
unsafe fn image_release(m: *mut LoadedModule) {
    unsafe {
        let p = (*m).bytes;
        if !p.is_null() {
            wasm_runtime_free(p as *mut c_void);
        }
        (*m).bytes = ptr::null_mut();
        (*m).bytes_len = 0;
    }
}

/* Copy: the static's `[row; MOD_MAX]` repeat (a const context) needs
 * the value copyable, same discipline as registry's ModEntry rows. */
#[derive(Clone, Copy)]
struct LoadedModule {
    live: bool,
    fqn: [u8; FQN_CAP],
    fqn_len: u32,
    generation: u32,
    module: wasm_module_t,
    instance: wasm_module_inst_t,
    exec_env: wasm_exec_env_t,
    // WAMR requires the byte buffer passed to wasm_runtime_load to
    // remain referenceable (and possibly mutated in place) until
    // wasm_runtime_unload — this row is that buffer's one owner, and owning
    // it is now holding the allocation rather than carrying a fixed array.
    bytes: *mut u8,
    bytes_len: u32,
    slots: [usize; MAX_ADAPTER_SLOTS],
    n_slots: u32,
}

static LOADED: Mut<[LoadedModule; MOD_MAX]> = Mut(UnsafeCell::new([
    LoadedModule {
        live: false,
        fqn: [0; FQN_CAP],
        fqn_len: 0,
        generation: 0,
        module: core::ptr::null_mut(),
        instance: core::ptr::null_mut(),
        exec_env: core::ptr::null_mut(),
        bytes: core::ptr::null_mut(),
        bytes_len: 0,
        slots: [0; MAX_ADAPTER_SLOTS],
        n_slots: 0,
    }; MOD_MAX
]));

static LOADED_LOCK: Mut<pm_util_lock_t> = Mut(UnsafeCell::new(pm_util_lock_t { locked: 0 }));

unsafe fn loaded_lock() -> *mut pm_util_lock_t {
    LOADED_LOCK.0.get()
}

/* Pointer to the first element — same ptr-to-first discipline as
 * adapter_slots (a ptr-to-array fn-ret render is not in the subset). */
unsafe fn loaded_table() -> *mut LoadedModule {
    LOADED.0.get() as *mut LoadedModule
}

/// The sentinel handle: index = u32::MAX. A const struct literal is not
/// an accepted rsx static-initializer shape, so the sentinel is a
/// builder fn — function bodies lower struct literals fine.
fn invalid_handle() -> pm_wasmmod_registry_handle_t {
    pm_wasmmod_registry_handle_t {
        index: INVALID_HANDLE_INDEX,
        generation: 0,
    }
}

fn fqn_matches(m: *const LoadedModule, fqn_ptr: *const u8, fqn_len: u32) -> bool {
    if unsafe { (*m).fqn_len } != fqn_len {
        return false;
    }
    let mut i = 0usize;
    while i < fqn_len as usize {
        let a = unsafe { *(*m).fqn.as_ptr().add(i) };
        let b = unsafe { *fqn_ptr.add(i) };
        if a != b {
            return false;
        }
        i += 1;
    }
    true
}

fn wamr_exception(module_inst: wasm_module_inst_t) -> &'static str {
    // SAFETY: module_inst is a live instance for the duration of this
    // call; wasm_runtime_get_exception's returned pointer is either
    // NULL or a runtime-owned nul-terminated string valid at least
    // that long.
    let p = unsafe { wasm_runtime_get_exception(module_inst) };
    if p.is_null() {
        return "";
    }
    /* Only the emptiness is consumed by callers (a debugger can read
     * the C string in place) — no need to decode UTF-8 in the subset. */
    unsafe {
        if *p == 0 {
            return "";
        }
    }
    ""
}

/// Tears down everything a partially-built load already created, in
/// reverse order — the one rollback path both a real failure midway
/// through `load()` and `unload()`'s happy path share.
fn teardown(
    module: wasm_module_t,
    instance: wasm_module_inst_t,
    exec_env: wasm_exec_env_t,
    slots: *const usize,
    n_slots: u32,
) {
    let mut i = 0usize;
    while i < n_slots as usize {
        unsafe { release_adapter_slot(*slots.add(i)) };
        i += 1;
    }
    if !exec_env.is_null() {
        unsafe { wasm_runtime_destroy_exec_env(exec_env) };
    }
    if !instance.is_null() {
        unsafe { wasm_runtime_detach_shared_heap(instance) };
        unsafe { wasm_runtime_deinstantiate(instance) };
    }
    if !module.is_null() {
        unsafe { wasm_runtime_unload(module) };
    }
}

/// Registry guest-test hook: call a `() -> i32` export on the loaded instance.
unsafe extern "C" fn loader_run_wasm_test(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_ptr: *const u8,
    export_len: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || export_ptr.is_null() || export_len == 0 {
        return -1;
    }
    let mut instance: wasm_module_inst_t = ptr::null_mut();
    let mut exec_env: wasm_exec_env_t = ptr::null_mut();
    unsafe {
        pm_util_lock_acquire(loaded_lock());
        let table = loaded_table();
        let mut i = 0usize;
        while i < MOD_MAX {
            let m = table.add(i);
            if (*m).live && fqn_matches(m, fqn_ptr, fqn_len) {
                instance = (*m).instance;
                exec_env = (*m).exec_env;
                break;
            }
            i += 1;
        }
        pm_util_lock_release(loaded_lock());
    }
    if instance.is_null() {
        return -1;
    }

    /* wasm_runtime_lookup_function wants a NUL-terminated name; pack
     * test names are short, so a fixed stack buffer with the NUL. */
    let mut name: [u8; 64] = [0; 64];
    let nl = export_len as usize;
    if nl >= 64 {
        return -1;
    }
    unsafe {
        ptr::copy_nonoverlapping(export_ptr, name.as_mut_ptr(), nl);
        name[nl] = 0;
    }
    let func = unsafe { wasm_runtime_lookup_function(instance, name.as_ptr() as *const i8) };
    if func.is_null() {
        return -1;
    }
    let _ = unsafe { wasm_runtime_init_thread_env() };
    let mut wasm_results: [wasm_val_t; 1] = [wasm_val_t {
        kind: 0,
        of: wasm_val_of_t { i64: 0 },
    }; 1];
    let ok = unsafe {
        wasm_runtime_call_wasm_a(
            exec_env,
            func,
            1,
            wasm_results.as_mut_ptr(),
            0,
            ptr::null_mut(),
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
    unsafe {
        /* The card's own knob, onto the seat's list. Before the early return
         * below: init is idempotent, and a seat should be able to read what it
         * will take whether or not this is the first call. */
        let _ = limits_attach(IMAGE_KNOB.0.get());
        pm_util_lock_acquire(heap_lock());
        let have = match *(SHARED_HEAP.0.get()) {
            Some(_) => true,
            None => false,
        };
        if have {
            pm_util_lock_release(heap_lock());
            // Still (re)install the guest-test hook — init is idempotent but
            // the runner must be live whenever a heap exists.
            pm_wasmmod_registry_set_wasm_test_runner(Some(loader_run_wasm_test));
            return 0;
        }

        if !wasm_runtime_init() {
            pm_util_lock_release(heap_lock());
            return -1;
        }

        let mut init_args = SharedHeapInitArgs {
            size: SHARED_HEAP_SIZE,
            pre_allocated_addr: ptr::null_mut(),
        };
        let heap = wasm_runtime_create_shared_heap(&mut init_args);
        if heap.is_null() {
            pm_util_lock_release(heap_lock());
            return -1;
        }

        *(SHARED_HEAP.0.get()) = Some(SharedHeapState { heap });
        pm_util_lock_release(heap_lock());
        pm_wasmmod_registry_set_wasm_test_runner(Some(loader_run_wasm_test));
    }
    0
}

const DEFAULT_STACK_SIZE: u32 = 32768; // 32 * 1024
const DEFAULT_HEAP_SIZE: u32 = 65536; // 64 * 1024

/* ---- byte readers over ptr+len (the subset has no slices) ----
 *
 * u16 reads return (value, ok); the u32 readers return 0 on
 * out-of-bounds, which every caller treats as malformed input. */

fn read_u16_le(p: *const u8, at: usize, n: usize) -> (u32, bool) {
    if at + 2 > n {
        return (0, false);
    }
    unsafe {
        (
            ((*p.add(at)) as u32) | (((*p.add(at + 1)) as u32) << 8),
            true,
        )
    }
}

fn read_u32_le(p: *const u8, at: usize, n: usize) -> u32 {
    if at + 4 > n {
        return 0;
    }
    unsafe {
        ((*p.add(at)) as u32)
            | (((*p.add(at + 1)) as u32) << 8)
            | (((*p.add(at + 2)) as u32) << 16)
            | (((*p.add(at + 3)) as u32) << 24)
    }
}

/// uLEB128 at `at`; returns (value, ok, cursor-past-the-value).
fn read_uleb(p: *const u8, n: usize, at: usize) -> (u32, bool, usize) {
    let mut result: u32 = 0;
    let mut shift: u32 = 0;
    let mut cur = at;
    while cur < n {
        let b = unsafe { *p.add(cur) };
        cur += 1;
        result |= ((b & 0x7f) as u32).wrapping_shl(shift);
        if b & 0x80 == 0 {
            return (result, true, cur);
        }
        shift += 7;
        if shift > 28 {
            return (0, false, cur);
        }
    }
    (0, false, cur)
}

fn span_eq(a: *const u8, a_len: usize, b: *const u8, b_len: usize) -> bool {
    if a_len != b_len {
        return false;
    }
    let mut i = 0usize;
    while i < a_len {
        let x = unsafe { *a.add(i) };
        let y = unsafe { *b.add(i) };
        if x != y {
            return false;
        }
        i += 1;
    }
    true
}

/// Find a Wasm custom section by name (id 0). Also works when the buffer
/// is a plain wasm embedded in larger artifacts that still start with
/// `\0asm`. `sec_start`/`sec_len` receive the section payload span
/// (offsets into the whole buffer).
fn find_wasm_custom_section(
    p: *const u8,
    n: usize,
    name: *const u8,
    name_len: usize,
    sec_start: *mut usize,
    sec_len: *mut usize,
) -> bool {
    if n < 8 {
        return false;
    }
    unsafe {
        if *p != 0 || *p.add(1) != b'a' || *p.add(2) != b's' || *p.add(3) != b'm' {
            return false;
        }
    }
    let mut cur = 8usize;
    while cur < n {
        let id = unsafe { *p.add(cur) };
        cur += 1;
        let (size, ok, next) = read_uleb(p, n, cur);
        if !ok {
            return false;
        }
        cur = next;
        let sz = size as usize;
        let sec = cur;
        if sec + sz > n {
            return false;
        }
        if id == 0 {
            /* inside the section: name length (uleb) then name then payload */
            let (nlen, ok2, q) = read_uleb(p, sec + sz, sec);
            if ok2 {
                let nl = nlen as usize;
                if q + nl <= sec + sz && unsafe { span_eq(p.add(q), nl, name, name_len) } {
                    unsafe {
                        *sec_start = q + nl;
                        *sec_len = sec + sz - (q + nl);
                    }
                    return true;
                }
            }
        }
        cur = sec + sz;
    }
    false
}

/// WAMR AOT custom section (type 100) — mirrors `mp_wasm_aot_find_section`.
fn find_aot_custom_section(
    p: *const u8,
    n: usize,
    name: *const u8,
    name_len: usize,
    sec_start: *mut usize,
    sec_len: *mut usize,
) -> bool {
    if n < 8 {
        return false;
    }
    unsafe {
        if *p != 0 || *p.add(1) != b'a' || *p.add(2) != b'o' || *p.add(3) != b't' {
            return false;
        }
    }
    let mut at = 8usize;
    while at + 8 <= n {
        let typ = read_u32_le(p, at, n);
        let size = read_u32_le(p, at + 4, n) as usize;
        let content_off = at + 8;
        if content_off + size > n || size > 0x1000_0000 {
            return false;
        }
        if typ == 100 && size >= 6 {
            let sub = read_u32_le(p, content_off, n);
            if sub == 0 {
                let (raw, ok) = read_u16_le(p, content_off + 4, n);
                if ok {
                    let slen = (raw & 0xffff) as usize;
                    if 6 + slen <= size {
                        let nb = content_off + 6;
                        let mut blen = slen;
                        if blen > 0 && unsafe { *p.add(nb + blen - 1) } == 0 {
                            blen -= 1;
                        }
                        if unsafe { span_eq(p.add(nb), blen, name, name_len) } {
                            unsafe {
                                *sec_start = nb + slen;
                                *sec_len = content_off + size - (nb + slen);
                            }
                            return true;
                        }
                    }
                }
            }
        }
        at = (content_off + size + 3) & !3;
    }
    false
}

fn find_custom_section(
    p: *const u8,
    n: usize,
    name: *const u8,
    name_len: usize,
    sec_start: *mut usize,
    sec_len: *mut usize,
) -> bool {
    if find_wasm_custom_section(p, n, name, name_len, sec_start, sec_len) {
        return true;
    }
    find_aot_custom_section(p, n, name, name_len, sec_start, sec_len)
}

/// MPPK payload → (start, len) of the version string, if shaped right.
fn parse_mppk(p: *const u8, n: usize, out_start: *mut usize, out_len: *mut usize) -> bool {
    if n < 8 || !span_eq(p, 4, b"MPPK".as_ptr(), 4) {
        return false;
    }
    let (fmt, ok) = read_u16_le(p, 4, n);
    if !ok || fmt != 1 {
        return false;
    }
    let (nlen, ok2) = read_u16_le(p, 6, n);
    if !ok2 {
        return false;
    }
    let nl = (nlen & 0xffff) as usize;
    if 8 + nl > n || nl == 0 {
        return false;
    }
    unsafe {
        *out_start = 8;
        *out_len = nl;
    }
    true
}

/// MPSR payload → (start, len) of the pkg version field.
fn parse_mpsr_pkg_version(p: *const u8, n: usize, out_start: *mut usize, out_len: *mut usize) -> bool {
    if n < 14 || !span_eq(p, 4, b"MPSR".as_ptr(), 4) {
        return false;
    }
    let (name_len, ok) = read_u16_le(p, 8, n);
    if !ok {
        return false;
    }
    let nl = (name_len & 0xffff) as usize;
    if 10 + nl + 2 > n {
        return false;
    }
    let (pv, ok2) = read_u16_le(p, 10 + nl, n);
    if !ok2 {
        return false;
    }
    let pv_len = (pv & 0xffff) as usize;
    if 2 + pv_len > n - (10 + nl) || pv_len == 0 {
        return false;
    }
    unsafe {
        *out_start = 10 + nl + 2;
        *out_len = pv_len;
    }
    true
}

/// Whole-buffer (start, len) of the package version, from wasmmod.pkg
/// (MPPK) or the wasmmod.source (MPSR) fallback.
fn extract_pkg_version(
    p: *const u8,
    n: usize,
    out_start: *mut usize,
    out_len: *mut usize,
) -> bool {
    let mut sec_start = 0usize;
    let mut sec_len = 0usize;
    if find_custom_section(p, n, b"wasmmod.pkg".as_ptr(), 11, &mut sec_start, &mut sec_len)
        && unsafe { parse_mppk(p.add(sec_start), sec_len, out_start, out_len) }
    {
        /* out offsets are relative to the section payload — make them
         * whole-buffer */
        unsafe {
            *out_start += sec_start;
        }
        return true;
    }
    if find_custom_section(p, n, b"wasmmod.source".as_ptr(), 14, &mut sec_start, &mut sec_len)
        && unsafe { parse_mpsr_pkg_version(p.add(sec_start), sec_len, out_start, out_len) }
    {
        unsafe {
            *out_start += sec_start;
        }
        return true;
    }
    false
}

/* One entry from ``wasmmod.tests`` (MPTE). Fixed rows: the payload
 * carries a module name (advisory), a case name and a wasm export. */
const TEST_MAX: usize = 32;
const TNAME_CAP: usize = 64;

#[derive(Clone, Copy)]
struct PackTest {
    name: [u8; TNAME_CAP],
    name_len: u32,
    export: [u8; TNAME_CAP],
    export_len: u32,
}

fn parse_mpte(p: *const u8, n: usize, out: *mut [PackTest; TEST_MAX]) -> usize {
    if n < 10 || !span_eq(p, 4, b"MPTE".as_ptr(), 4) {
        return 0;
    }
    let (ver, ok) = read_u16_le(p, 4, n);
    if !ok || ver != 1 {
        return 0;
    }
    let count = read_u32_le(p, 6, n) as usize;
    if count > TEST_MAX {
        return 0;
    }
    let mut i = 10usize;
    let mut out_n = 0usize;
    let mut t = 0usize;
    while t < count {
        t += 1;
        // module (advisory; load fqn wins at register time)
        let (ml, ok_ml) = read_u16_le(p, i, n);
        if !ok_ml {
            return 0;
        }
        i += 2 + (ml & 0xffff) as usize;
        let (nl_raw, ok_nl) = read_u16_le(p, i, n);
        if !ok_nl {
            return 0;
        }
        let nl = (nl_raw & 0xffff) as usize;
        if nl == 0 {
            return 0;
        }
        i += 2;
        if i + nl > n || nl >= TNAME_CAP {
            return 0;
        }
        unsafe {
            ptr::copy_nonoverlapping(p.add(i), (*out)[out_n].name.as_mut_ptr(), nl);
            (*out)[out_n].name_len = nl as u32;
        }
        i += nl;
        let (el_raw, ok_el) = read_u16_le(p, i, n);
        if !ok_el {
            return 0;
        }
        let el = (el_raw & 0xffff) as usize;
        if el == 0 {
            return 0;
        }
        i += 2;
        if i + el > n || el >= TNAME_CAP {
            return 0;
        }
        unsafe {
            ptr::copy_nonoverlapping(p.add(i), (*out)[out_n].export.as_mut_ptr(), el);
            (*out)[out_n].export_len = el as u32;
        }
        i += el;
        out_n += 1;
    }
    out_n
}

/// C/ELF path: bake version from artifact bytes onto an already-published fqn.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_bake_pkg_version(
    fqn_ptr: *const u8,
    fqn_len: u32,
    bytes_ptr: *const u8,
    bytes_len: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    if bytes_ptr.is_null() || bytes_len == 0 {
        return 0;
    }
    let mut ver_start = 0usize;
    let mut ver_len = 0usize;
    if !extract_pkg_version(
        bytes_ptr,
        bytes_len as usize,
        &mut ver_start,
        &mut ver_len,
    ) {
        return 0;
    }
    unsafe {
        pm_wasmmod_registry_set_version(
            fqn_ptr,
            fqn_len,
            bytes_ptr.add(ver_start),
            ver_len as u32,
        )
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_load(
    fqn_ptr: *const u8,
    fqn_len: u32,
    bytes_ptr: *const u8,
    bytes_len: u32,
) -> pm_wasmmod_registry_handle_t {
    if fqn_ptr.is_null() || fqn_len == 0 || bytes_ptr.is_null() {
        return invalid_handle();
    }
    if !unsafe { image_room(bytes_len) } {
        /* Loud refusal, never a silent truncate — WAMR mutates the buffer it
         * is handed, so the loader owns one full copy, and a seat says how
         * large a copy it will take (the image knob). */
        return invalid_handle();
    }
    if fqn_len as usize > FQN_CAP {
        return invalid_handle();
    }
    let heap = unsafe { shared_heap() };
    if heap.is_null() {
        // pm_wasmmod_loader_init() must run first — see the plan's
        // loader/registry split, "loader owns WAMR instantiation".
        return invalid_handle();
    }

    unsafe {
        /* One lock spans the whole load: the registry's own table lock
         * and the adapter pool's lock are never taken while holding
         * this one elsewhere, so the order cannot invert. */
        pm_util_lock_acquire(loaded_lock());
        let table = loaded_table();
        let mut free: usize = MOD_MAX;
        let mut i: usize = 0;
        while i < MOD_MAX {
            if !(*table.add(i)).live {
                free = i;
                break;
            }
            i += 1;
        }
        let idx: usize = free;
        if idx == MOD_MAX {
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }
        let m = table.add(idx);
        /* One allocation of exactly this image's length, owned by the row for
         * the module's lifetime (WAMR may mutate the buffer in place and
         * keeps referencing it until unload). */
        let image = wasm_runtime_malloc(bytes_len) as *mut u8;
        if image.is_null() {
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }
        ptr::copy_nonoverlapping(bytes_ptr, image, bytes_len as usize);
        (*m).bytes = image;
        (*m).bytes_len = bytes_len;

        // Real detection, not a hardcoded guess: `.aot` and `.wasm` are
        // told apart from the bytes themselves (magic-number sniff inside
        // WAMR), before `wasm_runtime_load` — the same call handles both
        // container kinds internally, so nothing else about the load path
        // below needs to branch on this; only the registry publish call
        // does. Anything WAMR doesn't recognize falls back to Wasm rather
        // than a new failure mode — wasm_runtime_load's own validation is
        // still the real gatekeeper for genuinely malformed input.
        let container = if wasm_runtime_get_file_package_type(
            (*m).bytes,
            (*m).bytes_len,
        ) == WASM_MODULE_AOT
        {
            pm_wasmmod_registry_container_kind_t::Aot
        } else {
            pm_wasmmod_registry_container_kind_t::Wasm
        };

        let mut error_buf: [u8; 256] = [0; 256];
        let module = wasm_runtime_load(
            (*m).bytes,
            (*m).bytes_len,
            error_buf.as_mut_ptr() as *mut i8,
            error_buf.len() as u32,
        );
        if module.is_null() {
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }

        let instance = wasm_runtime_instantiate(
            module,
            DEFAULT_STACK_SIZE,
            DEFAULT_HEAP_SIZE,
            error_buf.as_mut_ptr() as *mut i8,
            error_buf.len() as u32,
        );
        if instance.is_null() {
            teardown(module, ptr::null_mut(), ptr::null_mut(), ptr::null(), 0);
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }

        if !wasm_runtime_attach_shared_heap(instance, heap) {
            teardown(module, instance, ptr::null_mut(), ptr::null(), 0);
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }

        let exec_env = wasm_runtime_create_exec_env(instance, DEFAULT_STACK_SIZE);
        if exec_env.is_null() {
            teardown(module, instance, ptr::null_mut(), ptr::null(), 0);
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }

        let handle = pm_wasmmod_registry_publish(fqn_ptr, fqn_len, container);
        if handle.index == INVALID_HANDLE_INDEX {
            teardown(module, instance, exec_env, ptr::null(), 0);
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }
        // Bake package version from wasmmod.pkg (or source fallback) into the registry.
        let mut ver_start = 0usize;
        let mut ver_len = 0usize;
        if extract_pkg_version(
            (*m).bytes,
            (*m).bytes_len as usize,
            &mut ver_start,
            &mut ver_len,
        ) {
            let _ = pm_wasmmod_registry_set_version(
                fqn_ptr,
                fqn_len,
                (*m).bytes.add(ver_start),
                ver_len as u32,
            );
        }

        let mut pack_tests: [PackTest; TEST_MAX] = [PackTest {
            name: [0; TNAME_CAP],
            name_len: 0,
            export: [0; TNAME_CAP],
            export_len: 0,
        }; TEST_MAX];
        let mut n_tests = 0usize;
        {
            let mut sec_start = 0usize;
            let mut sec_len = 0usize;
            if find_custom_section(
                (*m).bytes,
                (*m).bytes_len as usize,
                b"wasmmod.tests".as_ptr(),
                13,
                &mut sec_start,
                &mut sec_len,
            ) {
                n_tests = parse_mpte(
                    (*m).bytes.add(sec_start),
                    sec_len,
                    &mut pack_tests,
                );
            }
        }

        let mut claimed_slots: [usize; MAX_ADAPTER_SLOTS] = [0; MAX_ADAPTER_SLOTS];
        let mut n_claimed = 0u32;
        let mut tests_hit: usize = 0;
        let export_count = wasm_runtime_get_export_count(module);
        let mut load_failed = false;
        let mut export_index: i32 = 0;
        while export_index < export_count {
            let mut export = wasm_export_t {
                name: ptr::null(),
                kind: 0,
                reserved: ptr::null_mut(),
            };
            wasm_runtime_get_export_type(module, export_index, &mut export);
            if export.kind == WASM_IMPORT_EXPORT_KIND_FUNC && !export.name.is_null() {
                let func = wasm_runtime_lookup_function(instance, export.name);
                if !func.is_null() {
                    // shouldn't fail for a real func export, but never worth aborting the whole load over

                    /* Pack tests are not product faces — register export
                     * name only; registry runs them via
                     * loader_run_wasm_test (no trampoline pool). */
                    let mut name_len: usize = 0;
                    let q = export.name;
                    while *q.add(name_len) != 0 && name_len < 256 {
                        name_len += 1;
                    }
                    let mut matched: i64 = -1;
                    let mut t = 0usize;
                    while t < n_tests {
                        if pack_tests[t].export_len as usize == name_len
                            && span_eq(
                                export.name as *const u8,
                                name_len,
                                pack_tests[t].export.as_ptr(),
                                pack_tests[t].export_len as usize,
                            )
                        {
                            matched = t as i64;
                            break;
                        }
                        t += 1;
                    }
                    if matched >= 0 {
                        let t = matched as usize;
                        let ok = pm_wasmmod_registry_test_register_wasm(
                            fqn_ptr,
                            fqn_len,
                            pack_tests[t].name.as_ptr(),
                            pack_tests[t].name_len,
                            pack_tests[t].export.as_ptr(),
                            pack_tests[t].export_len,
                        );
                        if ok == 0 {
                            load_failed = true;
                        } else {
                            tests_hit += 1;
                        }
                    } else {
                        let slot_index = claim_adapter_slot(instance, exec_env, func);
                        if slot_index == MAX_ADAPTER_SLOTS {
                            // pool exhausted — roll the whole load back rather than publish a partial export set
                            load_failed = true;
                        } else {
                            claimed_slots[n_claimed as usize] = slot_index;
                            n_claimed += 1;

                            let ok = pm_wasmmod_registry_export_set(
                                handle,
                                export.name as *const u8,
                                name_len as u32,
                                pm_wasmmod_registry_export_kind_t::Fn,
                                adapter_fn_at(slot_index),
                            );
                            if ok == 0 {
                                load_failed = true;
                            }
                        }
                    }
                }
            }
            if load_failed {
                export_index = export_count;
            } else {
                export_index += 1;
            }
        }

        if !load_failed && tests_hit != n_tests {
            // Section listed an export that is missing from the module.
            load_failed = true;
        }

        if load_failed {
            pm_wasmmod_registry_unpublish(handle);
            teardown(module, instance, exec_env, claimed_slots.as_ptr(), n_claimed);
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }

        let mut stored = false;
        {
            let idx2 = handle.index as usize;
            if idx2 < MOD_MAX {
                let row = table.add(idx2);
                ptr::copy_nonoverlapping(fqn_ptr, (*row).fqn.as_mut_ptr(), fqn_len as usize);
                (*row).fqn_len = fqn_len;
                (*row).generation = handle.generation;
                (*row).module = module;
                (*row).instance = instance;
                (*row).exec_env = exec_env;
                let image_len = (*m).bytes_len;
                if idx2 != idx {
                    /* The image moves by handing over the allocation: it must
                     * not be copied anywhere, because WAMR is already holding
                     * pointers into it (copying it is what the fixed arrays
                     * used to do, leaving the live buffer in a row that read
                     * as free). */
                    (*row).bytes = (*m).bytes;
                    (*m).bytes = ptr::null_mut();
                    (*m).bytes_len = 0;
                }
                (*row).bytes_len = image_len;
                ptr::copy_nonoverlapping(
                    claimed_slots.as_ptr(),
                    (*row).slots.as_mut_ptr(),
                    MAX_ADAPTER_SLOTS,
                );
                (*row).n_slots = n_claimed;
                (*row).live = true;
                stored = true;
            }
        }
        if !stored {
            pm_wasmmod_registry_unpublish(handle);
            teardown(module, instance, exec_env, claimed_slots.as_ptr(), n_claimed);
            image_release(m);
            pm_util_lock_release(loaded_lock());
            return invalid_handle();
        }

        pm_util_lock_release(loaded_lock());
        handle
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_unload(handle: pm_wasmmod_registry_handle_t) -> i32 {
    let idx = handle.index as usize;
    if idx >= MOD_MAX {
        return -1;
    }
    let mut taken = false;
    let mut module: wasm_module_t = ptr::null_mut();
    let mut instance: wasm_module_inst_t = ptr::null_mut();
    let mut exec_env: wasm_exec_env_t = ptr::null_mut();
    let mut slots: [usize; MAX_ADAPTER_SLOTS] = [0; MAX_ADAPTER_SLOTS];
    let mut n_slots: u32 = 0;
    /* The image outlives this block: WAMR is still holding pointers into it
     * until teardown unloads the module, so the row lets go of it here and it
     * is given back below, after. */
    let mut image: *mut u8 = ptr::null_mut();
    let mut image_len: u32 = 0;
    unsafe {
        pm_util_lock_acquire(loaded_lock());
        let table = loaded_table();
        let m = table.add(idx);
        if (*m).live && (*m).generation == handle.generation {
            module = (*m).module;
            instance = (*m).instance;
            exec_env = (*m).exec_env;
            ptr::copy_nonoverlapping((*m).slots.as_ptr(), slots.as_mut_ptr(), MAX_ADAPTER_SLOTS);
            n_slots = (*m).n_slots;
            image = (*m).bytes;
            image_len = (*m).bytes_len;
            (*m).bytes = ptr::null_mut();
            (*m).bytes_len = 0;
            (*m).live = false;
            taken = true;
        }
        pm_util_lock_release(loaded_lock());
    }
    if !taken {
        return -1;
    }

    if unsafe { pm_wasmmod_registry_unpublish(handle) } == 0 {
        // Handle was already stale at the registry's own layer — put
        // our record back rather than silently drop it out from under
        // whatever still thinks it owns this slot.
        unsafe {
            pm_util_lock_acquire(loaded_lock());
            let table = loaded_table();
            let m = table.add(idx);
            (*m).bytes = image;
            (*m).bytes_len = image_len;
            (*m).live = true;
            pm_util_lock_release(loaded_lock());
        }
        return -1;
    }

    teardown(module, instance, exec_env, slots.as_ptr(), n_slots);
    /* WAMR has let go of the buffer now. */
    if !image.is_null() {
        unsafe { wasm_runtime_free(image as *mut c_void) };
    }
    0
}

unsafe fn loaded_instance(handle: pm_wasmmod_registry_handle_t) -> wasm_module_inst_t {
    let idx = handle.index as usize;
    if idx >= MOD_MAX {
        return ptr::null_mut();
    }
    unsafe {
        pm_util_lock_acquire(loaded_lock());
        let table = loaded_table();
        let m = table.add(idx);
        let inst = if (*m).live && (*m).generation == handle.generation {
            (*m).instance
        } else {
            ptr::null_mut()
        };
        pm_util_lock_release(loaded_lock());
        inst
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_addr_to_native(
    handle: pm_wasmmod_registry_handle_t,
    addr: pm_addr_t,
) -> *mut c_void {
    if addr.space == PM_ADDR_SPACE_NATIVE {
        let wide: usize = addr.off as usize;
        return wide as *mut c_void;
    }
    let instance = unsafe { loaded_instance(handle) };
    if instance.is_null() {
        return ptr::null_mut();
    }
    unsafe { wasm_runtime_addr_app_to_native(instance, addr.off) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_loader_native_to_addr(
    handle: pm_wasmmod_registry_handle_t,
    space: u32,
    native: *const c_void,
) -> pm_addr_t {
    if space == PM_ADDR_SPACE_NATIVE {
        let native_wide: usize = native as usize;
        let native_off: u64 = native_wide as u64;
        return pm_addr_t {
            space: PM_ADDR_SPACE_NATIVE,
            off: native_off,
        };
    }
    let instance = unsafe { loaded_instance(handle) };
    if instance.is_null() {
        return pm_addr_t {
            space: PM_ADDR_SPACE_NATIVE,
            off: 0,
        };
    }
    let app = unsafe { wasm_runtime_addr_native_to_app(instance, native as *mut c_void) };
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
    let instance = unsafe { loaded_instance(handle) };
    if instance.is_null() {
        return -1;
    }
    unsafe {
        let mut native: *mut c_void = ptr::null_mut();
        let app = wasm_runtime_shared_heap_malloc(instance, len as u64, &mut native);
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
    let instance = unsafe { loaded_instance(handle) };
    if instance.is_null() {
        return -1;
    }
    unsafe { wasm_runtime_shared_heap_free(instance, addr.off) };
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
