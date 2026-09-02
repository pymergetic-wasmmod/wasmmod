//! pymergetic.wasmmod.registry — the one source of truth for native
//! module identity + exports. `sys.modules` stays the Python import
//! face; this table is what backs every native lookup underneath it,
//! for every impl language and every call direction. See
//! docs/REGISTRY.md for the design this implements.
//!
//! Subset discipline (this file compiles through the kernel's own rsx
//! chain — ksweep), same posture as net.search / util.lock: ptr+len
//! byte spans instead of `String`, fixed-capacity arrays with explicit
//! counts instead of `Vec`, one raw lock primitive instead of the
//! generic `Mutex<T>` wrapper. No allocations anywhere, which also
//! removes the old two-phase staging story: a registration arriving
//! from a constructor lands directly in the table, whether or not any
//! heap exists yet.
//!
//! Budgets (from the tree's own registrations, with headroom — see
//! REGISTRY.md "Storage" for the fixed-capacity trade-off):
//!   MOD_MAX modules, EXP_MAX exports per module, TST_MAX tests,
//!   BNC_MAX benches; FQN_CAP/VER_CAP/SIG_CAP/NAME_CAP bytes per row.
//! An overflow is a loud refusal (0 / invalid handle), never a silent
//! drop.

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use core::cell::UnsafeCell;
use core::ffi::c_void;
use core::ptr;

// The shared ABI shapes live in __types__.rs (the Rust twin of
// `__types__.h`). The path-include is the same one cargo resolves — and
// the one the build card's in-kernel rsx compile splices in — so there
// is one definition, reached the same way from both compilers.
#[path = "__types__.rs"]
mod __types__;
pub use __types__::*;

/* pymergetic.util.lock — the project's one lock card (RS muscle, C ABI),
 * declared the same way net.search declares it: raw primitive, caller
 * payload. The lock card is impl = rs, so this is not a second lock, it
 * is the same one, reached through its C face. */
#[repr(C)]
struct pm_util_lock_t {
    locked: u32,
}

unsafe extern "C" {
    fn pm_util_lock_acquire(l: *mut pm_util_lock_t);
    fn pm_util_lock_release(l: *mut pm_util_lock_t);
}

/* ---- budgets ---- */

const MOD_MAX: usize = 128;
const EXP_MAX: usize = 64;
const TST_MAX: usize = 32;
const BNC_MAX: usize = 8;
const FQN_CAP: usize = 64;
const VER_CAP: usize = 32;
const SIG_CAP: usize = 160;
const NAME_CAP: usize = 64;
const OUT_CAP: usize = 4096;

/* The sentinel handle: index = u32::MAX. A const struct literal is not an
 * accepted rsx static-initializer shape (the parser wraps struct lits in a
 * PATH), so the sentinel is a builder fn — function bodies lower struct
 * literals fine. */
fn invalid_handle() -> pm_wasmmod_registry_handle_t {
    pm_wasmmod_registry_handle_t {
        index: 4294967295u32,
        generation: 0,
    }
}

/// Module-local test case (`__tests__.*` / `PM_MOD_TEST_*`). Not an export —
/// never emitted by util.gen faces.
pub type pm_wasmmod_registry_test_fn_t = unsafe extern "C" fn() -> i32;

/// Loader hook: run a guest pack test by wasm export name (no trampoline pool).
pub type pm_wasmmod_registry_wasm_test_runner_t =
    unsafe extern "C" fn(*const u8, u32, *const u8, u32) -> i32;

/// Loader hook: run a guest pack bench by wasm export name + the ops to do.
/// Carries the `iterations` word that `pm_wasmmod_registry_bench_fn_t` takes,
/// so the same missing-call triage (native vs guest) applies to benches.
pub type pm_wasmmod_registry_wasm_bench_runner_t =
    unsafe extern "C" fn(*const u8, u32, *const u8, u32, u64) -> i32;

/// Module-local benchmark (`__bench__.*` / `PM_MOD_BENCH_*`). Not an export —
/// never emitted by util.gen faces, and never a pass/fail gate.
///
/// A bench is a `fn(iterations) -> i32` that runs **one iteration** of the
/// unit of work. The registry owns the timing: it warmups, measures
/// `iterations` repeats, and reports ns/op (or us/op). `0` means "ran ok" —
/// informational, not a failure count.
pub type pm_wasmmod_registry_bench_fn_t = unsafe extern "C" fn(u64) -> i32;

/// Per-seat monotonic clock fill for bench timing, in microseconds. `0` is
/// meaningless as a delta, so a seat with no clock simply never registers
/// one — `bench_run*` then report "no clock" instead of measuring garbage.
/// Metal provides a strong fill reaching `pm_metal_async_mono_us`; each seat
/// that can time provides its own (POSIX `clock_gettime` on unix, a
/// `performance.now`-derived value on emcc, the firmware timer otherwise).
pub type pm_wasmmod_registry_bench_clock_t = unsafe extern "C" fn() -> u64;

/* A test/bench body is either a native fn or a wasm export name — a
 * data-carrying enum would be a sum type, which rsx refuses; the
 * plain-data equivalent is a kind tag + the union payload spelled as
 * fields (null fn = WasmExport). */
const BODY_NATIVE: u8 = 0;
const BODY_WASM: u8 = 1;

/* Clone/Copy: rows repeat inside `[row; N]` initializers (the rsx subset
 * has no Default, and a bare struct-lit const is not an accepted static
 * shape — asgi's Conn rows use the same derive). */
#[derive(Clone, Copy)]
struct Export {
    name: [u8; NAME_CAP],
    name_len: u32,
    kind: pm_wasmmod_registry_export_kind_t,
    /// `Fn`/`Mem`/`I64`/`F32`/`F64`/`BufPtr`: a real native pointer.
    /// `Obj`: an opaque token owned by whichever embedder eventually
    /// attaches a GC (see `gc_visit`) — the registry never interprets
    /// it, only stores and hands it back.
    ptr: *mut c_void,
    /// Optional C prototype spelling for facegen (e.g. `int(void)`).
    /// len 0 = unset.
    sig: [u8; SIG_CAP],
    sig_len: u32,
}

#[derive(Clone, Copy)]
struct TestEntry {
    name: [u8; NAME_CAP],
    name_len: u32,
    body_kind: u8,
    native: Option<pm_wasmmod_registry_test_fn_t>,
    export: [u8; NAME_CAP],
    export_len: u32,
}

#[derive(Clone, Copy)]
struct BenchEntry {
    name: [u8; NAME_CAP],
    name_len: u32,
    body_kind: u8,
    native: Option<pm_wasmmod_registry_bench_fn_t>,
    export: [u8; NAME_CAP],
    export_len: u32,
}

