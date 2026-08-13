//! pymergetic.wasmmod.api — thin scalar helpers over the registry call graph.
//! Hand-written until PMM face codegen exists. See docs/CALLGRAPH.md.
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

use crate::wasmmod::registry::{
    pm_wasmmod_registry_call, pm_wasmmod_registry_connect_import, pm_wasmmod_registry_fn_t,
    pm_wasmmod_registry_valkind_t, pm_wasmmod_registry_value_of_t, pm_wasmmod_registry_value_t,
};

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
        if pm_wasmmod_registry_connect_import(fqn_ptr, fqn_len, exp_ptr, exp_len, &mut slot) == 0
            || slot.is_null()
        {
            return -1;
        }
        *out = core::mem::transmute::<*mut core::ffi::c_void, pm_wasmmod_registry_fn_t>(slot);
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
        let mut res = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: pm_wasmmod_registry_value_of_t { i32: 0 },
        };
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
        *out = res.of.i32;
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
        let args = [
            pm_wasmmod_registry_value_t {
                kind: pm_wasmmod_registry_valkind_t::I32,
                of: pm_wasmmod_registry_value_of_t { i32: a },
            },
            pm_wasmmod_registry_value_t {
                kind: pm_wasmmod_registry_valkind_t::I32,
                of: pm_wasmmod_registry_value_of_t { i32: b },
            },
        ];
        let mut res = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: pm_wasmmod_registry_value_of_t { i32: 0 },
        };
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
        *out = res.of.i32;
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
