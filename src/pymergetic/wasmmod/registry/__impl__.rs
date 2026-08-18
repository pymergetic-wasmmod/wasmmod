//! pymergetic.wasmmod.registry — the one source of truth for native
//! module identity + exports. `sys.modules` stays the Python import
//! face; this table is what backs every native lookup underneath it,
//! for every impl language and every call direction. See
//! docs/REGISTRY.md for the design this implements.

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

// `alloc` is declared once, crate-root, in lib.rs — resolvable from any
// module in this crate without a local `extern crate alloc;` here.
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::c_void;

use crate::util::lock::Mutex;

/// Which kind of artifact a module's exports live behind. `Resident`
/// covers a statically-linked-in C/Rust module with no container at all
/// — this is what makes wasm "one-of" alongside elf/aot, not a hierarchy
/// wasm sits above.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum pm_wasmmod_registry_container_kind_t {
    Wasm = 0,
    Aot = 1,
    Elf = 2,
    Resident = 3,
}

/// Marshaling shape for one export — same taxonomy already used for the
/// Python export faces (see SOURCETREE.md "Py export face"), reused here
/// rather than invented twice: whatever's true for a Python-visible
/// export is true for every export, this is the one place that's real.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum pm_wasmmod_registry_export_kind_t {
    Fn = 0,
    Mem = 1,
    Obj = 2,
    I64 = 3,
    F32 = 4,
    F64 = 5,
    BufPtr = 6,
}

/// A handle into the table: index + generation. Never treat the index
/// as a direct offset from outside this module — always go through
/// resolve/has/export_set, which check the generation and reject a
/// handle whose slot has since been unpublished and reused. Passed by
/// value everywhere, not a pointer — a stale copy is always safely
/// detectable, never a dangling-pointer footgun.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct pm_wasmmod_registry_handle_t {
    pub index: u32,
    pub generation: u32,
}

const INVALID_HANDLE: pm_wasmmod_registry_handle_t = pm_wasmmod_registry_handle_t {
    index: u32::MAX,
    generation: 0,
};

