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

static WASM_TEST_RUNNER: Mutex<Option<pm_wasmmod_registry_wasm_test_runner_t>> = Mutex::new(None);

struct ModEntry {
    fqn: String,
    /// Package / kernel version string (empty = unset). Build-root packs and
    /// host-intrinsic modules that are independently dependable.
    version: String,
    container: pm_wasmmod_registry_container_kind_t,
    generation: u32,
    exports: Vec<Export>,
    tests: Vec<TestEntry>,
    live: bool,
}

struct Table {
    entries: Vec<ModEntry>,
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

    fn live_modules(&self) -> Vec<&str> {
        self.entries
            .iter()
            .filter(|e| e.live)
            .map(|e| e.fqn.as_str())
            .collect()
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
/// scheduling (metal's own one-runner-per-CPU model included), so a real
/// lock is load-bearing here, not decoration.
static TABLE: Mutex<Table> = Mutex::new(Table::new());

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
    let mut table = TABLE.lock();
    let handle = table.ensure_resident(fqn);
    table.export_set_sig(handle, name, kind, ptr, sig) as i32
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
    TABLE.lock().live_modules().len() as u32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_module_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    let table = TABLE.lock();
    let mods = table.live_modules();
    let Some(fqn) = mods.get(index as usize) else {
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
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_count, "uint32_t(const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_at, "int32_t(const uint8_t *, uint32_t, uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_run, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_test_run_all, "int32_t(const uint8_t *, uint32_t)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