#[derive(Clone, Copy)]
struct ModEntry {
    fqn: [u8; FQN_CAP],
    fqn_len: u32,
    /// Package / kernel version string (empty = unset). Build-root packs and
    /// host-intrinsic modules that are independently dependable.
    version: [u8; VER_CAP],
    version_len: u32,
    container: pm_wasmmod_registry_container_kind_t,
    generation: u32,
    live: bool,
    exports: [Export; EXP_MAX],
    n_exports: u32,
    tests: [TestEntry; TST_MAX],
    n_tests: u32,
    benches: [BenchEntry; BNC_MAX],
    n_benches: u32,
}

/// How many repeats to run before measuring — a cheap warmup that lets the
/// runner park/schedule before we start counting, the same spirit as the
/// DHCP/SSH "drive the wire" loops.
const BENCH_MEASURE_WARMUP: u64 = 2;

/* Transparent static discipline (same as net.search): Mut<T> is a
 * one-field tuple struct rsx maps to its inner type, UnsafeCell::get
 * hands out the pointer. */
struct Mut<T>(UnsafeCell<T>);
unsafe impl<T> Sync for Mut<T> {}

static LOCK: Mut<pm_util_lock_t> = Mut(UnsafeCell::new(pm_util_lock_t { locked: 0 }));

struct Table {
    entries: [ModEntry; MOD_MAX],
    n_entries: u32,
    /* bench_run_all report buffer — filled under the lock, copied out
     * on release. Keeps the no-alloc discipline on the report path too. */
    out: [u8; OUT_CAP],
    out_len: u32,
}

/* The one table. Sized in .bss: 128 modules * (64 exports + 32 tests +
 * 8 benches) of fixed rows. Nothing here allocates, so the two-phase
 * staging buffer the allocator-backed design needed is gone entirely —
 * a ctor-time registration is just a row write.
 *
 * rsx shape discipline, two rules. (1) The static-initializer shape
 * check only accepts a *bare* STRUCT_LIT / ARRAY / LITERAL (or the
 * constant-lowering CALLs) at the top level — the parser wraps every
 * struct literal in a PATH, so a struct-lit `const` is always refused;
 * the zero rows therefore live only *inside* TABLE's initializer (a
 * `Mut(UnsafeCell::new(..))` CALL, whose argument is not shape-checked),
 * spelled as `[row; N]` repeat arrays. (2) CONST items lower in pass 0a
 * — before the struct pass — so a const naming `Export` would emit ahead
 * of the typedef; the inline spellings keep every type reference after
 * its struct.
 *
 * Every field here is the zero value, deliberately: rsx proves the whole
 * initializer folds to zero and emits the declaration bare, so the table
 * rides .bss instead of a multi-megabyte explicit zero blob through the
 * in-kernel object compiler. Dead rows therefore spell `container` as
 * `Wasm` (variant 0) — the kind is meaningless until publish() overwrites
 * it, and readers only ever see live rows. */
static TABLE: Mut<Table> = Mut(UnsafeCell::new(Table {
    entries: [ModEntry {
        fqn: [0; FQN_CAP],
        fqn_len: 0,
        version: [0; VER_CAP],
        version_len: 0,
        container: pm_wasmmod_registry_container_kind_t::Wasm,
        generation: 0,
        live: false,
        exports: [Export {
            name: [0; NAME_CAP],
            name_len: 0,
            kind: pm_wasmmod_registry_export_kind_t::Fn,
            ptr: ptr::null_mut(),
            sig: [0; SIG_CAP],
            sig_len: 0,
        }; EXP_MAX],
        n_exports: 0,
        tests: [TestEntry {
            name: [0; NAME_CAP],
            name_len: 0,
            body_kind: BODY_NATIVE,
            native: None,
            export: [0; NAME_CAP],
            export_len: 0,
        }; TST_MAX],
        n_tests: 0,
        benches: [BenchEntry {
            name: [0; NAME_CAP],
            name_len: 0,
            body_kind: BODY_NATIVE,
            native: None,
            export: [0; NAME_CAP],
            export_len: 0,
        }; BNC_MAX],
        n_benches: 0,
    }; MOD_MAX],
    n_entries: 0,
    out: [0; OUT_CAP],
    out_len: 0,
}));

/* The loader hooks are single fn-pointer cells: one lock-free spin for
 * the cell itself would be a second lock discipline, so they share the
 * table lock — installers and readers are both rare. */
static WASM_TEST_RUNNER: Mut<Option<pm_wasmmod_registry_wasm_test_runner_t>> =
    Mut(UnsafeCell::new(None));
static WASM_BENCH_RUNNER: Mut<Option<pm_wasmmod_registry_wasm_bench_runner_t>> =
    Mut(UnsafeCell::new(None));
static BENCH_CLOCK: Mut<Option<pm_wasmmod_registry_bench_clock_t>> = Mut(UnsafeCell::new(None));

unsafe fn table() -> *mut Table {
    TABLE.0.get()
}

unsafe fn lock() -> *mut pm_util_lock_t {
    LOCK.0.get()
}

/* ---- span helpers (ptr+len discipline; no &str anywhere) ---- */

unsafe fn span_eq(a: *const u8, a_len: usize, b: *const u8, b_len: usize) -> bool {
    if a_len != b_len {
        return false;
    }
    let mut i = 0usize;
    while i < a_len {
        unsafe {
            if *a.add(i) != *b.add(i) {
                return false;
            }
        }
        i += 1;
    }
    true
}

/// Copy a span into a fixed row buffer; returns the stored length or
/// SIZE_MAX on overflow (caller refuses loudly, never truncates a name
/// into a key that would then compare equal to a prefix).
unsafe fn span_put(dst: *mut u8, dst_cap: usize, src: *const u8, src_len: usize) -> usize {
    if src_len > dst_cap {
        return usize::MAX;
    }
    let mut i = 0usize;
    while i < src_len {
        unsafe {
            *dst.add(i) = *src.add(i);
        }
        i += 1;
    }
    src_len
}

/// ptr/len are already validated non-null by the callers; the registry
/// never interprets the bytes (names are compared as bytes, not UTF-8
/// text), so no utf8 validation gate on the hot path.
unsafe fn raw_ok(p: *const u8, len: u32) -> bool {
    !p.is_null() || len == 0
}

/* ---- table ops (all under the caller-held lock) ---- */

/// First dead slot, or -1 — publish reuses holes so every other live
/// handle's index keeps meaning the same thing (see REGISTRY.md).
unsafe fn find_dead(t: *mut Table) -> i32 {
    let mut i = 0usize;
    while i < (*t).n_entries as usize {
        if !(*t).entries[i].live {
            return i as i32;
        }
        i += 1;
    }
    if (*t).n_entries as usize >= MOD_MAX {
        return -1;
    }
    let idx = (*t).n_entries as usize;
    (*t).n_entries += 1;
    idx as i32
}

unsafe fn entry_by_handle(
    t: *mut Table,
    handle: pm_wasmmod_registry_handle_t,
) -> *mut ModEntry {
    let i = handle.index as usize;
    if i >= (*t).n_entries as usize || i >= MOD_MAX {
        return core::ptr::null_mut();
    }
    let e = (*t).entries.as_mut_ptr().add(i);
    if !(*e).live || (*e).generation != handle.generation {
        return core::ptr::null_mut();
    }
    e
}

