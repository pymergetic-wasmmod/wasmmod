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

const INVALID_HANDLE: pm_wasmmod_registry_handle_t = pm_wasmmod_registry_handle_t { index: u32::MAX, generation: 0 };

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
}

struct ModEntry {
    fqn: String,
    #[allow(dead_code)] // not yet read back anywhere; kept for the coming pack/format consumer
    container: pm_wasmmod_registry_container_kind_t,
    generation: u32,
    exports: Vec<Export>,
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
        Self { entries: Vec::new() }
    }

    fn publish(&mut self, fqn: &str, container: pm_wasmmod_registry_container_kind_t) -> pm_wasmmod_registry_handle_t {
        // Reuse the first dead slot if one exists — unpublish leaves a
        // hole rather than shifting indices, since every other live
        // handle's index has to keep meaning the same thing.
        if let Some((index, entry)) = self.entries.iter_mut().enumerate().find(|(_, e)| !e.live) {
            let generation = entry.generation.wrapping_add(1);
            *entry = ModEntry {
                fqn: String::from(fqn),
                container,
                generation,
                exports: Vec::new(),
                live: true,
            };
            return pm_wasmmod_registry_handle_t { index: index as u32, generation };
        }
        let index = self.entries.len() as u32;
        self.entries.push(ModEntry {
            fqn: String::from(fqn),
            container,
            generation: 0,
            exports: Vec::new(),
            live: true,
        });
        pm_wasmmod_registry_handle_t { index, generation: 0 }
    }

    fn unpublish(&mut self, handle: pm_wasmmod_registry_handle_t) -> bool {
        match self.live_entry_mut(handle) {
            Some(entry) => {
                entry.live = false;
                entry.exports.clear();
                entry.fqn.clear();
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
        let Some(entry) = self.live_entry_mut(handle) else { return false };
        if let Some(export) = entry.exports.iter_mut().find(|e| e.name == name) {
            export.kind = kind;
            export.ptr = ptr;
        } else {
            entry.exports.push(Export { name: String::from(name), kind, ptr });
        }
        true
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
            for export in entry.exports.iter().filter(|e| e.kind == pm_wasmmod_registry_export_kind_t::Obj) {
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
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else { return INVALID_HANDLE };
    TABLE.lock().publish(fqn, container)
}

#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_registry_unpublish(handle: pm_wasmmod_registry_handle_t) -> i32 {
    TABLE.lock().unpublish(handle) as i32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_has(fqn_ptr: *const u8, fqn_len: u32) -> i32 {
    let Some(fqn) = str_from_raw(fqn_ptr, fqn_len) else { return 0 };
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
    let Some(name) = str_from_raw(name_ptr, name_len) else { return 0 };
    TABLE.lock().export_set(handle, name, kind, ptr) as i32
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_registry_resolve_native(
    fqn_ptr: *const u8,
    fqn_len: u32,
    export_name_ptr: *const u8,
    export_name_len: u32,
) -> *mut c_void {
    let (Some(fqn), Some(export_name)) =
        (str_from_raw(fqn_ptr, fqn_len), str_from_raw(export_name_ptr, export_name_len))
    else {
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
    let (Some(fqn), Some(export_name)) =
        (str_from_raw(fqn_ptr, fqn_len), str_from_raw(export_name_ptr, export_name_len))
    else {
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
    let (Some(fqn), Some(export_name)) =
        (str_from_raw(fqn_ptr, fqn_len), str_from_raw(export_name_ptr, export_name_len))
    else {
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
pub extern "C" fn pm_wasmmod_registry_gc_visit(visit: extern "C" fn(*mut c_void, *mut c_void), ctx: *mut c_void) {
    TABLE.lock().gc_visit(visit, ctx);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn publish(fqn: &str, kind: pm_wasmmod_registry_container_kind_t) -> pm_wasmmod_registry_handle_t {
        TABLE.lock().publish(fqn, kind)
    }

    #[test]
    fn publish_then_resolve_roundtrips() {
        let handle = publish("test.pub_resolve", pm_wasmmod_registry_container_kind_t::Resident);
        let sentinel = 0x1234usize as *mut c_void;
        assert!(TABLE.lock().export_set(handle, "f", pm_wasmmod_registry_export_kind_t::Fn, sentinel));
        assert_eq!(TABLE.lock().resolve_native("test.pub_resolve", "f"), sentinel);
    }

    #[test]
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

    #[test]
    fn resolve_native_is_null_for_unknown_module_or_export() {
        assert!(TABLE.lock().resolve_native("test.does_not_exist", "f").is_null());
        let handle = publish("test.known_no_export", pm_wasmmod_registry_container_kind_t::Aot);
        let _ = handle;
        assert!(TABLE.lock().resolve_native("test.known_no_export", "missing").is_null());
    }

    #[test]
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

    #[test]
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

    #[test]
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
        assert_eq!(seen.len(), 1); // the Fn export must not show up
    }
}
