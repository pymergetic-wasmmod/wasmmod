//! pymergetic.wasmmod.registry — module tests (`__tests__.rs`).

use super::*;
use crate::util::mod_test::case;

fn publish(fqn: &str, kind: pm_wasmmod_registry_container_kind_t) -> pm_wasmmod_registry_handle_t {
    TABLE.lock().publish(fqn, kind)
}

fn version_set_and_query_roundtrip() {
    let _ = publish("test.ver", pm_wasmmod_registry_container_kind_t::Resident);
    assert!(TABLE.lock().set_version("test.ver", "1.2.3"));
    assert_eq!(TABLE.lock().version_of("test.ver"), Some("1.2.3"));
    let h = TABLE.lock().publish_ver(
        "test.ver2",
        pm_wasmmod_registry_container_kind_t::Wasm,
        "9.0.0",
    );
    assert_ne!(h.index, u32::MAX);
    assert_eq!(TABLE.lock().version_of("test.ver2"), Some("9.0.0"));
}

fn export_sig_and_enumerate_roundtrip() {
    let handle = publish(
        "test.enum_sig",
        pm_wasmmod_registry_container_kind_t::Resident,
    );
    assert!(TABLE.lock().export_set_sig(
        handle,
        "add",
        pm_wasmmod_registry_export_kind_t::Fn,
        core::ptr::dangling_mut::<c_void>(),
        Some("int(int, int)"),
    ));
    assert!(pm_wasmmod_registry_module_count() >= 1);
    let mut name = [0u8; 64];
    let mut name_len = 64u32;
    assert_eq!(
        unsafe { pm_wasmmod_registry_module_at(0, name.as_mut_ptr(), &mut name_len) },
        1
    );
    let n = unsafe { pm_wasmmod_registry_export_count(b"test.enum_sig".as_ptr(), 13) };
    assert_eq!(n, 1);
    let mut ename = [0u8; 32];
    let mut ename_len = 32u32;
    let mut kind = pm_wasmmod_registry_export_kind_t::Obj;
    let mut sig = [0u8; 64];
    let mut sig_len = 64u32;
    assert_eq!(
        unsafe {
            pm_wasmmod_registry_export_at(
                b"test.enum_sig".as_ptr(),
                13,
                0,
                ename.as_mut_ptr(),
                &mut ename_len,
                &mut kind,
                sig.as_mut_ptr(),
                &mut sig_len,
            )
        },
        1
    );
    assert_eq!(kind, pm_wasmmod_registry_export_kind_t::Fn);
    assert_eq!(&ename[..ename_len as usize], b"add");
    assert_eq!(&sig[..sig_len as usize], b"int(int, int)");
}

fn mod_export_ensures_resident() {
    assert_eq!(
        unsafe {
            pm_wasmmod_registry_mod_export(
                b"test.mod_exp".as_ptr(),
                12,
                b"ping".as_ptr(),
                4,
                pm_wasmmod_registry_export_kind_t::Fn,
                core::ptr::dangling_mut::<c_void>(),
                b"int(void)".as_ptr(),
                9,
            )
        },
        1
    );
    assert!(unsafe { pm_wasmmod_registry_has(b"test.mod_exp".as_ptr(), 12) } != 0);
    assert_eq!(
        TABLE.lock().container_of("test.mod_exp"),
        Some(pm_wasmmod_registry_container_kind_t::Resident)
    );
}

fn publish_then_resolve_roundtrips() {
    let handle = publish("test.pub_resolve", pm_wasmmod_registry_container_kind_t::Resident);
    let sentinel = 0x1234usize as *mut c_void;
    assert!(TABLE.lock().export_set(handle, "f", pm_wasmmod_registry_export_kind_t::Fn, sentinel));
    assert_eq!(TABLE.lock().resolve_native("test.pub_resolve", "f"), sentinel);
}

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

fn resolve_native_is_null_for_unknown_module_or_export() {
    assert!(TABLE.lock().resolve_native("test.does_not_exist", "f").is_null());
    let handle = publish("test.known_no_export", pm_wasmmod_registry_container_kind_t::Aot);
    let _ = handle;
    assert!(TABLE.lock().resolve_native("test.known_no_export", "missing").is_null());
}

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
}