unsafe fn entry_by_fqn(t: *mut Table, fqn: *const u8, fqn_len: usize) -> *mut ModEntry {
    let mut i = 0usize;
    while i < (*t).n_entries as usize {
        let e = (*t).entries.as_mut_ptr().add(i);
        if (*e).live && (*e).fqn_len as usize == fqn_len {
            let eq = unsafe {
                span_eq((*e).fqn.as_ptr(), (*e).fqn_len as usize, fqn, fqn_len)
            };
            if eq {
                return e;
            }
        }
        i += 1;
    }
    core::ptr::null_mut()
}

unsafe fn entry_index(e: *mut ModEntry, t: *mut Table) -> u32 {
    let base = (*t).entries.as_mut_ptr();
    if base.is_null() || e.is_null() {
        return u32::MAX;
    }
    let off = (e as usize) - (base as usize);
    (off / core::mem::size_of::<ModEntry>()) as u32
}

/// Publish (new slot or dead-slot reuse with a generation bump).
/// Returns the handle or the invalid sentinel when the table is full.
unsafe fn publish(
    t: *mut Table,
    fqn: *const u8,
    fqn_len: usize,
    container: pm_wasmmod_registry_container_kind_t,
) -> pm_wasmmod_registry_handle_t {
    let idx = unsafe { find_dead(t) };
    if idx < 0 {
        return invalid_handle();
    }
    let e = (*t).entries.as_mut_ptr().add(idx as usize);
    let stored = unsafe { span_put((*e).fqn.as_mut_ptr(), FQN_CAP, fqn, fqn_len) };
    if stored == usize::MAX {
        return invalid_handle();
    }
    /* A reused slot bumps its generation so every stale handle to the
     * dead occupant keeps being detectably stale (see REGISTRY.md). */
    (*e).generation = (*e).generation.wrapping_add(1);
    (*e).fqn_len = stored as u32;
    (*e).version_len = 0;
    (*e).container = container;
    (*e).live = true;
    (*e).n_exports = 0;
    (*e).n_tests = 0;
    (*e).n_benches = 0;
    pm_wasmmod_registry_handle_t {
        index: idx as u32,
        generation: (*e).generation,
    }
}

/// Publish if missing; if already live, keep the existing container.
unsafe fn ensure(
    t: *mut Table,
    fqn: *const u8,
    fqn_len: usize,
    container: pm_wasmmod_registry_container_kind_t,
) -> pm_wasmmod_registry_handle_t {
    let e = unsafe { entry_by_fqn(t, fqn, fqn_len) };
    if !e.is_null() {
        let idx = unsafe { entry_index(e, t) };
        return pm_wasmmod_registry_handle_t {
            index: idx,
            generation: (*e).generation,
        };
    }
    unsafe { publish(t, fqn, fqn_len, container) }
}

unsafe fn export_set_sig(
    t: *mut Table,
    handle: pm_wasmmod_registry_handle_t,
    name: *const u8,
    name_len: usize,
    kind: pm_wasmmod_registry_export_kind_t,
    ptr: *mut c_void,
    sig: *const u8,
    sig_len: usize,
) -> bool {
    let e = unsafe { entry_by_handle(t, handle) };
    if e.is_null() {
        return false;
    }
    if name_len > NAME_CAP || sig_len > SIG_CAP {
        return false;
    }
    let mut i = 0usize;
    while i < (*e).n_exports as usize {
        let x = (*e).exports.as_mut_ptr().add(i);
        if (*x).name_len as usize == name_len {
            let eq = unsafe { span_eq((*x).name.as_ptr(), name_len, name, name_len) };
            if eq {
                (*x).kind = kind;
                (*x).ptr = ptr;
                if sig_len > 0 {
                    let _ = unsafe { span_put((*x).sig.as_mut_ptr(), SIG_CAP, sig, sig_len) };
                    (*x).sig_len = sig_len as u32;
                }
                return true;
            }
        }
        i += 1;
    }
    if (*e).n_exports as usize >= EXP_MAX {
        return false;
    }
    let x = (*e).exports.as_mut_ptr().add((*e).n_exports as usize);
    let stored = unsafe { span_put((*x).name.as_mut_ptr(), NAME_CAP, name, name_len) };
    if stored == usize::MAX {
        return false;
    }
    (*x).name_len = stored as u32;
    (*x).kind = kind;
    (*x).ptr = ptr;
    (*x).sig_len = 0;
    if sig_len > 0 {
        let _ = unsafe { span_put((*x).sig.as_mut_ptr(), SIG_CAP, sig, sig_len) };
        (*x).sig_len = sig_len as u32;
    }
    (*e).n_exports += 1;
    true
}

unsafe fn resolve_native(
    t: *mut Table,
    fqn: *const u8,
    fqn_len: usize,
    name: *const u8,
    name_len: usize,
) -> *mut c_void {
    let e = unsafe { entry_by_fqn(t, fqn, fqn_len) };
    if e.is_null() {
        return core::ptr::null_mut();
    }
    let mut i = 0usize;
    while i < (*e).n_exports as usize {
        let x = (*e).exports.as_ptr().add(i);
        if (*x).name_len as usize == name_len {
            let eq = unsafe { span_eq((*x).name.as_ptr(), name_len, name, name_len) };
            if eq {
                return (*x).ptr;
            }
        }
        i += 1;
    }
    core::ptr::null_mut()
}

/* ---- C ABI face ---- */

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_init() {
    // Nothing to do: TABLE's static zero-init is the whole init — the
    // fixed-capacity design has no heap to bring up. Kept as an explicit
    // entry point so callers don't need to know that.
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_publish(
    fqn_ptr: *const u8,
    fqn_len: u32,
    container: pm_wasmmod_registry_container_kind_t,
) -> pm_wasmmod_registry_handle_t {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return invalid_handle();
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let h = publish(table(), fqn_ptr, fqn_len as usize, container);
        pm_util_lock_release(lock());
        h
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_unpublish(handle: pm_wasmmod_registry_handle_t) -> i32 {
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let e = entry_by_handle(t, handle);
        let ok = !e.is_null();
        if ok {
            (*e).live = false;
            (*e).n_exports = 0;
            (*e).n_tests = 0;
            (*e).n_benches = 0;
            (*e).fqn_len = 0;
            (*e).version_len = 0;
        }
        pm_util_lock_release(lock());
        ok as i32
    }
}

/// Container kind for a live module, or `-1` if the fqn is not published.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_container(
    fqn_ptr: *const u8,
    fqn_len: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return -1;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let k = if e.is_null() {
            -1
        } else {
            (*e).container as i32
        };
        pm_util_lock_release(lock());
        k
    }
}

/// Publish `fqn` with `container` if missing; no-op (keeps kind) if already live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_ensure(
    fqn_ptr: *const u8,
    fqn_len: u32,
    container: pm_wasmmod_registry_container_kind_t,
) -> pm_wasmmod_registry_handle_t {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return invalid_handle();
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let h = ensure(table(), fqn_ptr, fqn_len as usize, container);
        pm_util_lock_release(lock());
        h
    }
}

