//! pymergetic.wasmmod.api — module tests (`__tests__.rs`).

use super::*;
use crate::util::mod_test::case;
use crate::wasmmod::registry::{
    pm_wasmmod_registry_container_kind_t, pm_wasmmod_registry_export_kind_t,
    pm_wasmmod_registry_export_set, pm_wasmmod_registry_init, pm_wasmmod_registry_publish,
    pm_wasmmod_registry_unpublish,
};

unsafe extern "C" fn bridge_add(
    args: *const pm_wasmmod_registry_value_t,
    nargs: u32,
    results: *mut pm_wasmmod_registry_value_t,
    nresults: u32,
) -> i32 {
    if nargs < 2 || nresults < 1 || args.is_null() || results.is_null() {
        return -1;
    }
    unsafe {
        let a = (*args.add(0)).of.i32;
        let b = (*args.add(1)).of.i32;
        (*results).kind = pm_wasmmod_registry_valkind_t::I32;
        (*results).of.i32 = a + b;
    }
    0
}

fn api_call2_and_connect_roundtrip() {
    unsafe {
        pm_wasmmod_registry_init();
        let fqn = b"test.api.add";
        let h = pm_wasmmod_registry_publish(
            fqn.as_ptr(),
            fqn.len() as u32,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
        assert_ne!(h.index, u32::MAX);
        assert_eq!(
            pm_wasmmod_registry_export_set(
                h,
                b"add".as_ptr(),
                3,
                pm_wasmmod_registry_export_kind_t::Fn,
                bridge_add as *mut core::ffi::c_void,
            ),
            1
        );

        let mut out = 0i32;
        assert_eq!(
            pm_wasmmod_api_call2_i32(
                fqn.as_ptr(),
                fqn.len() as u32,
                b"add".as_ptr(),
                3,
                2,
                3,
                &mut out
            ),
            0
        );
        assert_eq!(out, 5);

        let mut fn_ptr: pm_wasmmod_registry_fn_t = bridge_add;
        assert_eq!(
            pm_wasmmod_api_connect(
                fqn.as_ptr(),
                fqn.len() as u32,
                b"add".as_ptr(),
                3,
                &mut fn_ptr
            ),
            0
        );
        let args = [
            pm_wasmmod_registry_value_t {
                kind: pm_wasmmod_registry_valkind_t::I32,
                of: pm_wasmmod_registry_value_of_t { i32: 10 },
            },
            pm_wasmmod_registry_value_t {
                kind: pm_wasmmod_registry_valkind_t::I32,
                of: pm_wasmmod_registry_value_of_t { i32: 1 },
            },
        ];
        let mut res = pm_wasmmod_registry_value_t {
            kind: pm_wasmmod_registry_valkind_t::I32,
            of: pm_wasmmod_registry_value_of_t { i32: 0 },
        };
        assert_eq!(fn_ptr(args.as_ptr(), 2, &mut res, 1), 0);
        assert_eq!(res.of.i32, 11);

        pm_wasmmod_registry_unpublish(h);
    }
}

unsafe extern "C" fn case_api_call2_and_connect_roundtrip() -> i32 {
    case(|| api_call2_and_connect_roundtrip())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.api",
    "api_call2_and_connect_roundtrip",
    case_api_call2_and_connect_roundtrip
);
#[test]
fn test_api_call2_and_connect_roundtrip() {
    assert_eq!(unsafe { case_api_call2_and_connect_roundtrip() }, 0);
}