/// The four primitive shapes a value crossing a container boundary can
/// be — deliberately the same four as wasm's own core value types
/// (WAMR's own `wasm_val_t` uses this exact kind+union shape), not a
/// separate encoding invented for the registry. See `__types__.h`.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum pm_wasmmod_registry_valkind_t {
    I32 = 0,
    I64 = 1,
    F32 = 2,
    F64 = 3,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union pm_wasmmod_registry_value_of_t {
    pub i32: i32,
    pub i64: i64,
    pub f32: f32,
    pub f64: f64,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_wasmmod_registry_value_t {
    pub kind: pm_wasmmod_registry_valkind_t,
    pub of: pm_wasmmod_registry_value_of_t,
}

/// The fixed prototype every cross-container `Fn` export conforms to
/// once resolved via `resolve_native`/`pm_wasmmod_registry_call`.
/// Same-artifact native-to-native calls never go through this — those
/// stay a direct, really-typed function pointer via `connect_import`.
pub type pm_wasmmod_registry_fn_t = unsafe extern "C" fn(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32;

struct Export {
    name: String,
    kind: pm_wasmmod_registry_export_kind_t,
    /// `Fn`/`Mem`/`I64`/`F32`/`F64`/`BufPtr`: a real native pointer.
    /// `Obj`: an opaque token owned by whichever embedder eventually
    /// attaches a GC (see `gc_visit`) — the registry never interprets
    /// it, only stores and hands it back.
    ptr: *mut c_void,
    /// Optional C prototype spelling for facegen (e.g. `int(void)`).
    sig: Option<String>,
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

enum TestBody {
    /// Host / resident: real `fn() -> i32` from `PM_MOD_TEST_*`.
    Native(pm_wasmmod_registry_test_fn_t),
    /// Guest pack: wasm export symbol; invoke via [`WASM_TEST_RUNNER`].
    WasmExport(String),
}

struct TestEntry {
    name: String,
    body: TestBody,
}

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

enum BenchBody {
    /// Host / resident: a real `fn(iterations) -> i32`.
    Native(pm_wasmmod_registry_bench_fn_t),
    /// Guest pack: wasm export symbol taking the iterations word.
    WasmExport(String),
}

struct BenchEntry {
    name: String,
    body: BenchBody,
}

/// How many repeats to run before measuring — a cheap warmup that lets the
/// runner park/schedule before we start counting, the same spirit as the
/// DHCP/SSH "drive the wire" loops.
const BENCH_MEASURE_WARMUP: u64 = 2;

static WASM_TEST_RUNNER: Mutex<Option<pm_wasmmod_registry_wasm_test_runner_t>> = Mutex::new(None);

/// Loader installs this so guest pack benches run without a host trampoline
/// pool; a guest bench is `WasmExport` + `iterations`, driven by this hook.
static WASM_BENCH_RUNNER: Mutex<Option<pm_wasmmod_registry_wasm_bench_runner_t>> =
    Mutex::new(None);

/// Installed by the seat that can time. Stays `None` on clockless seats so a
/// bench reports "no clock" rather than a bogus number (see the clock doc).
static BENCH_CLOCK: Mutex<Option<pm_wasmmod_registry_bench_clock_t>> = Mutex::new(None);

struct ModEntry {
    fqn: String,
    /// Package / kernel version string (empty = unset). Build-root packs and
    /// host-intrinsic modules that are independently dependable.
    version: String,
    container: pm_wasmmod_registry_container_kind_t,
    generation: u32,
    exports: Vec<Export>,
    tests: Vec<TestEntry>,
    benches: Vec<BenchEntry>,
    live: bool,
}

/// How many `PM_MOD_EXPORT_*` registrations can be held before the heap
/// exists. Around 330 across wasmmod's own C/RS cards and a downstream card
/// tree today; the spare is headroom.
const STAGE_MAX: usize = 512;

/// One `PM_MOD_EXPORT_*` registration captured before the allocator is
/// usable. Every field the macros pass is already static storage — string
/// literals and a function address — so staging one costs no allocation.
/// Addresses are held as `usize` so the struct stays plain-data and the
/// enclosing static needs no extra unsafe auto-trait promises.
#[derive(Clone, Copy)]
struct StagedExport {
    fqn: usize,
    fqn_len: u32,
    name: usize,
    name_len: u32,
    sig: usize,
    sig_len: u32,
    ptr: usize,
    kind: pm_wasmmod_registry_export_kind_t,
}

struct Table {
    entries: Vec<ModEntry>,
    /// Registrations that arrived from a constructor. Freestanding seats run
    /// `.init_array` / `.CRT$XCU` before any heap exists (firmware `malloc` is
    /// often a bump pointer that is still NULL at that point), so an
    /// insert here would allocate against nothing. They are replayed by
    /// `drain_staged` on the first read, once the heap is up.
    staged: [StagedExport; STAGE_MAX],
    nstaged: usize,
    /// Registrations dropped because `staged` was full and no heap was
    /// available to take them directly. Nonzero means STAGE_MAX is too small.
    ndropped: u32,
}

// SAFETY: the raw pointers `Table` holds (`Export::ptr`) are opaque
// addresses — native function pointers or embedder-owned tokens — never
// thread-affine data, and every access goes through `TABLE`'s `Mutex`
// (one accessor at a time). Send is the real requirement here; `Mutex`
// derives its own `Sync` from it (see pymergetic.util.lock).
unsafe impl Send for Table {}

impl Table {
    const fn new() -> Self {
        Self {
            entries: Vec::new(),
            staged: [StagedExport {
                fqn: 0,
                fqn_len: 0,
                name: 0,
                name_len: 0,
                sig: 0,
                sig_len: 0,
                ptr: 0,
                kind: pm_wasmmod_registry_export_kind_t::Fn,
            }; STAGE_MAX],
            nstaged: 0,
            ndropped: 0,
        }
    }

    /// Record a constructor-time registration without touching the heap.
    /// Returns false only when the buffer is full, so the caller can fall
    /// back to a direct insert on a seat that does have an allocator.
    fn stage_export(
        &mut self,
        fqn: &str,
        name: &str,
        kind: pm_wasmmod_registry_export_kind_t,
        ptr: *mut c_void,
        sig: Option<&str>,
    ) -> bool {
        if self.nstaged >= STAGE_MAX {
            return false;
        }
        let (sig_ptr, sig_len) = match sig {
            Some(s) => (s.as_ptr() as usize, s.len() as u32),
            None => (0usize, 0u32),
        };
        self.staged[self.nstaged] = StagedExport {
            fqn: fqn.as_ptr() as usize,
            fqn_len: fqn.len() as u32,
            name: name.as_ptr() as usize,
            name_len: name.len() as u32,
            sig: sig_ptr,
            sig_len,
            ptr: ptr as usize,
            kind,
        };
        self.nstaged += 1;
        true
    }

    /// Replay everything staged before the heap existed. Called on the first
    /// read of the table, by which point an allocator is always up.
    fn drain_staged(&mut self) {
        if self.nstaged == 0 {
            return;
        }
        let n = core::mem::replace(&mut self.nstaged, 0);
        for i in 0..n {
            let s = self.staged[i];
            // SAFETY: every address came from a `&str` backed by a string
            // literal in the image, so the bytes outlive the whole program.
            let (Some(fqn), Some(name)) = (
                str_from_raw(s.fqn as *const u8, s.fqn_len),
                str_from_raw(s.name as *const u8, s.name_len),
            ) else {
                continue;
            };
            let sig = if s.sig_len == 0 {
                None
            } else {
                str_from_raw(s.sig as *const u8, s.sig_len)
            };
            let handle = self.ensure_resident(fqn);
            self.export_set_sig(handle, name, s.kind, s.ptr as *mut c_void, sig);
        }
    }

    fn publish(
        &mut self,
        fqn: &str,
        container: pm_wasmmod_registry_container_kind_t,
    ) -> pm_wasmmod_registry_handle_t {
        // Reuse the first dead slot if one exists — unpublish leaves a
        // hole rather than shifting indices, since every other live
        // handle's index has to keep meaning the same thing.
        if let Some((index, entry)) = self.entries.iter_mut().enumerate().find(|(_, e)| !e.live) {
            let generation = entry.generation.wrapping_add(1);
            *entry = ModEntry {
                fqn: String::from(fqn),
                version: String::new(),
                container,
                generation,
                exports: Vec::new(),
                tests: Vec::new(),
                benches: Vec::new(),
                live: true,
            };
            return pm_wasmmod_registry_handle_t {
                index: index as u32,
                generation,
            };
        }
        let index = self.entries.len() as u32;
        self.entries.push(ModEntry {
            fqn: String::from(fqn),
            version: String::new(),
            container,
            generation: 0,
            exports: Vec::new(),
            tests: Vec::new(),
            benches: Vec::new(),
            live: true,
        });
        pm_wasmmod_registry_handle_t {
            index,
            generation: 0,
        }
    }

    fn unpublish(&mut self, handle: pm_wasmmod_registry_handle_t) -> bool {
        match self.live_entry_mut(handle) {
            Some(entry) => {
                entry.live = false;
                entry.exports.clear();
                entry.tests.clear();
                entry.benches.clear();
                entry.fqn.clear();
                entry.version.clear();
                true
            }
            None => false,
        }
    }

    fn live_entry_mut(&mut self, handle: pm_wasmmod_registry_handle_t) -> Option<&mut ModEntry> {
        self.entries
            .get_mut(handle.index as usize)
            .filter(|e| e.live && e.generation == handle.generation)
    }

    fn find_by_fqn(&self, fqn: &str) -> Option<&ModEntry> {
        self.entries.iter().find(|e| e.live && e.fqn == fqn)
    }

    fn export_set(
        &mut self,
        handle: pm_wasmmod_registry_handle_t,
        name: &str,
        kind: pm_wasmmod_registry_export_kind_t,
        ptr: *mut c_void,
    ) -> bool {
        self.export_set_sig(handle, name, kind, ptr, None)
    }

    fn export_set_sig(
        &mut self,
        handle: pm_wasmmod_registry_handle_t,
        name: &str,
        kind: pm_wasmmod_registry_export_kind_t,
        ptr: *mut c_void,
        sig: Option<&str>,
    ) -> bool {
        let Some(entry) = self.live_entry_mut(handle) else {
            return false;
        };
        let sig_owned = sig.map(String::from);
        if let Some(export) = entry.exports.iter_mut().find(|e| e.name == name) {
            export.kind = kind;
            export.ptr = ptr;
            if sig_owned.is_some() {
                export.sig = sig_owned;
            }
        } else {
            entry.exports.push(Export {
                name: String::from(name),
                kind,
                ptr,
                sig: sig_owned,
            });
        }
        true
    }

    /// Publish if missing; if already live, keep the existing container.
    fn ensure(
        &mut self,
        fqn: &str,
        container: pm_wasmmod_registry_container_kind_t,
    ) -> pm_wasmmod_registry_handle_t {
        if let Some((index, entry)) = self
            .entries
            .iter()
            .enumerate()
            .find(|(_, e)| e.live && e.fqn == fqn)
        {
            return pm_wasmmod_registry_handle_t {
                index: index as u32,
                generation: entry.generation,
            };
        }
        self.publish(fqn, container)
    }

    fn ensure_resident(&mut self, fqn: &str) -> pm_wasmmod_registry_handle_t {
        self.ensure(fqn, pm_wasmmod_registry_container_kind_t::Resident)
    }

    fn container_of(&self, fqn: &str) -> Option<pm_wasmmod_registry_container_kind_t> {
        self.find_by_fqn(fqn).map(|e| e.container)
    }

    fn set_version(&mut self, fqn: &str, version: &str) -> bool {
        match self.entries.iter_mut().find(|e| e.live && e.fqn == fqn) {
            Some(e) => {
                e.version = String::from(version);
                true
            }
            None => false,
        }
    }

    fn version_of(&self, fqn: &str) -> Option<&str> {
        self.find_by_fqn(fqn)
            .map(|e| e.version.as_str())
            .filter(|s| !s.is_empty())
    }

    fn publish_ver(
        &mut self,
        fqn: &str,
        container: pm_wasmmod_registry_container_kind_t,
        version: &str,
    ) -> pm_wasmmod_registry_handle_t {
        let h = self.publish(fqn, container);
        if !version.is_empty() {
            let _ = self.set_version(fqn, version);
        }
        h
    }

    /// Register (or replace) a host/resident module test case. Ensures Resident.
    fn test_register(&mut self, fqn: &str, name: &str, f: pm_wasmmod_registry_test_fn_t) -> bool {
        let _ = self.ensure_resident(fqn);
        let Some(entry) = self.entries.iter_mut().find(|e| e.live && e.fqn == fqn) else {
            return false;
        };
        let body = TestBody::Native(f);
        if let Some(t) = entry.tests.iter_mut().find(|t| t.name == name) {
            t.body = body;
        } else {
            entry.tests.push(TestEntry {
                name: String::from(name),
                body,
            });
        }
        true
    }

    /// Register (or replace) a guest pack test: case name → wasm export symbol.
    fn test_register_wasm(&mut self, fqn: &str, name: &str, export: &str) -> bool {
        let Some(entry) = self.entries.iter_mut().find(|e| e.live && e.fqn == fqn) else {
            return false;
        };
        let body = TestBody::WasmExport(String::from(export));
        if let Some(t) = entry.tests.iter_mut().find(|t| t.name == name) {
            t.body = body;
        } else {
            entry.tests.push(TestEntry {
                name: String::from(name),
                body,
            });
        }
        true
    }

    fn test_count(&self, fqn: &str) -> u32 {
        self.find_by_fqn(fqn)
            .map(|e| e.tests.len() as u32)
            .unwrap_or(0)
    }

    fn test_name_at(&self, fqn: &str, index: u32) -> Option<&str> {
        self.find_by_fqn(fqn)
            .and_then(|e| e.tests.get(index as usize))
            .map(|t| t.name.as_str())
    }

    fn test_body_named(&self, fqn: &str, name: &str) -> Option<&TestBody> {
        self.find_by_fqn(fqn)
            .and_then(|e| e.tests.iter().find(|t| t.name == name))
            .map(|t| &t.body)
    }

    /// Register (or replace) a host/resident module benchmark. Ensures Resident.
    fn bench_register(&mut self, fqn: &str, name: &str, f: pm_wasmmod_registry_bench_fn_t) -> bool {
        let _ = self.ensure_resident(fqn);
        let Some(entry) = self.entries.iter_mut().find(|e| e.live && e.fqn == fqn) else {
            return false;
        };
        let body = BenchBody::Native(f);
        if let Some(b) = entry.benches.iter_mut().find(|b| b.name == name) {
            b.body = body;
        } else {
            entry.benches.push(BenchEntry {
                name: String::from(name),
                body,
            });
        }
        true
    }

    /// Register (or replace) a guest pack benchmark: name → wasm export symbol.
    fn bench_register_wasm(&mut self, fqn: &str, name: &str, export: &str) -> bool {
        let Some(entry) = self.entries.iter_mut().find(|e| e.live && e.fqn == fqn) else {
            return false;
        };
        let body = BenchBody::WasmExport(String::from(export));
        if let Some(b) = entry.benches.iter_mut().find(|b| b.name == name) {
            b.body = body;
        } else {
            entry.benches.push(BenchEntry {
                name: String::from(name),
                body,
            });
        }
        true
    }

    fn bench_count(&self, fqn: &str) -> u32 {
        self.find_by_fqn(fqn)
            .map(|e| e.benches.len() as u32)
            .unwrap_or(0)
    }

    fn bench_name_at(&self, fqn: &str, index: u32) -> Option<&str> {
        self.find_by_fqn(fqn)
            .and_then(|e| e.benches.get(index as usize))
            .map(|b| b.name.as_str())
    }

    fn bench_body_named(&self, fqn: &str, name: &str) -> Option<&BenchBody> {
        self.find_by_fqn(fqn)
            .and_then(|e| e.benches.iter().find(|b| b.name == name))
            .map(|b| &b.body)
    }

    fn live_module_count(&self) -> u32 {
        self.entries.iter().filter(|e| e.live).count() as u32
    }

    /// Indexed access to the N-th live module FQN. Allocation-free: called from
    /// C host-face walks (import-time) on seats where the backing allocator is
    /// a one-shot bump shared with the WASM guest — a per-query snapshot Vec
    /// here allocates on every module_at/count and can fail mid-import when the
    /// slab is under WASM+CDN pressure (firmware prove OSError / hang).
    fn live_module_at(&self, index: u32) -> Option<&str> {
        self.entries
            .iter()
            .filter(|e| e.live)
            .nth(index as usize)
            .map(|e| e.fqn.as_str())
    }

    fn exports_of(&self, fqn: &str) -> Option<&[Export]> {
        self.find_by_fqn(fqn).map(|e| e.exports.as_slice())
    }

    fn resolve_native(&self, fqn: &str, export_name: &str) -> *mut c_void {
        match self.find_by_fqn(fqn) {
            Some(entry) => entry
                .exports
                .iter()
                .find(|e| e.name == export_name)
                .map(|e| e.ptr)
                .unwrap_or(core::ptr::null_mut()),
            None => core::ptr::null_mut(),
        }
    }

    fn gc_visit(&self, visit: extern "C" fn(*mut c_void, *mut c_void), ctx: *mut c_void) {
        for entry in self.entries.iter().filter(|e| e.live) {
            for export in entry
                .exports
                .iter()
                .filter(|e| e.kind == pm_wasmmod_registry_export_kind_t::Obj)
            {
                visit(export.ptr, ctx);
            }
        }
    }
}

/// The table itself. `Mutex` (== `SpinLock`, see pymergetic.util.lock) —
/// registry access is expected under multithreaded *and* cooperative
/// scheduling (a one-runner-per-CPU kernel included), so a real
/// lock is load-bearing here, not decoration.
static TABLE_CELL: Mutex<Table> = Mutex::new(Table::new());

/// Handle to the one registry table. `lock()` replays constructor-time
/// registrations before handing the table over, so no reader can observe a
/// half-populated registry and no future call site has to remember to.
struct TableRef;

static TABLE: TableRef = TableRef;

impl TableRef {
    fn lock(&self) -> crate::util::lock::MutexGuard<'static, Table> {
        let mut guard = TABLE_CELL.lock();
        guard.drain_staged();
        guard
    }

    /// The table without a replay. Only the staging path may use this: it runs
    /// from a constructor, where draining would allocate against a heap that
    /// does not exist yet.
    fn lock_staging(&self) -> crate::util::lock::MutexGuard<'static, Table> {
        TABLE_CELL.lock()
    }
}

fn str_from_raw<'a>(ptr: *const u8, len: u32) -> Option<&'a str> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: caller (a extern "C" fn below) contracts ptr/len to
    // describe a valid, live byte range for the duration of this call.
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
    core::str::from_utf8(bytes).ok()
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_init() {
    // Nothing to do yet beyond TABLE's own static initialization — kept
    // as an explicit entry point so callers don't need to know that, and
    // so a future per-arena/per-pack init story has somewhere to land
    // without a new symbol.
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_publish(
    fqn_ptr: *const u8,
    fqn_len: u32,
    container: pm_wasmmod_registry_container_kind_t,
) -> pm_wasmmod_registry_handle_t {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return INVALID_HANDLE;
    };
    TABLE.lock().publish(fqn, container)
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_unpublish(handle: pm_wasmmod_registry_handle_t) -> i32 {
    TABLE.lock().unpublish(handle) as i32
}

/// Container kind for a live module, or `-1` if the fqn is not published.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_container(
    fqn_ptr: *const u8,
    fqn_len: u32,
) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return -1;
    };
    match TABLE.lock().container_of(fqn) {
        Some(k) => k as i32,
        None => -1,
    }
}