/// Publish with an optional version string (`ver_len == 0` → unset).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_publish_ver(
    fqn_ptr: *const u8,
    fqn_len: u32,
    container: pm_wasmmod_registry_container_kind_t,
    ver_ptr: *const u8,
    ver_len: u32,
) -> pm_wasmmod_registry_handle_t {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return invalid_handle();
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let h = publish(t, fqn_ptr, fqn_len as usize, container);
        if h.index != u32::MAX && ver_len > 0 && !ver_ptr.is_null() {
            let e = entry_by_handle(t, h);
            if !e.is_null() && ver_len as usize <= VER_CAP {
                let _ = span_put((*e).version.as_mut_ptr(), VER_CAP, ver_ptr, ver_len as usize);
                (*e).version_len = ver_len;
            }
        }
        pm_util_lock_release(lock());
        h
    }
}

/// Set / replace the version on a live module. Returns 1 on success, 0 if missing.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_version(
    fqn_ptr: *const u8,
    fqn_len: u32,
    ver_ptr: *const u8,
    ver_len: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || ver_ptr.is_null() || ver_len == 0 {
        return 0;
    }
    if ver_len as usize > VER_CAP {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let ok = !e.is_null();
        if ok {
            let _ = span_put((*e).version.as_mut_ptr(), VER_CAP, ver_ptr, ver_len as usize);
            (*e).version_len = ver_len;
        }
        pm_util_lock_release(lock());
        ok as i32
    }
}

