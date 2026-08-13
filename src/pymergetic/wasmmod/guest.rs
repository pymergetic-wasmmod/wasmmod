//! pymergetic.wasmmod.guest — Rust face of `guest.h` (path == module).
//!
//! C: `PM_MOD_EXPORT_C` / `PM_MOD_CONNECT` in `guest.h`
//! RS: `PM_MOD_EXPORT_RS!` here
//! Both call `pm_wasmmod_registry_mod_export` — one table, two language macros.

/// Host-side export registration (same job as `PM_MOD_EXPORT_C` in `guest.h`).
///
/// ```ignore
/// PM_MOD_EXPORT_RS!("pymergetic.wasmmod.registry", pm_wasmmod_registry_init, "void(void)");
/// ```
#[macro_export]
macro_rules! PM_MOD_EXPORT_RS {
    ($fqn:expr, $fn:ident, $sig:expr) => {
        const _: () = {
            #[used]
            #[cfg_attr(
                any(target_os = "linux", target_os = "android"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_MOD_EXPORT: extern "C" fn() = {
                extern "C" fn __pm_mod_export_ctor() {
                    let fqn: &str = $fqn;
                    let name: &str = stringify!($fn);
                    let sig: &str = $sig;
                    let ptr = $fn as *mut core::ffi::c_void;
                    unsafe {
                        let _ = $crate::wasmmod::registry::pm_wasmmod_registry_mod_export(
                            fqn.as_ptr(),
                            fqn.len() as u32,
                            name.as_ptr(),
                            name.len() as u32,
                            $crate::wasmmod::registry::pm_wasmmod_registry_export_kind_t::Fn,
                            ptr,
                            sig.as_ptr(),
                            sig.len() as u32,
                        );
                    }
                }
                __pm_mod_export_ctor
            };
        };
    };
}

/// Host-side module test registration (same job as `PM_MOD_TEST_C` in `guest.h`).
///
/// Case fn: `unsafe extern "C" fn() -> i32` — `0` pass, nonzero fail.
/// Lives in `__tests__.rs` under `cfg(test)` — never a product export face.
///
/// ```ignore
/// PM_MOD_TEST_RS!("pymergetic.util.version", "cmp_orders_semver", case_cmp);
/// ```
#[macro_export]
macro_rules! PM_MOD_TEST_RS {
    ($fqn:expr, $name:expr, $fn:ident) => {
        const _: () = {
            #[used]
            #[cfg_attr(
                any(target_os = "linux", target_os = "android"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_MOD_TEST: extern "C" fn() = {
                extern "C" fn __pm_mod_test_ctor() {
                    let fqn: &str = $fqn;
                    let name: &str = $name;
                    unsafe {
                        let _ = $crate::wasmmod::registry::pm_wasmmod_registry_test_register(
                            fqn.as_ptr(),
                            fqn.len() as u32,
                            name.as_ptr(),
                            name.len() as u32,
                            Some($fn),
                        );
                    }
                }
                __pm_mod_test_ctor
            };
        };
    };
}