unsafe extern "C" fn case_version_set_and_query_roundtrip() -> i32 {
    case(|| version_set_and_query_roundtrip())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "version_set_and_query_roundtrip",
    case_version_set_and_query_roundtrip
);
#[test]
fn test_version_set_and_query_roundtrip() {
    assert_eq!(unsafe { case_version_set_and_query_roundtrip() }, 0);
}

unsafe extern "C" fn case_export_sig_and_enumerate_roundtrip() -> i32 {
    case(|| export_sig_and_enumerate_roundtrip())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "export_sig_and_enumerate_roundtrip",
    case_export_sig_and_enumerate_roundtrip
);
#[test]
fn test_export_sig_and_enumerate_roundtrip() {
    assert_eq!(unsafe { case_export_sig_and_enumerate_roundtrip() }, 0);
}

unsafe extern "C" fn case_mod_export_ensures_resident() -> i32 {
    case(|| mod_export_ensures_resident())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.registry",
    "mod_export_ensures_resident",
    case_mod_export_ensures_resident
);
#[test]
fn test_mod_export_ensures_resident() {
    assert_eq!(unsafe { case_mod_export_ensures_resident() }, 0);
}

unsafe extern "C" fn case_publish_then_resolve_roundtrips() -> i32 {
    case(|| publish_then_resolve_roundtrips())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "publish_then_resolve_roundtrips", case_publish_then_resolve_roundtrips);
#[test]
fn test_publish_then_resolve_roundtrips() {
    assert_eq!(unsafe { case_publish_then_resolve_roundtrips() }, 0);
}

unsafe extern "C" fn case_unpublish_invalidates_the_old_handle_even_after_slot_reuse() -> i32 {
    case(|| unpublish_invalidates_the_old_handle_even_after_slot_reuse())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "unpublish_invalidates_the_old_handle_even_after_slot_reuse", case_unpublish_invalidates_the_old_handle_even_after_slot_reuse);
#[test]
fn test_unpublish_invalidates_the_old_handle_even_after_slot_reuse() {
    assert_eq!(unsafe { case_unpublish_invalidates_the_old_handle_even_after_slot_reuse() }, 0);
}

unsafe extern "C" fn case_resolve_native_is_null_for_unknown_module_or_export() -> i32 {
    case(|| resolve_native_is_null_for_unknown_module_or_export())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "resolve_native_is_null_for_unknown_module_or_export", case_resolve_native_is_null_for_unknown_module_or_export);
#[test]
fn test_resolve_native_is_null_for_unknown_module_or_export() {
    assert_eq!(unsafe { case_resolve_native_is_null_for_unknown_module_or_export() }, 0);
}

unsafe extern "C" fn case_call_roundtrips_through_the_fn_t_convention() -> i32 {
    case(|| call_roundtrips_through_the_fn_t_convention())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "call_roundtrips_through_the_fn_t_convention", case_call_roundtrips_through_the_fn_t_convention);
#[test]
fn test_call_roundtrips_through_the_fn_t_convention() {
    assert_eq!(unsafe { case_call_roundtrips_through_the_fn_t_convention() }, 0);
}

unsafe extern "C" fn case_call_is_negative_one_for_unknown_module_or_export() -> i32 {
    case(|| call_is_negative_one_for_unknown_module_or_export())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "call_is_negative_one_for_unknown_module_or_export", case_call_is_negative_one_for_unknown_module_or_export);
#[test]
fn test_call_is_negative_one_for_unknown_module_or_export() {
    assert_eq!(unsafe { case_call_is_negative_one_for_unknown_module_or_export() }, 0);
}

unsafe extern "C" fn case_gc_visit_only_sees_live_obj_exports() -> i32 {
    case(|| gc_visit_only_sees_live_obj_exports())
}
crate::PM_MOD_TEST_RS!("pymergetic.wasmmod.registry", "gc_visit_only_sees_live_obj_exports", case_gc_visit_only_sees_live_obj_exports);
#[test]
fn test_gc_visit_only_sees_live_obj_exports() {
    assert_eq!(unsafe { case_gc_visit_only_sees_live_obj_exports() }, 0);
}