/// Copy the version string for `fqn` into `buf`. Same buffer protocol as
/// `module_at`: on success writes bytes and sets `*buf_len_io` to length;
/// returns 1 if set, 0 if module missing or version unset (still reports need).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_version(
    fqn_ptr: *const u8,
    fqn_len: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let ver_len = if e.is_null() { 0 } else { (*e).version_len };
        let ver = if e.is_null() {
            core::ptr::null()
        } else {
            (*e).version.as_ptr()
        };
        let rc = if ver_len > 0 {
            copy_span_to_buf(ver, ver_len as usize, buf, buf_len_io)
        } else {
            if !buf_len_io.is_null() {
                *buf_len_io = 0;
            }
            0
        };
        pm_util_lock_release(lock());
        rc
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_has(fqn_ptr: *const u8, fqn_len: u32) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let found = !entry_by_fqn(table(), fqn_ptr, fqn_len as usize).is_null();
        pm_util_lock_release(lock());
        found as i32
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_export_set(
    handle: pm_wasmmod_registry_handle_t,
    name_ptr: *const u8,
    name_len: u32,
    kind: pm_wasmmod_registry_export_kind_t,
    ptr: *mut c_void,
) -> i32 {
    if name_ptr.is_null() || name_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let ok = export_set_sig(
            table(),
            handle,
            name_ptr,
            name_len as usize,
            kind,
            ptr,
            core::ptr::null(),
            0,
        );
        pm_util_lock_release(lock());
        ok as i32
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_export_set_sig(
    handle: pm_wasmmod_registry_handle_t,
    name_ptr: *const u8,
    name_len: u32,
    kind: pm_wasmmod_registry_export_kind_t,
    ptr: *mut c_void,
    sig_ptr: *const u8,
    sig_len: u32,
) -> i32 {
    if name_ptr.is_null() || name_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let ok = export_set_sig(
            table(),
            handle,
            name_ptr,
            name_len as usize,
            kind,
            ptr,
            sig_ptr,
            sig_len as usize,
        );
        pm_util_lock_release(lock());
        ok as i32
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_mod_export(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
    kind: pm_wasmmod_registry_export_kind_t,
    ptr: *mut c_void,
    sig_ptr: *const u8,
    sig_len: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || name_ptr.is_null() || name_len == 0 {
        return 0;
    }
    /* Reached from a constructor on every seat; the table is fixed-
     * capacity .bss, so this writes a row directly — no heap, no
     * staging buffer, no drain pass. */
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let h = ensure(
            t,
            fqn_ptr,
            fqn_len as usize,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        let ok = h.index != u32::MAX
            && export_set_sig(
                t,
                h,
                name_ptr,
                name_len as usize,
                kind,
                ptr,
                sig_ptr,
                sig_len as usize,
            );
        pm_util_lock_release(lock());
        ok as i32
    }
}

fn copy_span_to_buf(src: *const u8, src_len: usize, buf: *mut u8, buf_len_io: *mut u32) -> i32 {
    unsafe {
        if buf_len_io.is_null() {
            return 0;
        }
        let cap = *buf_len_io as usize;
        if cap < src_len {
            *buf_len_io = src_len as u32;
            return 0;
        }
        if src_len > 0 && !buf.is_null() {
            core::ptr::copy_nonoverlapping(src, buf, src_len);
        }
        *buf_len_io = src_len as u32;
    }
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_module_count() -> u32 {
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let mut n: u32 = 0;
        let mut i = 0usize;
        while i < (*t).n_entries as usize {
            if (*t).entries[i].live {
                n += 1;
            }
            i += 1;
        }
        pm_util_lock_release(lock());
        n
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_module_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let mut seen: u32 = 0;
        let mut i = 0usize;
        let mut rc = 0;
        while i < (*t).n_entries as usize {
            if (*t).entries[i].live {
                if seen == index {
                    rc = copy_span_to_buf(
                        (*t).entries[i].fqn.as_ptr(),
                        (*t).entries[i].fqn_len as usize,
                        buf,
                        buf_len_io,
                    );
                    break;
                }
                seen += 1;
            }
            i += 1;
        }
        if rc == 0 && !buf_len_io.is_null() {
            *buf_len_io = 0;
        }
        pm_util_lock_release(lock());
        rc
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_export_count(
    fqn_ptr: *const u8,
    fqn_len: u32,
) -> u32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let n = if e.is_null() { 0 } else { (*e).n_exports };
        pm_util_lock_release(lock());
        n
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_export_at(
    fqn_ptr: *const u8,
    fqn_len: u32,
    index: u32,
    name_buf: *mut u8,
    name_len_io: *mut u32,
    kind_out: *mut pm_wasmmod_registry_export_kind_t,
    sig_buf: *mut u8,
    sig_len_io: *mut u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let mut rc = 0;
        if !e.is_null() && index < (*e).n_exports {
            let x = (*e).exports.as_ptr().add(index as usize);
            rc = copy_span_to_buf((*x).name.as_ptr(), (*x).name_len as usize, name_buf, name_len_io);
            if rc == 1 {
                if !kind_out.is_null() {
                    *kind_out = (*x).kind;
                }
                if !sig_len_io.is_null() {
                    if (*x).sig_len > 0 {
                        rc = copy_span_to_buf(
                            (*x).sig.as_ptr(),
                            (*x).sig_len as usize,
                            sig_buf,
                            sig_len_io,
                        );
                    } else {
                        *sig_len_io = 0;
                    }
                }
            }
        }
        if rc == 0 {
            if !name_len_io.is_null() {
                *name_len_io = 0;
            }
            if !sig_len_io.is_null() {
                *sig_len_io = 0;
            }
        }
        pm_util_lock_release(lock());
        rc
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_resolve_native(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_name_ptr: *const u8,
    export_name_len: u32,
) -> *mut c_void {
    if fqn_ptr.is_null() || fqn_len == 0 || export_name_ptr.is_null() || export_name_len == 0 {
        return core::ptr::null_mut();
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let p = resolve_native(
            table(),
            fqn_ptr,
            fqn_len as usize,
            export_name_ptr,
            export_name_len as usize,
        );
        pm_util_lock_release(lock());
        p
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_connect_import(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_name_ptr: *const u8,
    export_name_len: u32,
    out_slot: *mut *mut c_void,
) -> i32 {
    if fqn_ptr.is_null()
        || fqn_len == 0
        || export_name_ptr.is_null()
        || export_name_len == 0
        || out_slot.is_null()
    {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let p = resolve_native(
            table(),
            fqn_ptr,
            fqn_len as usize,
            export_name_ptr,
            export_name_len as usize,
        );
        pm_util_lock_release(lock());
        if p.is_null() {
            return 0;
        }
        *out_slot = p;
        1
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_call(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_name_ptr: *const u8,
    export_name_len: u32,
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || export_name_ptr.is_null() || export_name_len == 0 {
        return -1;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let p = resolve_native(
            table(),
            fqn_ptr,
            fqn_len as usize,
            export_name_ptr,
            export_name_len as usize,
        );
        pm_util_lock_release(lock());
        if p.is_null() {
            return -1;
        }
        // SAFETY: every ptr stored under PM_WASMMOD_REGISTRY_EXPORT_FN is
        // contracted (by whoever called export_set — the loader, for wasm
        // exports) to be a pm_wasmmod_registry_fn_t. The registry itself
        // never calls through a ptr of any other export kind. The
        // reinterpret goes through the fn_slot_t union (the value-level
        // twin of the C face's helper — a pointer-to-fn-pointer cast is a
        // load in disguise, and rsx has no transmute).
        let mut slot = fn_slot_t { raw: p };
        let f = unsafe { slot.fnptr };
        unsafe { f(args, nargs, results, nresults) }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_gc_visit(
    visit: extern "C" fn(*mut c_void, *mut c_void),
    ctx: *mut c_void,
) {
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let mut i = 0usize;
        while i < (*t).n_entries as usize {
            let e = (*t).entries.as_ptr().add(i);
            if (*e).live {
                let mut j = 0usize;
                while j < (*e).n_exports as usize {
                    let x = (*e).exports.as_ptr().add(j);
                    if (*x).kind == pm_wasmmod_registry_export_kind_t::Obj {
                        visit((*x).ptr, ctx);
                    }
                    j += 1;
                }
            }
            i += 1;
        }
        pm_util_lock_release(lock());
    }
}

/// Register a module test (`__tests__.*` / `PM_MOD_TEST_*`). Returns 1 on ok.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_register(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
    f: Option<pm_wasmmod_registry_test_fn_t>,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || name_ptr.is_null() || name_len == 0 || f.is_none() {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let h = ensure(
            t,
            fqn_ptr,
            fqn_len as usize,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        let e = entry_by_fqn(t, fqn_ptr, fqn_len as usize);
        let ok = test_upsert(e, name_ptr, name_len as usize, f, BODY_NATIVE, core::ptr::null(), 0);
        pm_util_lock_release(lock());
        ok as i32
    }
}

/// Register a guest pack test (case name → wasm export). No host trampoline.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_register_wasm(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
    export_ptr: *const u8,
    export_len: u32,
) -> i32 {
    if fqn_ptr.is_null()
        || fqn_len == 0
        || name_ptr.is_null()
        || name_len == 0
        || export_ptr.is_null()
        || export_len == 0
    {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let e = entry_by_fqn(t, fqn_ptr, fqn_len as usize);
        let ok = test_upsert(
            e,
            name_ptr,
            name_len as usize,
            None,
            BODY_WASM,
            export_ptr,
            export_len as usize,
        );
        pm_util_lock_release(lock());
        ok as i32
    }
}

/// Insert-or-replace one test row; `e == null` refuses.
unsafe fn test_upsert(
    e: *mut ModEntry,
    name: *const u8,
    name_len: usize,
    native: Option<pm_wasmmod_registry_test_fn_t>,
    body_kind: u8,
    export: *const u8,
    export_len: usize,
) -> bool {
    if e.is_null() || name_len > NAME_CAP || export_len > NAME_CAP {
        return false;
    }
    unsafe {
        let mut i = 0usize;
        while i < (*e).n_tests as usize {
            let tr = (*e).tests.as_mut_ptr().add(i);
            if (*tr).name_len as usize == name_len {
                let eq = span_eq((*tr).name.as_ptr(), name_len, name, name_len);
                if eq {
                    (*tr).body_kind = body_kind;
                    (*tr).native = native;
                    (*tr).export_len = 0;
                    if export_len > 0 {
                        let _ = span_put((*tr).export.as_mut_ptr(), NAME_CAP, export, export_len);
                        (*tr).export_len = export_len as u32;
                    }
                    return true;
                }
            }
            i += 1;
        }
        if (*e).n_tests as usize >= TST_MAX {
            return false;
        }
        let tr = (*e).tests.as_mut_ptr().add((*e).n_tests as usize);
        let stored = span_put((*tr).name.as_mut_ptr(), NAME_CAP, name, name_len);
        if stored == usize::MAX {
            return false;
        }
        (*tr).name_len = stored as u32;
        (*tr).body_kind = body_kind;
        (*tr).native = native;
        (*tr).export_len = 0;
        if export_len > 0 {
            let _ = span_put((*tr).export.as_mut_ptr(), NAME_CAP, export, export_len);
            (*tr).export_len = export_len as u32;
        }
        (*e).n_tests += 1;
        true
    }
}

/// Same as `test_upsert` for bench rows.
unsafe fn bench_upsert(
    e: *mut ModEntry,
    name: *const u8,
    name_len: usize,
    native: Option<pm_wasmmod_registry_bench_fn_t>,
    body_kind: u8,
    export: *const u8,
    export_len: usize,
) -> bool {
    if e.is_null() || name_len > NAME_CAP || export_len > NAME_CAP {
        return false;
    }
    unsafe {
        let mut i = 0usize;
        while i < (*e).n_benches as usize {
            let br = (*e).benches.as_mut_ptr().add(i);
            if (*br).name_len as usize == name_len {
                let eq = span_eq((*br).name.as_ptr(), name_len, name, name_len);
                if eq {
                    (*br).body_kind = body_kind;
                    (*br).native = native;
                    (*br).export_len = 0;
                    if export_len > 0 {
                        let _ = span_put((*br).export.as_mut_ptr(), NAME_CAP, export, export_len);
                        (*br).export_len = export_len as u32;
                    }
                    return true;
                }
            }
            i += 1;
        }
        if (*e).n_benches as usize >= BNC_MAX {
            return false;
        }
        let br = (*e).benches.as_mut_ptr().add((*e).n_benches as usize);
        let stored = span_put((*br).name.as_mut_ptr(), NAME_CAP, name, name_len);
        if stored == usize::MAX {
            return false;
        }
        (*br).name_len = stored as u32;
        (*br).body_kind = body_kind;
        (*br).native = native;
        (*br).export_len = 0;
        if export_len > 0 {
            let _ = span_put((*br).export.as_mut_ptr(), NAME_CAP, export, export_len);
            (*br).export_len = export_len as u32;
        }
        (*e).n_benches += 1;
        true
    }
}

/// Loader installs this so guest pack tests run without a trampoline pool.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_wasm_test_runner(
    f: Option<pm_wasmmod_registry_wasm_test_runner_t>,
) {
    unsafe {
        pm_util_lock_acquire(lock());
        *(WASM_TEST_RUNNER.0.get()) = f;
        pm_util_lock_release(lock());
    }
}

/// Loader installs this so guest pack benches run without a host trampoline
/// pool. Passing `None` clears the fill, mirroring `set_wasm_test_runner`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_wasm_bench_runner(
    f: Option<pm_wasmmod_registry_wasm_bench_runner_t>,
) {
    unsafe {
        pm_util_lock_acquire(lock());
        *(WASM_BENCH_RUNNER.0.get()) = f;
        pm_util_lock_release(lock());
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_count(fqn_ptr: *const u8, fqn_len: u32) -> u32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let n = if e.is_null() { 0 } else { (*e).n_tests };
        pm_util_lock_release(lock());
        n
    }
}

/// Copy test case name at `index` into `buf`. Returns 1 if present.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_at(
    fqn_ptr: *const u8,
    fqn_len: u32,
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let mut rc = 0;
        if !e.is_null() && index < (*e).n_tests {
            let tr = (*e).tests.as_ptr().add(index as usize);
            rc = copy_span_to_buf((*tr).name.as_ptr(), (*tr).name_len as usize, buf, buf_len_io);
        }
        if rc == 0 && !buf_len_io.is_null() {
            *buf_len_io = 0;
        }
        pm_util_lock_release(lock());
        rc
    }
}

/// One captured test body: enough plain data to run it after the table
/// lock is released (the body may itself call back into the registry).
struct TestBody {
    body_kind: u8,
    native: Option<pm_wasmmod_registry_test_fn_t>,
    fqn: [u8; FQN_CAP],
    fqn_len: u32,
    export: [u8; NAME_CAP],
    export_len: u32,
}

/// One captured bench body, same shape as TestBody with the bench fn type.
struct BenchBody {
    body_kind: u8,
    native: Option<pm_wasmmod_registry_bench_fn_t>,
    fqn: [u8; FQN_CAP],
    fqn_len: u32,
    export: [u8; NAME_CAP],
    export_len: u32,
}

unsafe fn run_test_body(b: *const TestBody) -> i32 {
    if (*b).body_kind == BODY_NATIVE {
        if let Some(f) = (*b).native {
            return unsafe { f() };
        }
        return -1;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let runner = *(WASM_TEST_RUNNER.0.get());
        pm_util_lock_release(lock());
        if let Some(runner) = runner {
            return runner(
                (*b).fqn.as_ptr(),
                (*b).fqn_len,
                (*b).export.as_ptr(),
                (*b).export_len,
            );
        }
        return -1;
    }
}

/// Run one named test. Returns case status (0=pass), or `-1` if missing.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_run(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || name_ptr.is_null() || name_len == 0 {
        return -1;
    }
    let mut body = TestBody {
        body_kind: BODY_NATIVE,
        native: None,
        fqn: [0; FQN_CAP],
        fqn_len: 0,
        export: [0; NAME_CAP],
        export_len: 0,
    };
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let mut found = false;
        if !e.is_null() {
            let mut i = 0usize;
            while i < (*e).n_tests as usize {
                let tr = (*e).tests.as_ptr().add(i);
                if (*tr).name_len as usize == name_len as usize {
                    let eq = span_eq((*tr).name.as_ptr(), name_len as usize, name_ptr, name_len as usize);
                    if eq {
                        body.body_kind = (*tr).body_kind;
                        body.native = (*tr).native;
                        body.export_len = (*tr).export_len;
                        if (*tr).export_len > 0 {
                            core::ptr::copy_nonoverlapping(
                                (*tr).export.as_ptr(),
                                body.export.as_mut_ptr(),
                                (*tr).export_len as usize,
                            );
                        }
                        found = true;
                        break;
                    }
                }
                i += 1;
            }
        }
        if found {
            body.fqn_len = fqn_len;
            if fqn_len as usize <= FQN_CAP {
                core::ptr::copy_nonoverlapping(fqn_ptr, body.fqn.as_mut_ptr(), fqn_len as usize);
            }
        }
        pm_util_lock_release(lock());
        if !found {
            return -1;
        }
        run_test_body(&body)
    }
}

/// Run every test for `fqn`. Returns number of failures (missing module → 0).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_run_all(fqn_ptr: *const u8, fqn_len: u32) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    let mut fails = 0i32;
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let n = if e.is_null() { 0 } else { (*e).n_tests };
        let mut i = 0usize;
        while i < n as usize {
            let tr = (*e).tests.as_ptr().add(i);
            let mut body = TestBody {
                body_kind: (*tr).body_kind,
                native: (*tr).native,
                fqn: [0; FQN_CAP],
                fqn_len: 0,
                export: [0; NAME_CAP],
                export_len: (*tr).export_len,
            };
            if (*tr).export_len > 0 {
                core::ptr::copy_nonoverlapping(
                    (*tr).export.as_ptr(),
                    body.export.as_mut_ptr(),
                    (*tr).export_len as usize,
                );
            }
            if fqn_len as usize <= FQN_CAP {
                body.fqn_len = fqn_len;
                core::ptr::copy_nonoverlapping(fqn_ptr, body.fqn.as_mut_ptr(), fqn_len as usize);
            }
            pm_util_lock_release(lock());
            let rc = run_test_body(&body);
            pm_util_lock_acquire(lock());
            if rc != 0 {
                fails += 1;
            }
            i += 1;
        }
        pm_util_lock_release(lock());
    }
    fails
}

