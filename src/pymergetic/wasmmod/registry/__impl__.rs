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
    /* The seat's own allocator, which is where a row comes from once the
     * seat has one: stdlib malloc on the µPy and host seats (the same
     * MICROPY_WASM_MALLOC net.search takes its pack buffers from), the bump
     * over the boot arena on firmware (port/upy/firmware_upy.c). A
     * registration is a once-per-card / once-per-pack event and a row is
     * one allocation, so nothing here is on a hot path. */
    fn malloc(size: usize) -> *mut c_void;
    fn free(ptr: *mut c_void);
}

/* pymergetic.util.limits — the seat's knobs. The struct is spelled here
 * because this card owns three of them (the generated face
 * util/limits/__exports__.rs has the knob opaque), same as the loader's
 * image knob; `limits_attach` below is that face, not a second one. */
#[repr(C)]
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
    /// `int32_t (*apply)(pm_util_limit_t *)` on the C side, spelled here as
    /// the opaque pointer it is on every target: only the owning card fills
    /// it, for a knob whose new value has to be pushed somewhere, and these
    /// three are read where they are used. Spelling the fn type would also
    /// put its typedef ahead of this struct in the in-kernel compile (rsx
    /// lowers a fn-pointer type in an earlier pass than the struct it
    /// names), which is a card the kernel could not rebuild.
    apply: *mut c_void,
}

unsafe fn limits_attach(knob: *mut pm_util_limit_t) -> i32 {
    unsafe {
        crate::util::limits::pm_util_limits_attach(
            knob as *mut crate::util::limits::pm_util_limit_t,
        )
    }
}

/* ---- budgets ---- */

const MOD_MAX: usize = 128;
/* Export, test and bench rows are taken one at a time, so a module has no
 * per-module ceiling any more: what a seat will hold is the knob
 * (the exports, tests and benches knobs), and a row taken while the
 * seat has a heap comes from the heap.
 *
 * The pools below are the pre-heap floor, not a policy limit. A card's
 * registrations arrive from its constructor, and on a board that is crt0 —
 * a board's crt0.S calls pm_metal_elf_ctors_run before main, long before
 * the boot arena and therefore before malloc answers — so those rows can
 * only come out of .bss. A BIOS image is ~915 constructors all
 * told (.init_array / 8 on its elf), and the fullest seat built here takes
 * 731 export rows from its cards, so the floor is that with room to grow
 * rather than the number that only just fits; each knob's `used` is the live
 * row count, so a seat can read how much of its floor it takes. A hosted
 * seat (unix, browser, the host tests) has an allocator from its first
 * constructor and never touches the pools. */
const EXPORT_POOL: usize = 1536;
const TEST_POOL: usize = 384;
const BENCH_POOL: usize = 64;
/* The knob defaults are the pool sizes: what a seat hands out before it has
 * a heap is also where a seat that never grows the knob stops. */
const EXPORT_DEFAULT: u32 = 1536;
const TEST_DEFAULT: u32 = 384;
const BENCH_DEFAULT: u32 = 64;
const FQN_CAP: usize = 64;
const VER_CAP: usize = 32;
/* SIG_CAP must hold the longest valid ABI signature in the tree. The
 * dark-export audit found silent refusals at 160 (13-arg faces:
 * pm_metal_jit_c_object_compile_target is 173 bytes as stringified —
 * macro-continuation whitespace counts). 256 holds it with headroom and
 * a loud-refusal ring below turns any future overlong face into a
 * manifest-test failure instead of a dark face. Costs 96B x EXP_MAX x
 * MOD_MAX more .bss (~786 KiB on a ~89 MiB firmware bss; host/browser
 * are heap-backed). The gen face reader has always allowed 512. */
const SIG_CAP: usize = 256;
const NAME_CAP: usize = 64;
const OUT_CAP: usize = 4096;

/* ---- loud-refusal ring ----
 * A registration that cannot be stored (name/sig over cap, export table
 * full, module table full) is recorded here and returned as `false` to
 * the caller. The card's manifest test fails when this ring is non-empty
 * at prove time — a public face may never disappear silently. */
const REF_MAX: usize = 16;
const REF_NAME_CAP: usize = 96;

const REF_REASON_SIG_OVER_CAP: u8 = 1;
const REF_REASON_NAME_OVER_CAP: u8 = 2;
const REF_REASON_EXPORTS_FULL: u8 = 3;
const REF_REASON_MODULES_FULL: u8 = 4;
const REF_REASON_STORE_FAIL: u8 = 5;

#[derive(Clone, Copy)]
struct Refusal {
    fqn: [u8; FQN_CAP],
    fqn_len: u32,
    name: [u8; REF_NAME_CAP],
    name_len: u32,
    reason: u8,
    count: u32,
}

static REFUSALS: Mut<[Refusal; REF_MAX]> =
    Mut(UnsafeCell::new([Refusal {
        fqn: [0; FQN_CAP],
        fqn_len: 0,
        name: [0; REF_NAME_CAP],
        name_len: 0,
        reason: 0,
        count: 0,
    }; REF_MAX]));
static N_REFUSALS: Mut<u32> = Mut(UnsafeCell::new(0));

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
/// Metal provides a strong fill reaching `pm_metal_coop_mono_us`; each seat
/// that can time provides its own (POSIX `clock_gettime` on unix, a
/// `performance.now`-derived value on emcc, the firmware timer otherwise).
pub type pm_wasmmod_registry_bench_clock_t = unsafe extern "C" fn() -> u64;

/* A test/bench body is either a native fn or a wasm export name — a
 * data-carrying enum would be a sum type, which rsx refuses; the
 * plain-data equivalent is a kind tag + the union payload spelled as
 * fields (null fn = WasmExport). */
const BODY_NATIVE: u8 = 0;
const BODY_WASM: u8 = 1;

/* A row's string. The bytes are the row's own (copied into the tail of its
 * allocation) when the row came from the heap, and the caller's when it
 * came from the pre-heap pool: a pre-heap registration is by construction a
 * constructor's literal, which outlives every table. Either way a reader
 * sees one (ptr, len) — the same span discipline as every face here. */
