//! pymergetic.wasmmod.api — thin scalar helpers over the registry call graph.
//! Hand-written until PMM face codegen exists. See docs/CALLGRAPH.md.
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

// The registry's shared ABI shapes + the Value-convention entry points,
// from the `__types__.rs` face (the Rust twin of `__types__.h` — same
// file the registry itself includes, no second definition). For cargo
// this is an ordinary module include; for the in-kernel rsx compile the
// build face splices the file into the unit.
#[path = "../registry/__types__.rs"]
mod registry_types;

use registry_types::{
    pm_wasmmod_registry_fn_t, pm_wasmmod_registry_value_t, value_get_i32, value_i32,
};

// The registry entry points this card calls, declared as the card's own
// FFI boundary (same posture as the loader's WAMR extern block: the
// linker resolves the symbols from the registry's object; the decl here
// is what both cargo and the in-kernel rsx compile type against).
unsafe extern "C" {
    pub(crate) fn pm_wasmmod_registry_connect_import(
        fqn_ptr: *const u8,
        fqn_len: u32,
        exp_ptr: *const u8,
        exp_len: u32,
        out: *mut *mut core::ffi::c_void,
    ) -> i32;
    pub(crate) fn pm_wasmmod_registry_call(
        fqn_ptr: *const u8,
        fqn_len: u32,
        exp_ptr: *const u8,
        exp_len: u32,
        args: *const pm_wasmmod_registry_value_t,
        nargs: u32,
        results: *mut pm_wasmmod_registry_value_t,
        nresults: u32,
    ) -> i32;
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_api_connect(
    fqn_ptr: *const u8,
    fqn_len: u32,
    exp_ptr: *const u8,
    exp_len: u32,
    out: *mut pm_wasmmod_registry_fn_t,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    unsafe {
        let mut slot: *mut core::ffi::c_void = core::ptr::null_mut();
        if pm_wasmmod_registry_connect_import(
            fqn_ptr,
            fqn_len,
            exp_ptr,
            exp_len,
            &mut slot,
        ) == 0
            || slot.is_null()
        {
            return -1;
        }
        let u = registry_types::fn_slot_t { raw: slot };
        *out = unsafe { u.fnptr };
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_api_call0_i32(
    fqn_ptr: *const u8,
    fqn_len: u32,
    exp_ptr: *const u8,
    exp_len: u32,
    out: *mut i32,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    unsafe {
        let mut res = value_i32(0);
        let st = pm_wasmmod_registry_call(
            fqn_ptr,
            fqn_len,
            exp_ptr,
            exp_len,
            core::ptr::null(),
            0,
            &mut res,
            1,
        );
        if st < 0 {
            return -1;
        }
        *out = value_get_i32(&res);
    }
    0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_api_call2_i32(
    fqn_ptr: *const u8,
    fqn_len: u32,
    exp_ptr: *const u8,
    exp_len: u32,
    a: i32,
    b: i32,
    out: *mut i32,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    unsafe {
        let args = [value_i32(a), value_i32(b)];
        let mut res = value_i32(0);
        let st = pm_wasmmod_registry_call(
            fqn_ptr,
            fqn_len,
            exp_ptr,
            exp_len,
            args.as_ptr(),
            2,
            &mut res,
            1,
        );
        if st < 0 {
            return -1;
        }
        *out = value_get_i32(&res);
    }
    0
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.api", pm_wasmmod_api_connect, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, pm_wasmmod_registry_fn_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.api", pm_wasmmod_api_call0_i32, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, int32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.api", pm_wasmmod_api_call2_i32, "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, int32_t, int32_t, int32_t *)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