/// Install the monotonic clock fill for bench timing (microseconds). A seat
/// provides this once at boot; metal wires it to `pm_metal_async_mono_us`.
/// Passing `None` clears the fill, which benches report as "no clock".
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_bench_clock(
    f: Option<pm_wasmmod_registry_bench_clock_t>,
) {
    unsafe {
        pm_util_lock_acquire(lock());
        *(BENCH_CLOCK.0.get()) = f;
        pm_util_lock_release(lock());
    }
}

/// Register a module benchmark (`__bench__.*` / `PM_MOD_BENCH_*`). Returns 1
/// on ok. A bench is `fn(iterations) -> i32`; the registry times it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_register(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
    f: Option<pm_wasmmod_registry_bench_fn_t>,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 || name_ptr.is_null() || name_len == 0 || f.is_none() {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let h = ensure(
            t,
            fqn_ptr,
            fqn_len as usize,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        let _ = h;
        let e = entry_by_fqn(t, fqn_ptr, fqn_len as usize);
        let ok = bench_upsert(e, name_ptr, name_len as usize, f, BODY_NATIVE, core::ptr::null(), 0);
        pm_util_lock_release(lock());
        ok as i32
    }
}

/// Register a guest pack benchmark (name → wasm export). No host trampoline.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_register_wasm(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
    export_ptr: *const u8,
    export_len: u32,
) -> i32 {
    if fqn_ptr.is_null()
        || fqn_len == 0
        || name_ptr.is_null()
        || name_len == 0
        || export_ptr.is_null()
        || export_len == 0
    {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let e = entry_by_fqn(t, fqn_ptr, fqn_len as usize);
        let ok = bench_upsert(
            e,
            name_ptr,
            name_len as usize,
            None,
            BODY_WASM,
            export_ptr,
            export_len as usize,
        );
        pm_util_lock_release(lock());
        ok as i32
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_count(fqn_ptr: *const u8, fqn_len: u32) -> u32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let n = if e.is_null() { 0 } else { (*e).n_benches };
        pm_util_lock_release(lock());
        n
    }
}

