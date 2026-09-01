//! pymergetic.wasmmod.registry — shared ABI shapes, Rust face.
//!
//! Hand-written twin of `__types__.h` (same discipline as the other
//! pre-codegen faces: hand-authored to match the end-state shape, one
//! definition per language, mirroring docs/REGISTRY.md). Cards that
//! consume these types `#[path]`-include this file as a module — for
//! cargo that is an ordinary module include; for the in-kernel rsx
//! compile the build face splices the file into the unit, so every seat
//! sees the same layout with no second definition anywhere.
//!
//! `repr(C)` throughout; the C header is the ABI contract.

#![allow(non_camel_case_types)]

/// Which kind of artifact a module's exports live behind. `Resident`
/// covers a statically-linked-in C/Rust module with no container at all —
/// this is what makes wasm "one-of" alongside elf/aot, not a hierarchy
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
/// Python export faces, reused here rather than invented twice.
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

/// A handle into the registry table: index + generation. Passed by value
/// everywhere, never a pointer — a stale copy is always safely
/// detectable via the generation mismatch.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct pm_wasmmod_registry_handle_t {
    pub index: u32,
    pub generation: u32,
}

/// The four primitive shapes a value crossing a container boundary can
/// be — deliberately the same four as wasm's own core value types
/// (WAMR's own `wasm_val_t` uses this exact kind+union shape). See
/// docs/REGISTRY.md "Value convention".
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

/// Construct an I32 value — twin of the C face's inline
/// `pm_wasmmod_registry_value_i32` helper in `__types__.h`.
#[inline]
pub fn value_i32(v: i32) -> pm_wasmmod_registry_value_t {
    pm_wasmmod_registry_value_t {
        kind: pm_wasmmod_registry_valkind_t::I32,
        of: pm_wasmmod_registry_value_of_t { i32: v },
    }
}

/// Read the I32 payload. The kind field is the caller's contract to
/// check; this reads the active member like any C union.
#[inline]
pub fn value_get_i32(v: &pm_wasmmod_registry_value_t) -> i32 {
    unsafe { v.of.i32 }
}

/// Reinterpret a raw slot from `connect_import` as the resolved fn
/// pointer — the value-level reinterpret C does by union, since a
/// pointer-to-fn-pointer cast is a load in disguise, not a
/// reinterpretation.
#[repr(C)]
#[derive(Clone, Copy)]
pub union fn_slot_t {
    pub raw: *mut core::ffi::c_void,
    pub fnptr: pm_wasmmod_registry_fn_t,
}