#[derive(Clone, Copy)]
struct Span {
    ptr: *const u8,
    len: u32,
}

/* Clone/Copy: rows repeat inside `[row; N]` initializers (the rsx subset
 * has no Default, and a bare struct-lit const is not an accepted static
 * shape — asgi's Conn rows use the same derive). */
#[derive(Clone, Copy)]
struct Export {
    name: Span,
    kind: pm_wasmmod_registry_export_kind_t,
    /// `Fn`/`Mem`/`I64`/`F32`/`F64`/`BufPtr`: a real native pointer.
    /// `Obj`: an opaque token owned by whichever embedder eventually
    /// attaches a GC (see `gc_visit`) — the registry never interprets
    /// it, only stores and hands it back.
    ptr: *mut c_void,
    /// Optional C prototype spelling for facegen (e.g. `int(void)`).
    /// len 0 = unset.
    sig: Span,
    /// The module's next export, in registration order. Also the pool's
    /// free-list link once the row is given back.
    next: *mut Export,
    /// Row from the seat's heap (one free gives it back) or from the
    /// pre-heap pool (it goes on the free list).
    heap: bool,
}

#[derive(Clone, Copy)]
struct TestEntry {
    name: Span,
    body_kind: u8,
    native: Option<pm_wasmmod_registry_test_fn_t>,
    export: Span,
    next: *mut TestEntry,
    heap: bool,
}