/// Copy bench name at `index` into `buf`. Returns 1 if present.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_at(
    fqn_ptr: *const u8,
    fqn_len: u32,
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let mut rc = 0;
        if !e.is_null() && index < (*e).n_benches {
            let br = (*e).benches.as_ptr().add(index as usize);
            rc = copy_span_to_buf((*br).name.as_ptr(), (*br).name_len as usize, buf, buf_len_io);
        }
        if rc == 0 && !buf_len_io.is_null() {
            *buf_len_io = 0;
        }
        pm_util_lock_release(lock());
        rc
    }
}

unsafe fn run_bench_body(b: *const BenchBody, iterations: u64) -> i32 {
    if (*b).body_kind == BODY_NATIVE {
        if let Some(f) = (*b).native {
            return unsafe { f(iterations) };
        }
        return -1;
    }
    unsafe {
        pm_util_lock_acquire(lock());
        let runner = *(WASM_BENCH_RUNNER.0.get());
        pm_util_lock_release(lock());
        if let Some(runner) = runner {
            // No loader to call the wasm export: a real missing-fill, not a
            // silent pass. Same triage as a clockless seat reporting
            // "no clock" — the guest bench can't be timed without a path to
            // its export, so it surfaces as FAILED rather than a fake number.
            return runner(
                (*b).fqn.as_ptr(),
                (*b).fqn_len,
                (*b).export.as_ptr(),
                (*b).export_len,
                iterations,
            );
        }
        return -1;
    }
}

unsafe fn bench_now() -> u64 {
    unsafe {
        pm_util_lock_acquire(lock());
        let clock = *(BENCH_CLOCK.0.get());
        pm_util_lock_release(lock());
        if let Some(clock) = clock {
            return clock();
        }
        return 0;
    }
}

/// Result of timing one bench. Negative means it did not run:
/// `-1` no clock, `-2` bench missing, `-3` bench failed its own run.
const BENCH_RC_NO_CLOCK: i64 = -1;
const BENCH_RC_MISSING: i64 = -2;
const BENCH_RC_FAILED: i64 = -3;

/// Time one bench over `iterations` ops and return **ns/op** (≥0), or a
/// negative `BENCH_RC_*`. The bench contract is "do `iterations` ops inside
/// this one call" (the Go `b.N` model), so a warmup call settles the runner
/// before the measured lap, and the lap's elapsed time divides by iterations.
unsafe fn time_bench_ns(b: *const BenchBody, iterations: u64) -> i64 {
    if iterations == 0 {
        return BENCH_RC_FAILED;
    }
    if unsafe { bench_now() } == 0 {
        return BENCH_RC_NO_CLOCK;
    }
    // Warmup: a small batch lets the runner park/schedule/init before timing,
    // so the measured lap starts warm. Failing warmup is not the bench's fault
    // unless it is genuinely broken; still surface the failure via FAILED.
    if unsafe { run_bench_body(b, BENCH_MEASURE_WARMUP) } != 0 {
        return BENCH_RC_FAILED;
    }
    let t0 = unsafe { bench_now() };
    if t0 == 0 {
        return BENCH_RC_NO_CLOCK;
    }
    if unsafe { run_bench_body(b, iterations) } != 0 {
        return BENCH_RC_FAILED;
    }
    let t1 = unsafe { bench_now() };
    if t1 == 0 {
        return BENCH_RC_NO_CLOCK;
    }
    let us = t1.wrapping_sub(t0);
    // ns/op from a uS clock, u128-free (the subset has no u128): split
    // the multiply to stay inside u64 — us is far below 2^54 in any real
    // bench lap, so us * 1000 cannot overflow before the divide.
    ((us as i64) * 1000) / (iterations as i64)
}

/// Run one named bench over `iterations` ops. Returns **ns/op** (≥0) or a
/// negative `BENCH_RC_*`. Informational — never a pass/fail gate.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_run(
    fqn_ptr: *const u8,
    fqn_len: u32,
    name_ptr: *const u8,
    name_len: u32,
    iterations: u64,
) -> i64 {
    if fqn_ptr.is_null() || fqn_len == 0 || name_ptr.is_null() || name_len == 0 {
        return BENCH_RC_MISSING;
    }
    let mut body = BenchBody {
        body_kind: BODY_NATIVE,
        native: None,
        fqn: [0; FQN_CAP],
        fqn_len: 0,
        export: [0; NAME_CAP],
        export_len: 0,
    };
    unsafe {
        pm_util_lock_acquire(lock());
        let e = entry_by_fqn(table(), fqn_ptr, fqn_len as usize);
        let mut found = false;
        if !e.is_null() {
            let mut i = 0usize;
            while i < (*e).n_benches as usize {
                let br = (*e).benches.as_ptr().add(i);
                if (*br).name_len as usize == name_len as usize {
                    let eq = span_eq((*br).name.as_ptr(), name_len as usize, name_ptr, name_len as usize);
                    if eq {
                        body.body_kind = (*br).body_kind;
                        body.native = (*br).native;
                        body.export_len = (*br).export_len;
                        if (*br).export_len > 0 {
                            core::ptr::copy_nonoverlapping(
                                (*br).export.as_ptr(),
                                body.export.as_mut_ptr(),
                                (*br).export_len as usize,
                            );
                        }
                        found = true;
                        break;
                    }
                }
                i += 1;
            }
        }
        if found {
            body.fqn_len = fqn_len;
            if fqn_len as usize <= FQN_CAP {
                core::ptr::copy_nonoverlapping(fqn_ptr, body.fqn.as_mut_ptr(), fqn_len as usize);
            }
        }
        pm_util_lock_release(lock());
        if !found {
            return BENCH_RC_MISSING;
        }
        time_bench_ns(&body, iterations)
    }
}

