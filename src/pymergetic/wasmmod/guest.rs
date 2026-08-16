//! pymergetic.wasmmod.guest — Rust face of `guest.h` (path == module).
//!
//! C: `PM_MOD_EXPORT_C` / `PM_MOD_CONNECT` / `PM_MOD_BOOT_C` in `guest.h`
//! RS: `PM_MOD_EXPORT_RS!` / `PM_MOD_BOOT_RS!` here

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

#[repr(C, align(8))]
pub struct pm_mod_boot_t {
    pub fqn: *const u8,
    pub init: Option<unsafe extern "C" fn(*mut core::ffi::c_void) -> i32>,
    pub deinit: Option<unsafe extern "C" fn()>,
    pub ready: Option<unsafe extern "C" fn() -> i32>,
}

unsafe impl Sync for pm_mod_boot_t {}

#[repr(C, align(8))]
pub struct pm_mod_bootdep_t {
    pub fqn: *const u8,
    pub dep: *const u8,
    pub flags: u32,
}

unsafe impl Sync for pm_mod_bootdep_t {}

pub unsafe fn pm_mod_boot_add(rec: *const pm_mod_boot_t) -> i32 {
    unsafe { crate::wasmmod::boot::pm_mod_boot_add(rec.cast()) }
}

pub unsafe fn pm_mod_bootdep_add(rec: *const pm_mod_bootdep_t) -> i32 {
    unsafe { crate::wasmmod::boot::pm_mod_bootdep_add(rec.cast()) }
}

/// Same job as `PM_MOD_BOOT_C` — record in section `pm_mod_boot`.
#[macro_export]
macro_rules! PM_MOD_BOOT_RS {
    ($fqn:expr, $init:ident, $deinit:ident) => {
        const _: () = {
            unsafe extern "C" fn __pm_mod_boot_init(arena: *mut core::ffi::c_void) -> i32 {
                unsafe { $init(arena.cast()) }
            }
            #[used]
            #[unsafe(link_section = "pm_mod_boot")]
            static __PM_MOD_BOOT: $crate::wasmmod::guest::pm_mod_boot_t =
                $crate::wasmmod::guest::pm_mod_boot_t {
                    fqn: concat!($fqn, "\0").as_ptr(),
                    init: Some(__pm_mod_boot_init),
                    deinit: Some($deinit),
                    ready: None,
                };
            #[used]
            #[cfg_attr(
                any(target_arch = "wasm32", target_os = "linux", target_os = "emscripten"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_MOD_BOOT_REG: extern "C" fn() = {
                extern "C" fn __pm_mod_boot_reg() {
                    unsafe {
                        let _ = $crate::wasmmod::guest::pm_mod_boot_add(&__PM_MOD_BOOT);
                    }
                }
                __pm_mod_boot_reg
            };
        };
    };
}

/// Hard bootdep. Same job as `PM_MOD_BOOTDEP_C`.
#[macro_export]
macro_rules! PM_MOD_BOOTDEP_RS {
    ($fqn:expr, $dep:expr) => {
        const _: () = {
            #[used]
            #[unsafe(link_section = "pm_mod_bootdep")]
            static __PM_MOD_BOOTDEP: $crate::wasmmod::guest::pm_mod_bootdep_t =
                $crate::wasmmod::guest::pm_mod_bootdep_t {
                    fqn: concat!($fqn, "\0").as_ptr(),
                    dep: concat!($dep, "\0").as_ptr(),
                    flags: 0,
                };
            #[used]
            #[cfg_attr(
                any(target_arch = "wasm32", target_os = "linux", target_os = "emscripten"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(target_os = "uefi", unsafe(link_section = ".CRT$XCU"))]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_MOD_BOOTDEP_REG: extern "C" fn() = {
                extern "C" fn __pm_mod_bootdep_reg() {
                    unsafe {
                        let _ = $crate::wasmmod::guest::pm_mod_bootdep_add(&__PM_MOD_BOOTDEP);
                    }
                }
                __pm_mod_bootdep_reg
            };
        };
    };
}