/// Publish `fqn` with `container` if missing; no-op (keeps kind) if already live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_ensure(
    fqn_ptr: *const u8,
    fqn_len: u32,
    container: pm_wasmmod_registry_container_kind_t,
) -> pm_wasmmod_registry_handle_t {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return INVALID_HANDLE;
    };
    TABLE.lock().ensure(fqn, container)
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return INVALID_HANDLE;
    };
    let ver = if ver_len == 0 {
        ""
    } else {
        str_from_raw(ver_ptr, ver_len).unwrap_or("")
    };
    TABLE.lock().publish_ver(fqn, container, ver)
}

/// Set / replace the version on a live module. Returns 1 on success, 0 if missing.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_version(
    fqn_ptr: *const u8,
    fqn_len: u32,
    ver_ptr: *const u8,
    ver_len: u32,
) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let Some(ver) = str_from_raw(ver_ptr, ver_len) else {
        return 0;
    };
    TABLE.lock().set_version(fqn, ver) as i32
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let table = TABLE.lock();
    let Some(ver) = table.version_of(fqn) else {
        if !buf_len_io.is_null() {
            unsafe { *buf_len_io = 0 };
        }
        return 0;
    };
    copy_str_to_buf(ver, buf, buf_len_io)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_has(fqn_ptr: *const u8, fqn_len: u32) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    TABLE.lock().find_by_fqn(fqn).is_some() as i32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_export_set(
    handle: pm_wasmmod_registry_handle_t,
    name_ptr: *const u8,
    name_len: u32,
    kind: pm_wasmmod_registry_export_kind_t,
    ptr: *mut c_void,
) -> i32 {
    let Some(name) = str_from_raw(name_ptr, name_len) else {
        return 0;
    };
    TABLE.lock().export_set(handle, name, kind, ptr) as i32
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
    let Some(name) = str_from_raw(name_ptr, name_len) else {
        return 0;
    };
    let sig = if sig_len == 0 {
        None
    } else {
        str_from_raw(sig_ptr, sig_len)
    };
    TABLE
        .lock()
        .export_set_sig(handle, name, kind, ptr, sig) as i32
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
    let (Some(fqn), Some(name)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
    ) else {
        return 0;
    };
    let sig = if sig_len == 0 {
        None
    } else {
        str_from_raw(sig_ptr, sig_len)
    };
    // Reached from a constructor on every seat, which on freestanding images
    // is before any allocator exists. Stage the plain-data record now and let
    // the first reader replay it; only fall back to allocating inline when the
    // staging buffer is full.
    let mut guard = TABLE.lock_staging();
    if guard.stage_export(fqn, name, kind, ptr, sig) {
        return 1;
    }
    guard.drain_staged();
    let handle = guard.ensure_resident(fqn);
    let ok = guard.export_set_sig(handle, name, kind, ptr, sig);
    if !ok {
        guard.ndropped = guard.ndropped.saturating_add(1);
    }
    ok as i32
}