/* bench_run_all report lines, hand-formatted (no alloc::format! in the
 * subset): append helpers over the table's fixed out buffer. */
unsafe fn out_reset(t: *mut Table) {
    (*t).out_len = 0;
}

unsafe fn out_put(t: *mut Table, s: *const u8, n: usize) {
    let at = (*t).out_len as usize;
    if at + n > OUT_CAP {
        return;
    }
    let mut i = 0usize;
    while i < n {
        unsafe {
            *(*t).out.as_mut_ptr().add(at + i) = *s.add(i);
        }
        i += 1;
    }
    (*t).out_len += n as u32;
}

unsafe fn out_puts(t: *mut Table, s: *const u8) {
    unsafe {
        let mut n = 0usize;
        while *s.add(n) != 0 {
            n += 1;
        }
        out_put(t, s, n);
    }
}

unsafe fn out_put_u64(t: *mut Table, v: u64) {
    unsafe {
        let mut digs: [u8; 20] = [0; 20];
        let mut nd = 0usize;
        let mut x = v;
        if x == 0 {
            digs[0] = b'0';
            nd = 1;
        } else {
            while x > 0 && nd < 20 {
                digs[nd] = b'0' + (x % 10) as u8;
                x /= 10;
                nd += 1;
            }
        }
        let mut q = nd;
        while q > 0 {
            q -= 1;
            out_put(t, &digs[q], 1);
        }
    }
}

/// Run every bench for `fqn` over `iterations` ops, formatting a report line
/// per bench into `buf`. Returns number of benches that did not run cleanly
/// (0 = all ok). Informational — the caller decides what the numbers mean,
/// and this never gates.
///
/// `buf_len_io` in: capacity, out: bytes written (or needed on overflow).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_run_all(
    fqn_ptr: *const u8,
    fqn_len: u32,
    iterations: u64,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    if fqn_ptr.is_null() || fqn_len == 0 {
        return 0;
    }
    let mut bad = 0i32;
    unsafe {
        pm_util_lock_acquire(lock());
        let t = table();
        let e = entry_by_fqn(t, fqn_ptr, fqn_len as usize);
        if e.is_null() || (*e).n_benches == 0 {
            pm_util_lock_release(lock());
            if !buf_len_io.is_null() {
                *buf_len_io = 0;
            }
            return 0;
        }
        out_reset(t);
        out_put(t, fqn_ptr, fqn_len as usize);
        out_puts(t, b"\n\0".as_ptr());
        /* Bench bodies are captured one at a time: each is run with the
         * table lock released (it may call back in), and the report line
         * is appended on re-acquire. */
        let n = (*e).n_benches;
        let mut i = 0usize;
        while i < n as usize {
            let br = (*e).benches.as_ptr().add(i);
            let mut body = BenchBody {
                body_kind: (*br).body_kind,
                native: (*br).native,
                fqn: [0; FQN_CAP],
                fqn_len: 0,
                export: [0; NAME_CAP],
                export_len: (*br).export_len,
            };
            if (*br).export_len > 0 {
                core::ptr::copy_nonoverlapping(
                    (*br).export.as_ptr(),
                    body.export.as_mut_ptr(),
                    (*br).export_len as usize,
                );
            }
            if fqn_len as usize <= FQN_CAP {
                body.fqn_len = fqn_len;
                core::ptr::copy_nonoverlapping(fqn_ptr, body.fqn.as_mut_ptr(), fqn_len as usize);
            }
            pm_util_lock_release(lock());
            let ns = time_bench_ns(&body, iterations);
            pm_util_lock_acquire(lock());
            out_puts(t, b"* \0".as_ptr());
            let br2 = (*e).benches.as_ptr().add(i);
            out_put(t, (*br2).name.as_ptr(), (*br2).name_len as usize);
            if ns < 0 {
                bad += 1;
                if ns == BENCH_RC_NO_CLOCK {
                    out_puts(t, b": no clock\n\0".as_ptr());
                } else if ns == BENCH_RC_MISSING {
                    out_puts(t, b": missing\n\0".as_ptr());
                } else {
                    out_puts(t, b": failed\n\0".as_ptr());
                }
            } else {
                out_puts(t, b": \0".as_ptr());
                out_put_u64(t, ns as u64);
                out_puts(t, b" ns/op over \0".as_ptr());
                out_put_u64(t, iterations);
                out_puts(t, b" iters\n\0".as_ptr());
            }
            i += 1;
        }
        /* copy out under the lock (the buffer is table-owned) */
        let rc = copy_span_to_buf((*t).out.as_ptr(), (*t).out_len as usize, buf, buf_len_io);
        pm_util_lock_release(lock());
        let _ = rc;
    }
    bad
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_init, "void(void)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_publish, "pm_wasmmod_registry_handle_t(const uint8_t *, uint32_t, pm_wasmmod_registry_container_kind_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_publish_ver, "pm_wasmmod_registry_handle_t(const uint8_t *, uint32_t, pm_wasmmod_registry_container_kind_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_ensure, "pm_wasmmod_registry_handle_t(const uint8_t *, uint32_t, pm_wasmmod_registry_container_kind_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_unpublish, "int32_t(pm_wasmmod_registry_handle_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_has, "int32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_container, "int32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_set_version, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_version, "int32_t(const uint8_t *, uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_export_set, "int32_t(pm_wasmmod_registry_handle_t, const uint8_t *, uint32_t, pm_wasmmod_registry_export_kind_t, void *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_export_set_sig, "int32_t(pm_wasmmod_registry_handle_t, const uint8_t *, uint32_t, pm_wasmmod_registry_export_kind_t, void *, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_mod_export, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, pm_wasmmod_registry_export_kind_t, void *, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_resolve_native, "void *(const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_connect_import, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, void **)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_call, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, const pm_wasmmod_registry_value_t *, uint32_t, pm_wasmmod_registry_value_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_gc_visit, "void(void (*)(void *, void *), void *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_module_count, "uint32_t(void)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_module_at, "int32_t(uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_export_count, "uint32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_export_at, "int32_t(const uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t *, pm_wasmmod_registry_export_kind_t *, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_register, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, pm_wasmmod_registry_test_fn_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_register_wasm, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_set_wasm_test_runner, "void(pm_wasmmod_registry_wasm_test_runner_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_set_wasm_bench_runner, "void(pm_wasmmod_registry_wasm_bench_runner_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_count, "uint32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_at, "int32_t(const uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_run, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_run_all, "int32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_set_bench_clock, "void(pm_wasmmod_registry_bench_clock_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_bench_register, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, pm_wasmmod_registry_bench_fn_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_bench_register_wasm, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_bench_count, "uint32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_bench_at, "int32_t(const uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_bench_run, "int64_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, uint64_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_bench_run_all, "int32_t(const uint8_t *, uint32_t, uint64_t, uint8_t *, uint32_t *)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