#[derive(Clone, Copy)]
struct BenchEntry {
    name: Span,
    body_kind: u8,
    native: Option<pm_wasmmod_registry_bench_fn_t>,
    export: Span,
    next: *mut BenchEntry,
    heap: bool,
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
    /// Head of the module's export chain; `n_exports` is its length, which
    /// is what `export_count` has always reported.
    exports: *mut Export,
    n_exports: u32,
    tests: *mut TestEntry,
    n_tests: u32,
    benches: *mut BenchEntry,
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

/* The one table: 128 module rows, each holding the head of its export,
 * test and bench chains (the rows themselves come from `export_take` and
 * friends below — 128 * 64 fixed export rows was 3.6MB of a seat's .bss
 * for exports nobody had registered). A ctor-time registration is still
 * just a row write, so the two-phase staging buffer the old
 * allocator-backed design needed stays gone.
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
        exports: ptr::null_mut(),
        n_exports: 0,
        tests: ptr::null_mut(),
        n_tests: 0,
        benches: ptr::null_mut(),
        n_benches: 0,
    }; MOD_MAX],
    n_entries: 0,
    out: [0; OUT_CAP],
    out_len: 0,
}));

/* ---- row storage ----
 *
 * The pre-heap pools, their free lists (a pack that loads, unloads and
 * loads again must not drain a pool), the live counts each knob reports as
 * `used`, and the knobs themselves. Every one of these is zero at rest, so
 * the pools ride .bss the same way the table does. */

static EXPORT_ROWS: Mut<[Export; EXPORT_POOL]> = Mut(UnsafeCell::new([Export {
    name: Span {
        ptr: ptr::null(),
        len: 0,
    },
    kind: pm_wasmmod_registry_export_kind_t::Fn,
    ptr: ptr::null_mut(),
    sig: Span {
        ptr: ptr::null(),
        len: 0,
    },
    next: ptr::null_mut(),
    heap: false,
}; EXPORT_POOL]));
static EXPORT_TAKEN: Mut<u32> = Mut(UnsafeCell::new(0));
static EXPORT_FREE: Mut<*mut Export> = Mut(UnsafeCell::new(ptr::null_mut()));
static EXPORT_LIVE: Mut<u32> = Mut(UnsafeCell::new(0));

static TEST_ROWS: Mut<[TestEntry; TEST_POOL]> = Mut(UnsafeCell::new([TestEntry {
    name: Span {
        ptr: ptr::null(),
        len: 0,
    },
    body_kind: BODY_NATIVE,
    native: None,
    export: Span {
        ptr: ptr::null(),
        len: 0,
    },
    next: ptr::null_mut(),
    heap: false,
}; TEST_POOL]));
static TEST_TAKEN: Mut<u32> = Mut(UnsafeCell::new(0));
static TEST_FREE: Mut<*mut TestEntry> = Mut(UnsafeCell::new(ptr::null_mut()));
static TEST_LIVE: Mut<u32> = Mut(UnsafeCell::new(0));

static BENCH_ROWS: Mut<[BenchEntry; BENCH_POOL]> = Mut(UnsafeCell::new([BenchEntry {
    name: Span {
        ptr: ptr::null(),
        len: 0,
    },
    body_kind: BODY_NATIVE,
    native: None,
    export: Span {
        ptr: ptr::null(),
        len: 0,
    },
    next: ptr::null_mut(),
    heap: false,
}; BENCH_POOL]));
static BENCH_TAKEN: Mut<u32> = Mut(UnsafeCell::new(0));
static BENCH_FREE: Mut<*mut BenchEntry> = Mut(UnsafeCell::new(ptr::null_mut()));
static BENCH_LIVE: Mut<u32> = Mut(UnsafeCell::new(0));

/// Set once any allocation on this seat has succeeded. Before that a row
/// has to come out of a pool and borrow its caller's bytes; after it, a row
/// that cannot be allocated is a loud refusal rather than a borrow of bytes
/// that may not outlive the table.
static HEAP_SEEN: Mut<u32> = Mut(UnsafeCell::new(0));
static KNOBS_DONE: Mut<u32> = Mut(UnsafeCell::new(0));

static EXPORTS_KNOB: Mut<pm_util_limit_t> = Mut(UnsafeCell::new(pm_util_limit_t {
    name: "pymergetic.wasmmod.registry.exports\0".as_ptr(),
    module: "pymergetic.wasmmod.registry\0".as_ptr(),
    leaf: "exports\0".as_ptr(),
    soft: EXPORT_DEFAULT,
    hard: 0,
    dflt: EXPORT_DEFAULT,
    used: ptr::null(),
    next: ptr::null_mut(),
    apply: ptr::null_mut(),
}));
static TESTS_KNOB: Mut<pm_util_limit_t> = Mut(UnsafeCell::new(pm_util_limit_t {
    name: "pymergetic.wasmmod.registry.tests\0".as_ptr(),
    module: "pymergetic.wasmmod.registry\0".as_ptr(),
    leaf: "tests\0".as_ptr(),
    soft: TEST_DEFAULT,
    hard: 0,
    dflt: TEST_DEFAULT,
    used: ptr::null(),
    next: ptr::null_mut(),
    apply: ptr::null_mut(),
}));
static BENCHES_KNOB: Mut<pm_util_limit_t> = Mut(UnsafeCell::new(pm_util_limit_t {
    name: "pymergetic.wasmmod.registry.benches\0".as_ptr(),
    module: "pymergetic.wasmmod.registry\0".as_ptr(),
    leaf: "benches\0".as_ptr(),
    soft: BENCH_DEFAULT,
    hard: 0,
    dflt: BENCH_DEFAULT,
    used: ptr::null(),
    next: ptr::null_mut(),
    apply: ptr::null_mut(),
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

unsafe fn span_copy(dst: *mut u8, src: *const u8, n: usize) {
    if n == 0 || dst.is_null() || src.is_null() {
        return;
    }
    unsafe { core::ptr::copy_nonoverlapping(src, dst, n) };
}

/* ---- rows (all under the caller-held table lock) ---- */

/// The card's knobs onto the seat's list, once. Reached from the first row
/// take as well as from `init`: a registration arrives from a constructor
/// long before any seat calls init, and a seat should be able to read what
/// the registry will hold either way.
unsafe fn knobs_attach() {
    unsafe {
        if *KNOBS_DONE.0.get() != 0 {
            return;
        }
        *KNOBS_DONE.0.get() = 1;
        (*EXPORTS_KNOB.0.get()).used = EXPORT_LIVE.0.get() as *const u32;
        (*TESTS_KNOB.0.get()).used = TEST_LIVE.0.get() as *const u32;
        (*BENCHES_KNOB.0.get()).used = BENCH_LIVE.0.get() as *const u32;
        let _ = limits_attach(EXPORTS_KNOB.0.get());
        let _ = limits_attach(TESTS_KNOB.0.get());
        let _ = limits_attach(BENCHES_KNOB.0.get());
    }
}

/// Room for one more row? A knob at zero never refuses, the same reading
/// every other knob in the tree has.
unsafe fn knob_room(knob: *mut pm_util_limit_t, live: u32) -> bool {
    unsafe {
        knobs_attach();
        let soft = (*knob).soft;
        soft == 0 || live < soft
    }
}

/// A row's memory from the seat's allocator: the row struct, then room for
/// its strings, in one allocation so one free gives it all back.
unsafe fn heap_take(n: usize) -> *mut u8 {
    unsafe {
        let p = malloc(n) as *mut u8;
        if !p.is_null() {
            *HEAP_SEEN.0.get() = 1;
        }
        p
    }
}

unsafe fn export_pool_take() -> *mut Export {
    unsafe {
        let freed = *EXPORT_FREE.0.get();
        if !freed.is_null() {
            *EXPORT_FREE.0.get() = (*freed).next;
            return freed;
        }
        let taken = *EXPORT_TAKEN.0.get() as usize;
        if taken >= EXPORT_POOL {
            return core::ptr::null_mut();
        }
        *EXPORT_TAKEN.0.get() = (taken + 1) as u32;
        (EXPORT_ROWS.0.get() as *mut Export).add(taken)
    }
}

/// One export row carrying `name` and `sig`: from the heap when the seat
/// has one, with both strings copied into the row's own tail, else from the
/// pre-heap pool with the caller's literals borrowed. Null = refused.
unsafe fn export_take(
    name: *const u8,
    name_len: usize,
    sig: *const u8,
    sig_len: usize,
) -> *mut Export {
    unsafe {
        let mut x: *mut Export = core::ptr::null_mut();
        let p = heap_take(core::mem::size_of::<Export>() + name_len + sig_len);
        if !p.is_null() {
            x = p as *mut Export;
            let tail = p.add(core::mem::size_of::<Export>());
            span_copy(tail, name, name_len);
            span_copy(tail.add(name_len), sig, sig_len);
            (*x).name = Span {
                ptr: tail,
                len: name_len as u32,
            };
            (*x).sig = Span {
                ptr: tail.add(name_len),
                len: sig_len as u32,
            };
            (*x).heap = true;
        } else if *HEAP_SEEN.0.get() == 0 {
            x = export_pool_take();
            if !x.is_null() {
                (*x).name = Span {
                    ptr: name,
                    len: name_len as u32,
                };
                (*x).sig = Span {
                    ptr: sig,
                    len: sig_len as u32,
                };
                (*x).heap = false;
            }
        }
        if x.is_null() {
            return core::ptr::null_mut();
        }
        (*x).kind = pm_wasmmod_registry_export_kind_t::Fn;
        (*x).ptr = core::ptr::null_mut();
        (*x).next = core::ptr::null_mut();
        *EXPORT_LIVE.0.get() += 1;
        x
    }
}

unsafe fn export_give(x: *mut Export) {
    if x.is_null() {
        return;
    }
    unsafe {
        let live = *EXPORT_LIVE.0.get();
        if live > 0 {
            *EXPORT_LIVE.0.get() = live - 1;
        }
        if (*x).heap {
            free(x as *mut c_void);
            return;
        }
        (*x).name = Span {
            ptr: core::ptr::null(),
            len: 0,
        };
        (*x).sig = Span {
            ptr: core::ptr::null(),
            len: 0,
        };
        (*x).ptr = core::ptr::null_mut();
        (*x).next = *EXPORT_FREE.0.get();
        *EXPORT_FREE.0.get() = x;
    }
}

/// The module's export at `index`, or null: the chain is in registration
/// order, which is the order `export_at` has always reported.
unsafe fn export_at_ptr(e: *mut ModEntry, index: usize) -> *mut Export {
    unsafe {
        let mut x = (*e).exports;
        let mut i = 0usize;
        while !x.is_null() && i < index {
            x = (*x).next;
            i += 1;
        }
        x
    }
}

unsafe fn export_find(e: *mut ModEntry, name: *const u8, name_len: usize) -> *mut Export {
    unsafe {
        let mut x = (*e).exports;
        while !x.is_null() {
            if (*x).name.len as usize == name_len
                && span_eq((*x).name.ptr, name_len, name, name_len)
            {
                return x;
            }
            x = (*x).next;
        }
        core::ptr::null_mut()
    }
}

unsafe fn export_append(e: *mut ModEntry, x: *mut Export) {
    unsafe {
        let head = (*e).exports;
        if head.is_null() {
            (*e).exports = x;
        } else {
            let mut last = head;
            while !(*last).next.is_null() {
                last = (*last).next;
            }
            (*last).next = x;
        }
        (*e).n_exports += 1;
    }
}

/// Put `fresh` where `old` sits, so an updated export keeps its place in
/// the chain, and give the old row back.
unsafe fn export_replace(e: *mut ModEntry, old: *mut Export, fresh: *mut Export) {
    unsafe {
        (*fresh).next = (*old).next;
        if (*e).exports == old {
            (*e).exports = fresh;
        } else {
            let mut prev = (*e).exports;
            while !prev.is_null() && (*prev).next != old {
                prev = (*prev).next;
            }
            if !prev.is_null() {
                (*prev).next = fresh;
            }
        }
        export_give(old);
    }
}

unsafe fn export_chain_give(e: *mut ModEntry) {
    unsafe {
        let mut x = (*e).exports;
        while !x.is_null() {
            let nx = (*x).next;
            export_give(x);
            x = nx;
        }
        (*e).exports = core::ptr::null_mut();
        (*e).n_exports = 0;
    }
}

unsafe fn test_pool_take() -> *mut TestEntry {
    unsafe {
        let freed = *TEST_FREE.0.get();
        if !freed.is_null() {
            *TEST_FREE.0.get() = (*freed).next;
            return freed;
        }
        let taken = *TEST_TAKEN.0.get() as usize;
        if taken >= TEST_POOL {
            return core::ptr::null_mut();
        }
        *TEST_TAKEN.0.get() = (taken + 1) as u32;
        (TEST_ROWS.0.get() as *mut TestEntry).add(taken)
    }
}

/// One test row, same two sources as `export_take`. The second string is
/// the guest export a wasm case runs through, empty for a native body.
unsafe fn test_take(
    name: *const u8,
    name_len: usize,
    export: *const u8,
    export_len: usize,
) -> *mut TestEntry {
    unsafe {
        let mut tr: *mut TestEntry = core::ptr::null_mut();
        let p = heap_take(core::mem::size_of::<TestEntry>() + name_len + export_len);
        if !p.is_null() {
            tr = p as *mut TestEntry;
            let tail = p.add(core::mem::size_of::<TestEntry>());
            span_copy(tail, name, name_len);
            span_copy(tail.add(name_len), export, export_len);
            (*tr).name = Span {
                ptr: tail,
                len: name_len as u32,
            };
            (*tr).export = Span {
                ptr: tail.add(name_len),
                len: export_len as u32,
            };
            (*tr).heap = true;
        } else if *HEAP_SEEN.0.get() == 0 {
            tr = test_pool_take();
            if !tr.is_null() {
                (*tr).name = Span {
                    ptr: name,
                    len: name_len as u32,
                };
                (*tr).export = Span {
                    ptr: export,
                    len: export_len as u32,
                };
                (*tr).heap = false;
            }
        }
        if tr.is_null() {
            return core::ptr::null_mut();
        }
        (*tr).body_kind = BODY_NATIVE;
        (*tr).native = None;
        (*tr).next = core::ptr::null_mut();
        *TEST_LIVE.0.get() += 1;
        tr
    }
}

unsafe fn test_give(tr: *mut TestEntry) {
    if tr.is_null() {
        return;
    }
    unsafe {
        let live = *TEST_LIVE.0.get();
        if live > 0 {
            *TEST_LIVE.0.get() = live - 1;
        }
        if (*tr).heap {
            free(tr as *mut c_void);
            return;
        }
        (*tr).name = Span {
            ptr: core::ptr::null(),
            len: 0,
        };
        (*tr).export = Span {
            ptr: core::ptr::null(),
            len: 0,
        };
        (*tr).native = None;
        (*tr).next = *TEST_FREE.0.get();
        *TEST_FREE.0.get() = tr;
    }
}

unsafe fn test_at_ptr(e: *mut ModEntry, index: usize) -> *mut TestEntry {
    unsafe {
        let mut tr = (*e).tests;
        let mut i = 0usize;
        while !tr.is_null() && i < index {
            tr = (*tr).next;
            i += 1;
        }
        tr
    }
}

unsafe fn test_find(e: *mut ModEntry, name: *const u8, name_len: usize) -> *mut TestEntry {
    unsafe {
        let mut tr = (*e).tests;
        while !tr.is_null() {
            if (*tr).name.len as usize == name_len
                && span_eq((*tr).name.ptr, name_len, name, name_len)
            {
                return tr;
            }
            tr = (*tr).next;
        }
        core::ptr::null_mut()
    }
}

unsafe fn test_append(e: *mut ModEntry, tr: *mut TestEntry) {
    unsafe {
        let head = (*e).tests;
        if head.is_null() {
            (*e).tests = tr;
        } else {
            let mut last = head;
            while !(*last).next.is_null() {
                last = (*last).next;
            }
            (*last).next = tr;
        }
        (*e).n_tests += 1;
    }
}

unsafe fn test_replace(e: *mut ModEntry, old: *mut TestEntry, fresh: *mut TestEntry) {
    unsafe {
        (*fresh).next = (*old).next;
        if (*e).tests == old {
            (*e).tests = fresh;
        } else {
            let mut prev = (*e).tests;
            while !prev.is_null() && (*prev).next != old {
                prev = (*prev).next;
            }
            if !prev.is_null() {
                (*prev).next = fresh;
            }
        }
        test_give(old);
    }
}

unsafe fn test_chain_give(e: *mut ModEntry) {
    unsafe {
        let mut tr = (*e).tests;
        while !tr.is_null() {
            let nx = (*tr).next;
            test_give(tr);
            tr = nx;
        }
        (*e).tests = core::ptr::null_mut();
        (*e).n_tests = 0;
    }
}

unsafe fn bench_pool_take() -> *mut BenchEntry {
    unsafe {
        let freed = *BENCH_FREE.0.get();
        if !freed.is_null() {
            *BENCH_FREE.0.get() = (*freed).next;
            return freed;
        }
        let taken = *BENCH_TAKEN.0.get() as usize;
        if taken >= BENCH_POOL {
            return core::ptr::null_mut();
        }
        *BENCH_TAKEN.0.get() = (taken + 1) as u32;
        (BENCH_ROWS.0.get() as *mut BenchEntry).add(taken)
    }
}

unsafe fn bench_take(
    name: *const u8,
    name_len: usize,
    export: *const u8,
    export_len: usize,
) -> *mut BenchEntry {
    unsafe {
        let mut br: *mut BenchEntry = core::ptr::null_mut();
        let p = heap_take(core::mem::size_of::<BenchEntry>() + name_len + export_len);
        if !p.is_null() {
            br = p as *mut BenchEntry;
            let tail = p.add(core::mem::size_of::<BenchEntry>());
            span_copy(tail, name, name_len);
            span_copy(tail.add(name_len), export, export_len);
            (*br).name = Span {
                ptr: tail,
                len: name_len as u32,
            };
            (*br).export = Span {
                ptr: tail.add(name_len),
                len: export_len as u32,
            };
            (*br).heap = true;
        } else if *HEAP_SEEN.0.get() == 0 {
            br = bench_pool_take();
            if !br.is_null() {
                (*br).name = Span {
                    ptr: name,
                    len: name_len as u32,
                };
                (*br).export = Span {
                    ptr: export,
                    len: export_len as u32,
                };
                (*br).heap = false;
            }
        }
        if br.is_null() {
            return core::ptr::null_mut();
        }
        (*br).body_kind = BODY_NATIVE;
        (*br).native = None;
        (*br).next = core::ptr::null_mut();
        *BENCH_LIVE.0.get() += 1;
        br
    }
}

unsafe fn bench_give(br: *mut BenchEntry) {
    if br.is_null() {
        return;
    }
    unsafe {
        let live = *BENCH_LIVE.0.get();
        if live > 0 {
            *BENCH_LIVE.0.get() = live - 1;
        }
        if (*br).heap {
            free(br as *mut c_void);
            return;
        }
        (*br).name = Span {
            ptr: core::ptr::null(),
            len: 0,
        };
        (*br).export = Span {
            ptr: core::ptr::null(),
            len: 0,
        };
        (*br).native = None;
        (*br).next = *BENCH_FREE.0.get();
        *BENCH_FREE.0.get() = br;
    }
}

unsafe fn bench_at_ptr(e: *mut ModEntry, index: usize) -> *mut BenchEntry {
    unsafe {
        let mut br = (*e).benches;
        let mut i = 0usize;
        while !br.is_null() && i < index {
            br = (*br).next;
            i += 1;
        }
        br
    }
}

unsafe fn bench_find(e: *mut ModEntry, name: *const u8, name_len: usize) -> *mut BenchEntry {
    unsafe {
        let mut br = (*e).benches;
        while !br.is_null() {
            if (*br).name.len as usize == name_len
                && span_eq((*br).name.ptr, name_len, name, name_len)
            {
                return br;
            }
            br = (*br).next;
        }
        core::ptr::null_mut()
    }
}

unsafe fn bench_append(e: *mut ModEntry, br: *mut BenchEntry) {
    unsafe {
        let head = (*e).benches;
        if head.is_null() {
            (*e).benches = br;
        } else {
            let mut last = head;
            while !(*last).next.is_null() {
                last = (*last).next;
            }
            (*last).next = br;
        }
        (*e).n_benches += 1;
    }
}

unsafe fn bench_replace(e: *mut ModEntry, old: *mut BenchEntry, fresh: *mut BenchEntry) {
    unsafe {
        (*fresh).next = (*old).next;
        if (*e).benches == old {
            (*e).benches = fresh;
        } else {
            let mut prev = (*e).benches;
            while !prev.is_null() && (*prev).next != old {
                prev = (*prev).next;
            }
            if !prev.is_null() {
                (*prev).next = fresh;
            }
        }
        bench_give(old);
    }
}

unsafe fn bench_chain_give(e: *mut ModEntry) {
    unsafe {
        let mut br = (*e).benches;
        while !br.is_null() {
            let nx = (*br).next;
            bench_give(br);
            br = nx;
        }
        (*e).benches = core::ptr::null_mut();
        (*e).n_benches = 0;
    }
}

/// Every row a module holds, back to its pool or its heap. A slot is only
/// ever reused through here, so a dead row never keeps memory.
unsafe fn entry_rows_give(e: *mut ModEntry) {
    unsafe {
        export_chain_give(e);
        test_chain_give(e);
        bench_chain_give(e);
    }
}

/// Record a refused registration into the loud-refusal ring (caller holds
/// the table lock). The ring is deduplicated: a repeated refusal (the
/// same fqn + name + reason) bumps `count` instead of consuming a row,
/// so a ctor that fires per-seat records once. FQN/name longer than the
/// row caps store their **prefix** — the row exists to name the culprit,
/// and the introspection face reports the true lengths alongside.
unsafe fn refusal_record(
    fqn: *const u8,
    fqn_len: usize,
    name: *const u8,
    name_len: usize,
    reason: u8,
) {
    unsafe {
        /* UnsafeCell<[Refusal; REF_MAX]>::get hands back a pointer to the
         * array; the ring walks its rows, so view it as the element
         * pointer (the [row; N] initializer shape is the rsx subset's
         * only static form — same posture as TABLE). */
        let ring = REFUSALS.0.get() as *mut Refusal;
        let n = *N_REFUSALS.0.get();
        let mut i = 0usize;
        while i < n as usize && i < REF_MAX {
            let r = ring.add(i);
            if (*r).reason == reason
                && (*r).fqn_len as usize == fqn_len
                && (*r).name_len as usize == name_len
                && span_eq((*r).fqn.as_ptr(), (*r).fqn_len as usize, fqn, fqn_len)
                && span_eq((*r).name.as_ptr(), (*r).name_len as usize, name, name_len)
            {
                (*r).count += 1;
                return;
            }
            i += 1;
        }
        if n as usize >= REF_MAX {
            /* ring full: refuse the loudest way the no-alloc design
             * allows — the newest refusal wins the last row. */
            i = REF_MAX - 1;
        } else {
            let r = ring.add(n as usize);
            (*r).reason = reason;
            (*r).count = 1;
            *N_REFUSALS.0.get() = n + 1;
            i = n as usize;
        }
        let r = ring.add(i);
        let fl = if fqn_len > FQN_CAP { FQN_CAP } else { fqn_len };
        let nl = if name_len > REF_NAME_CAP { REF_NAME_CAP } else { name_len };
        if !fqn.is_null() && fl > 0 {
            let _ = span_put((*r).fqn.as_mut_ptr(), FQN_CAP, fqn, fl);
            (*r).fqn_len = fqn_len as u32;
        } else {
            (*r).fqn_len = 0;
        }
        if !name.is_null() && nl > 0 {
            let _ = span_put((*r).name.as_mut_ptr(), REF_NAME_CAP, name, nl);
            (*r).name_len = name_len as u32;
        } else {
            (*r).name_len = 0;
        }
    }
}

/* ---- table ops (all under the caller-held lock) ---- */

/// First dead slot, or -1 — publish reuses holes so every other live
/// handle's index keeps meaning the same thing (see REGISTRY.md).
unsafe fn find_dead(t: *mut Table) -> i32 {
    // SAFETY: the caller holds the table lock and passes the live TABLE pointer;
    // n_entries is bounded by MOD_MAX, including when a new slot is reserved.
    unsafe {
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
}

unsafe fn entry_by_handle(
    t: *mut Table,
    handle: pm_wasmmod_registry_handle_t,
) -> *mut ModEntry {
    // SAFETY: the caller holds the table lock and passes TABLE; the index is
    // checked against both the initialized count and the fixed array capacity.
    unsafe {
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
}

unsafe fn entry_by_fqn(t: *mut Table, fqn: *const u8, fqn_len: usize) -> *mut ModEntry {
    // SAFETY: the caller holds the table lock, passes TABLE and a readable
    // fqn span; each row index stays within n_entries (at most MOD_MAX).
    unsafe {
        let mut i = 0usize;
        while i < (*t).n_entries as usize {
            let e = (*t).entries.as_mut_ptr().add(i);
            if (*e).live && (*e).fqn_len as usize == fqn_len {
                if span_eq((*e).fqn.as_ptr(), (*e).fqn_len as usize, fqn, fqn_len) {
                    return e;
                }
            }
            i += 1;
        }
        core::ptr::null_mut()
    }
}

unsafe fn entry_index(e: *mut ModEntry, t: *mut Table) -> u32 {
    // SAFETY: callers pass TABLE under its lock; e is either null or a row
    // returned from that table. The arithmetic below does not dereference e.
    let base = unsafe { (*t).entries.as_mut_ptr() };
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
    // SAFETY: find_dead reserved an index in TABLE below MOD_MAX; the caller
    // holds its lock and supplies a readable fqn span. The row remains live
    // through initialization and its previous chains are returned once.
    unsafe {
        let e = (*t).entries.as_mut_ptr().add(idx as usize);
        let stored = span_put((*e).fqn.as_mut_ptr(), FQN_CAP, fqn, fqn_len);
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
        /* A dead slot gave its rows back at unpublish, so this is a no-op
         * there and on a fresh slot; it is here because a reused slot must
         * start empty however it came to be free. */
        entry_rows_give(e);
        pm_wasmmod_registry_handle_t {
            index: idx as u32,
            generation: (*e).generation,
        }
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
            // SAFETY: entry_by_fqn returned a live row of the locked table.
            generation: unsafe { (*e).generation },
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
        /* loud refusal, never a silent dark face (see REF_* in budgets) */
        unsafe {
            refusal_record(
                (*e).fqn.as_ptr(),
                (*e).fqn_len as usize,
                name,
                name_len,
                if name_len > NAME_CAP {
                    REF_REASON_NAME_OVER_CAP
                } else {
                    REF_REASON_SIG_OVER_CAP
                },
            );
        }
        return false;
    }
    let old = unsafe { export_find(e, name, name_len) };
    if !old.is_null() {
        /* A re-registration keeps the row's place in the chain, and keeps
         * the sig it already had when the new one brings none. The bytes it
         * keeps are read out of the old row while that row is still alive:
         * the fresh row copies them before the old one goes back. */
        let mut keep = sig;
        let mut keep_len = sig_len;
        if sig_len == 0 {
            // SAFETY: export_find found a live row; keep its signature until
            // export_take copies the bytes, before export_replace frees it.
            unsafe {
                keep = (*old).sig.ptr;
                keep_len = (*old).sig.len as usize;
            }
        }
        let fresh = unsafe { export_take(name, name_len, keep, keep_len) };
        if fresh.is_null() {
            unsafe {
                refusal_record(
                    (*e).fqn.as_ptr(),
                    (*e).fqn_len as usize,
                    name,
                    name_len,
                    REF_REASON_STORE_FAIL,
                );
            }
            return false;
        }
        // SAFETY: export_take returned an owned, non-null row under the lock.
        unsafe {
            (*fresh).kind = kind;
            (*fresh).ptr = ptr;
        }
        unsafe { export_replace(e, old, fresh) };
        return true;
    }
    if !unsafe { knob_room(EXPORTS_KNOB.0.get(), *EXPORT_LIVE.0.get()) } {
        unsafe {
            refusal_record(
                (*e).fqn.as_ptr(),
                (*e).fqn_len as usize,
                name,
                name_len,
                REF_REASON_EXPORTS_FULL,
            );
        }
        return false;
    }
    let x = unsafe { export_take(name, name_len, sig, sig_len) };
    if x.is_null() {
        /* No row: the pre-heap pool is dry, or the seat's allocator said
         * no. Either way the registration is recorded, never dropped. */
        unsafe {
            refusal_record(
                (*e).fqn.as_ptr(),
                (*e).fqn_len as usize,
                name,
                name_len,
                REF_REASON_STORE_FAIL,
            );
        }
        return false;
    }
    // SAFETY: export_take returned a non-null row owned by the locked table.
    unsafe {
        (*x).kind = kind;
        (*x).ptr = ptr;
    }
    unsafe { export_append(e, x) };
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
    let x = unsafe { export_find(e, name, name_len) };
    if x.is_null() {
        return core::ptr::null_mut();
    }
    unsafe { (*x).ptr }
}

/* ---- C ABI face ---- */

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_init() {
    // TABLE's static zero-init is still the whole init; what is left to do
    // is the card's knobs, and only if no registration has arrived yet (a
    // constructor attaches them itself, long before any seat calls here).
    unsafe {
        pm_util_lock_acquire(lock());
        knobs_attach();
        pm_util_lock_release(lock());
    }
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
            entry_rows_give(e);
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
        if h.index == u32::MAX {
            /* module table full or fqn store failed — a loud refusal,
             * never a silent dark face */
            refusal_record(
                fqn_ptr,
                fqn_len as usize,
                name_ptr,
                name_len as usize,
                REF_REASON_MODULES_FULL,
            );
            pm_util_lock_release(lock());
            return 0;
        }
        let ok = export_set_sig(
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
        let x = if e.is_null() || index >= (*e).n_exports {
            core::ptr::null_mut()
        } else {
            export_at_ptr(e, index as usize)
        };
        if !x.is_null() {
            rc = copy_span_to_buf((*x).name.ptr, (*x).name.len as usize, name_buf, name_len_io);
            if rc == 1 {
                if !kind_out.is_null() {
                    *kind_out = (*x).kind;
                }
                if !sig_len_io.is_null() {
                    if (*x).sig.len > 0 {
                        rc = copy_span_to_buf(
                            (*x).sig.ptr,
                            (*x).sig.len as usize,
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

/* ---- loud-refusal introspection (manifest test + inspection faces) ---- */

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_refusal_count() -> u32 {
    unsafe {
        pm_util_lock_acquire(lock());
        let n = *N_REFUSALS.0.get();
        pm_util_lock_release(lock());
        n
    }
}

/// One refusal row: fqn + export name (true lengths in the *_len_io outs —
/// a row stores a prefix when a span overflows its row cap) + reason tag +
/// repeat count. Returns 1 on success, 0 when `index` is out of range.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_refusal_at(
    index: u32,
    fqn_buf: *mut u8,
    fqn_len_io: *mut u32,
    name_buf: *mut u8,
    name_len_io: *mut u32,
    reason_out: *mut u8,
    count_out: *mut u32,
) -> i32 {
    unsafe {
        pm_util_lock_acquire(lock());
        let n = *N_REFUSALS.0.get();
        let mut rc = 0;
        if index < n {
            let r = (REFUSALS.0.get() as *mut Refusal).add(index as usize);
            rc = copy_span_to_buf((*r).fqn.as_ptr(), (*r).fqn_len as usize, fqn_buf, fqn_len_io);
            if rc == 1 {
                rc = copy_span_to_buf(
                    (*r).name.as_ptr(),
                    (*r).name_len as usize,
                    name_buf,
                    name_len_io,
                );
            }
            if rc == 1 {
                if !reason_out.is_null() {
                    *reason_out = (*r).reason;
                }
                if !count_out.is_null() {
                    *count_out = (*r).count;
                }
            }
        }
        if rc == 0 {
            if !fqn_len_io.is_null() {
                *fqn_len_io = 0;
            }
            if !name_len_io.is_null() {
                *name_len_io = 0;
            }
        }
        pm_util_lock_release(lock());
        rc
    }
}

/// Test face: clear the ring. Never called by product code — the ring is
/// the persistent record of refused faces and only a test's setup resets it.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_refusal_reset() {
    unsafe {
        pm_util_lock_acquire(lock());
        *N_REFUSALS.0.get() = 0;
        pm_util_lock_release(lock());
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
        let slot = fn_slot_t { raw: p };
        let f = slot.fnptr;
        f(args, nargs, results, nresults)
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
            let e = (*t).entries.as_mut_ptr().add(i);
            if (*e).live {
                let mut x = (*e).exports;
                while !x.is_null() {
                    if (*x).kind == pm_wasmmod_registry_export_kind_t::Obj {
                        visit((*x).ptr, ctx);
                    }
                    x = (*x).next;
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
        ensure(
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
        let old = test_find(e, name, name_len);
        if !old.is_null() {
            /* Re-registration keeps the row's place, and keeps the export
             * it already had when the new registration brings none — read
             * out of the old row before it goes back. */
            let mut keep = export;
            let mut keep_len = export_len;
            if export_len == 0 {
                keep = (*old).export.ptr;
                keep_len = (*old).export.len as usize;
            }
            let fresh = test_take(name, name_len, keep, keep_len);
            if fresh.is_null() {
                return false;
            }
            (*fresh).body_kind = body_kind;
            (*fresh).native = native;
            test_replace(e, old, fresh);
            return true;
        }
        if !knob_room(TESTS_KNOB.0.get(), *TEST_LIVE.0.get()) {
            return false;
        }
        let tr = test_take(name, name_len, export, export_len);
        if tr.is_null() {
            return false;
        }
        (*tr).body_kind = body_kind;
        (*tr).native = native;
        test_append(e, tr);
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
        let old = bench_find(e, name, name_len);
        if !old.is_null() {
            let mut keep = export;
            let mut keep_len = export_len;
            if export_len == 0 {
                keep = (*old).export.ptr;
                keep_len = (*old).export.len as usize;
            }
            let fresh = bench_take(name, name_len, keep, keep_len);
            if fresh.is_null() {
                return false;
            }
            (*fresh).body_kind = body_kind;
            (*fresh).native = native;
            bench_replace(e, old, fresh);
            return true;
        }
        if !knob_room(BENCHES_KNOB.0.get(), *BENCH_LIVE.0.get()) {
            return false;
        }
        let br = bench_take(name, name_len, export, export_len);
        if br.is_null() {
            return false;
        }
        (*br).body_kind = body_kind;
        (*br).native = native;
        bench_append(e, br);
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
            let tr = test_at_ptr(e, index as usize);
            if !tr.is_null() {
                rc = copy_span_to_buf((*tr).name.ptr, (*tr).name.len as usize, buf, buf_len_io);
            }
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
    // SAFETY: the caller passes its initialized stack snapshot; the runner
    // cannot outlive that snapshot, and no lock is held while it executes.
    if unsafe { (*b).body_kind } == BODY_NATIVE {
        if let Some(f) = unsafe { (*b).native } {
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
            let tr = test_find(e, name_ptr, name_len as usize);
            if !tr.is_null() {
                body.body_kind = (*tr).body_kind;
                body.native = (*tr).native;
                body.export_len = (*tr).export.len;
                if (*tr).export.len > 0 && (*tr).export.len as usize <= NAME_CAP {
                    core::ptr::copy_nonoverlapping(
                        (*tr).export.ptr,
                        body.export.as_mut_ptr(),
                        (*tr).export.len as usize,
                    );
                }
                found = true;
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
            /* Walked from the head each lap on purpose: the lock is
             * released to run the body, and the body may register into
             * this very module. */
            let tr = test_at_ptr(e, i);
            if tr.is_null() {
                break;
            }
            let mut body = TestBody {
                body_kind: (*tr).body_kind,
                native: (*tr).native,
                fqn: [0; FQN_CAP],
                fqn_len: 0,
                export: [0; NAME_CAP],
                export_len: (*tr).export.len,
            };
            if (*tr).export.len > 0 && (*tr).export.len as usize <= NAME_CAP {
                core::ptr::copy_nonoverlapping(
                    (*tr).export.ptr,
                    body.export.as_mut_ptr(),
                    (*tr).export.len as usize,
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
/// provides this once at boot; metal wires it to `pm_metal_coop_mono_us`.
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
            let br = bench_at_ptr(e, index as usize);
            if !br.is_null() {
                rc = copy_span_to_buf((*br).name.ptr, (*br).name.len as usize, buf, buf_len_io);
            }
        }
        if rc == 0 && !buf_len_io.is_null() {
            *buf_len_io = 0;
        }
        pm_util_lock_release(lock());
        rc
    }
}

unsafe fn run_bench_body(b: *const BenchBody, iterations: u64) -> i32 {
    // SAFETY: the caller passes its initialized stack snapshot; the runner
    // cannot outlive that snapshot, and no lock is held while it executes.
    if unsafe { (*b).body_kind } == BODY_NATIVE {
        if let Some(f) = unsafe { (*b).native } {
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
            let br = bench_find(e, name_ptr, name_len as usize);
            if !br.is_null() {
                body.body_kind = (*br).body_kind;
                body.native = (*br).native;
                body.export_len = (*br).export.len;
                if (*br).export.len > 0 && (*br).export.len as usize <= NAME_CAP {
                    core::ptr::copy_nonoverlapping(
                        (*br).export.ptr,
                        body.export.as_mut_ptr(),
                        (*br).export.len as usize,
                    );
                }
                found = true;
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
    // SAFETY: caller holds the table lock and passes TABLE.
    unsafe { (*t).out_len = 0 };
}

unsafe fn out_put(t: *mut Table, s: *const u8, n: usize) {
    // SAFETY: caller holds the table lock and passes TABLE with out_len <=
    // OUT_CAP; the capacity check keeps writes inside out, and s has n bytes.
    unsafe {
        let at = (*t).out_len as usize;
        if at + n > OUT_CAP {
            return;
        }
        let mut i = 0usize;
        while i < n {
            *(*t).out.as_mut_ptr().add(at + i) = *s.add(i);
            i += 1;
        }
        (*t).out_len += n as u32;
    }
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
            let br = bench_at_ptr(e, i);
            if br.is_null() {
                break;
            }
            let mut body = BenchBody {
                body_kind: (*br).body_kind,
                native: (*br).native,
                fqn: [0; FQN_CAP],
                fqn_len: 0,
                export: [0; NAME_CAP],
                export_len: (*br).export.len,
            };
            if (*br).export.len > 0 && (*br).export.len as usize <= NAME_CAP {
                core::ptr::copy_nonoverlapping(
                    (*br).export.ptr,
                    body.export.as_mut_ptr(),
                    (*br).export.len as usize,
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
            let br2 = bench_at_ptr(e, i);
            if !br2.is_null() {
                out_put(t, (*br2).name.ptr, (*br2).name.len as usize);
            }
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
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_refusal_count, "uint32_t(void)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_refusal_at, "int32_t(uint32_t, uint8_t *, uint32_t *, uint8_t *, uint32_t *, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_refusal_reset, "void(void)");
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