fn copy_str_to_buf(src: &str, buf: *mut u8, buf_len_io: *mut u32) -> i32 {
    if buf_len_io.is_null() {
        return 0;
    }
    let need = src.len();
    if need == 0 {
        unsafe { *buf_len_io = 0 };
        return 1;
    }
    let cap = unsafe { *buf_len_io } as usize;
    if buf.is_null() || cap < need {
        unsafe { *buf_len_io = need as u32 };
        return 0;
    }
    unsafe {
        core::ptr::copy_nonoverlapping(src.as_ptr(), buf, need);
        *buf_len_io = need as u32;
    }
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_module_count() -> u32 {
    TABLE.lock().live_module_count()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_module_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    let table = TABLE.lock();
    let Some(fqn) = table.live_module_at(index) else {
        return 0;
    };
    copy_str_to_buf(fqn, buf, buf_len_io)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_export_count(
    fqn_ptr: *const u8,
    fqn_len: u32,
) -> u32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    TABLE
        .lock()
        .exports_of(fqn)
        .map(|e| e.len() as u32)
        .unwrap_or(0)
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let table = TABLE.lock();
    let Some(exports) = table.exports_of(fqn) else {
        return 0;
    };
    let Some(export) = exports.get(index as usize) else {
        return 0;
    };
    if copy_str_to_buf(&export.name, name_buf, name_len_io) == 0 {
        return 0;
    }
    if !kind_out.is_null() {
        unsafe { *kind_out = export.kind };
    }
    if !sig_len_io.is_null() {
        let sig = export.sig.as_deref().unwrap_or("");
        if copy_str_to_buf(sig, sig_buf, sig_len_io) == 0 && !sig.is_empty() {
            return 0;
        }
    }
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_resolve_native(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_name_ptr: *const u8,
    export_name_len: u32,
) -> *mut c_void {
    let (Some(fqn), Some(export_name)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(export_name_ptr, export_name_len),
    ) else {
        return core::ptr::null_mut();
    };
    TABLE.lock().resolve_native(fqn, export_name)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_connect_import(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_name_ptr: *const u8,
    export_name_len: u32,
    out_slot: *mut *mut c_void,
) -> i32 {
    let (Some(fqn), Some(export_name)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(export_name_ptr, export_name_len),
    ) else {
        return 0;
    };
    let ptr = TABLE.lock().resolve_native(fqn, export_name);
    if ptr.is_null() {
        return 0;
    }
    unsafe { *out_slot = ptr };
    1
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
    let (Some(fqn), Some(export_name)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(export_name_ptr, export_name_len),
    ) else {
        return -1;
    };
    let ptr = TABLE.lock().resolve_native(fqn, export_name);
    if ptr.is_null() {
        return -1;
    }
    // SAFETY: every ptr stored under PM_WASMMOD_REGISTRY_EXPORT_FN is
    // contracted (by whoever called export_set — the loader, for wasm
    // exports) to be a pm_wasmmod_registry_fn_t. The registry itself
    // never calls through a ptr of any other export kind.
    let f: pm_wasmmod_registry_fn_t = unsafe { core::mem::transmute(ptr) };
    unsafe { f(args, nargs, results, nresults) }
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_gc_visit(
    visit: extern "C" fn(*mut c_void, *mut c_void),
    ctx: *mut c_void,
) {
    TABLE.lock().gc_visit(visit, ctx);
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
    let (Some(fqn), Some(name), Some(f)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
        f,
    ) else {
        return 0;
    };
    TABLE.lock().test_register(fqn, name, f) as i32
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
    let (Some(fqn), Some(name), Some(export)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
        str_from_raw(export_ptr, export_len),
    ) else {
        return 0;
    };
    TABLE.lock().test_register_wasm(fqn, name, export) as i32
}

/// Loader installs this so guest pack tests run without a trampoline pool.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_wasm_test_runner(
    f: Option<pm_wasmmod_registry_wasm_test_runner_t>,
) {
    *WASM_TEST_RUNNER.lock() = f;
}

/// Loader installs this so guest pack benches run without a host trampoline
/// pool. Passing `None` clears the fill, mirroring `set_wasm_test_runner`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_set_wasm_bench_runner(
    f: Option<pm_wasmmod_registry_wasm_bench_runner_t>,
) {
    *WASM_BENCH_RUNNER.lock() = f;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_count(fqn_ptr: *const u8, fqn_len: u32) -> u32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    TABLE.lock().test_count(fqn)
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let table = TABLE.lock();
    let Some(name) = table.test_name_at(fqn, index) else {
        if !buf_len_io.is_null() {
            unsafe { *buf_len_io = 0 };
        }
        return 0;
    };
    copy_str_to_buf(name, buf, buf_len_io)
}

fn run_test_body(fqn: &str, body: &TestBody) -> i32 {
    match body {
        TestBody::Native(f) => unsafe { f() },
        TestBody::WasmExport(export) => {
            let Some(runner) = *WASM_TEST_RUNNER.lock() else {
                return -1;
            };
            unsafe {
                runner(
                    fqn.as_ptr(),
                    fqn.len() as u32,
                    export.as_ptr(),
                    export.len() as u32,
                )
            }
        }
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
    let (Some(fqn), Some(name)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
    ) else {
        return -1;
    };
    let body = {
        let table = TABLE.lock();
        match table.test_body_named(fqn, name) {
            Some(TestBody::Native(f)) => TestBody::Native(*f),
            Some(TestBody::WasmExport(e)) => TestBody::WasmExport(e.clone()),
            None => return -1,
        }
    };
    run_test_body(fqn, &body)
}

/// Run every test for `fqn`. Returns number of failures (missing module → 0).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_test_run_all(fqn_ptr: *const u8, fqn_len: u32) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let bodies: Vec<TestBody> = {
        let table = TABLE.lock();
        let Some(entry) = table.find_by_fqn(fqn) else {
            return 0;
        };
        entry
            .tests
            .iter()
            .map(|t| match &t.body {
                TestBody::Native(f) => TestBody::Native(*f),
                TestBody::WasmExport(e) => TestBody::WasmExport(e.clone()),
            })
            .collect()
    };
    let mut fails = 0i32;
    for body in &bodies {
        if run_test_body(fqn, body) != 0 {
            fails += 1;
        }
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
    *BENCH_CLOCK.lock() = f;
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
    let (Some(fqn), Some(name), Some(f)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
        f,
    ) else {
        return 0;
    };
    TABLE.lock().bench_register(fqn, name, f) as i32
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
    let (Some(fqn), Some(name), Some(export)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
        str_from_raw(export_ptr, export_len),
    ) else {
        return 0;
    };
    TABLE.lock().bench_register_wasm(fqn, name, export) as i32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_bench_count(fqn_ptr: *const u8, fqn_len: u32) -> u32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    TABLE.lock().bench_count(fqn)
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let table = TABLE.lock();
    let Some(name) = table.bench_name_at(fqn, index) else {
        if !buf_len_io.is_null() {
            unsafe { *buf_len_io = 0 };
        }
        return 0;
    };
    copy_str_to_buf(name, buf, buf_len_io)
}

fn run_bench_body(fqn: &str, body: &BenchBody, iterations: u64) -> i32 {
    match body {
        BenchBody::Native(f) => unsafe { f(iterations) },
        BenchBody::WasmExport(export) => {
            let Some(runner) = *WASM_BENCH_RUNNER.lock() else {
                // No loader to call the wasm export: a real missing-fill, not a
                // silent pass. Same triage as a clockless seat reporting
                // "no clock" — the guest bench can't be timed without a path to
                // its export, so it surfaces as FAILED rather than a fake number.
                return -1;
            };
            unsafe {
                runner(
                    fqn.as_ptr(),
                    fqn.len() as u32,
                    export.as_ptr(),
                    export.len() as u32,
                    iterations,
                )
            }
        }
    }
}

fn bench_now() -> Option<u64> {
    let clock = (*BENCH_CLOCK.lock())?;
    Some(unsafe { clock() })
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
fn time_bench_ns(fqn: &str, body: &BenchBody, iterations: u64) -> i64 {
    if iterations == 0 {
        return BENCH_RC_FAILED;
    }
    if bench_now().is_none() {
        return BENCH_RC_NO_CLOCK;
    }
    // Warmup: a small batch lets the runner park/schedule/init before timing,
    // so the measured lap starts warm. Failing warmup is not the bench's fault
    // unless it is genuinely broken; still surface the failure via FAILED.
    if run_bench_body(fqn, body, BENCH_MEASURE_WARMUP) != 0 {
        return BENCH_RC_FAILED;
    }
    let t0 = match bench_now() {
        Some(t) => t,
        None => return BENCH_RC_NO_CLOCK,
    };
    if run_bench_body(fqn, body, iterations) != 0 {
        return BENCH_RC_FAILED;
    }
    let t1 = match bench_now() {
        Some(t) => t,
        None => return BENCH_RC_NO_CLOCK,
    };
    let us = t1.saturating_sub(t0);
    // ns/op from a uS clock: multiply, then divide by iterations. Use u128 so
    // large iteration counts do not overflow before the division.
    (us as u128 * 1000 / iterations as u128) as i64
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
    let (Some(fqn), Some(name)) = (
        str_from_raw(fqn_ptr, fqn_len),
        str_from_raw(name_ptr, name_len),
    ) else {
        return BENCH_RC_MISSING;
    };
    let body = {
        let table = TABLE.lock();
        match table.bench_body_named(fqn, name) {
            Some(BenchBody::Native(f)) => BenchBody::Native(*f),
            Some(BenchBody::WasmExport(e)) => BenchBody::WasmExport(e.clone()),
            None => return BENCH_RC_MISSING,
        }
    };
    time_bench_ns(fqn, &body, iterations)
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else {
        return 0;
    };
    let bodies: Vec<(String, BenchBody)> = {
        let table = TABLE.lock();
        let Some(entry) = table.find_by_fqn(fqn) else {
            return 0;
        };
        entry
            .benches
            .iter()
            .map(|b| match &b.body {
                BenchBody::Native(f) => (b.name.clone(), BenchBody::Native(*f)),
                BenchBody::WasmExport(e) => (b.name.clone(), BenchBody::WasmExport(e.clone())),
            })
            .collect()
    };
    if bodies.is_empty() {
        return 0;
    }
    let mut bad = 0i32;
    let mut out = String::from(fqn);
    out.push('\n');
    for (name, body) in &bodies {
        let ns = time_bench_ns(fqn, body, iterations);
        if ns < 0 {
            bad += 1;
            out.push_str("* ");
            out.push_str(name);
            match ns {
                BENCH_RC_NO_CLOCK => out.push_str(": no clock\n"),
                BENCH_RC_MISSING => out.push_str(": missing\n"),
                _ => out.push_str(": failed\n"),
            }
        } else {
            out.push_str(&alloc::format!(
                "* {name}: {ns} ns/op over {iterations} iters\n"
            ));
        }
    }
    copy_str_to_buf(&out, buf, buf_len_io);
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
