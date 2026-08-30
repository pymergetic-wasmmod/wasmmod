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

/// Host-side module benchmark registration (same job as `PM_MOD_BENCH_C`).
///
/// Bench fn: `unsafe extern "C" fn(iterations: u64) -> i32` — do `iterations`
/// ops, `0` = ok. The registry owns timing (warmup + measured lap → ns/op);
/// benches are informational and never gate the build. Lives in `__bench__.rs`
/// under `cfg(bench)`, so it never becomes a product export face.
///
/// ```ignore
/// PM_MOD_BENCH_RS!("pymergetic.metal.async", "ready_ring_task_switch", bench_task_switch);
/// ```
#[macro_export]
macro_rules! PM_MOD_BENCH_RS {
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
            static __PM_MOD_BENCH: extern "C" fn() = {
                extern "C" fn __pm_mod_bench_ctor() {
                    let fqn: &str = $fqn;
                    let name: &str = $name;
                    unsafe {
                        let _ = $crate::wasmmod::registry::pm_wasmmod_registry_bench_register(
                            fqn.as_ptr(),
                            fqn.len() as u32,
                            name.as_ptr(),
                            name.len() as u32,
                            Some($fn),
                        );
                    }
                }
                __pm_mod_bench_ctor
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

/*----------------------------------------------------------------------
 * pymergetic.types C-ABI mirrors for PM_TYPE_DEFINE_RS! — same posture
 * as pm_mod_boot_t above (the defining layout lives in
 * pymergetic/types/__types__.h, impl = "c"; this is the Rust face).
 *--------------------------------------------------------------------*/

pub const PM_TYPE_DESCRIPTOR_MAGIC: u32 = 0x54595045; /* "TYPE" */
pub const PM_TYPE_DESC_STRUCT: u16 = 0;

/* Primitive descriptors — the same statics PM_TYPE_DEFINE_C references
 * in C (pymergetic/types/__types__.h). Field rows point at these
 * directly: no registry lookup in the ctor, because .init_array order
 * is link order and the primitives' own ctors may not have run yet
 * when a card's ctor fires. */
unsafe extern "C" {
    pub static PM_TYPE_NIL_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_I32_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_I64_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_U32_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_U64_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_F32_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_F64_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_BOOL_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_STR_DESC: pm_types_descriptor_rs;
    pub static PM_TYPE_BYTES_DESC: pm_types_descriptor_rs;
    /* list / dict have no exported symbol (file-local in the C card)
     * — field rows for those fall through to a registry find. */
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct pm_types_field_rs {
    pub name_hash: u16,
    pub _flags: u16,
    pub offset: u32,
    pub type_: *const core::ffi::c_void, /* const pm_type_descriptor_t * */
    pub name: *const u8,
}

unsafe impl Sync for pm_types_field_rs {}

#[repr(C)]
pub struct pm_types_descriptor_rs {
    pub magic: u32,
    pub kind: u16,
    pub instance_size: u16,
    pub name: *const u8,
    pub fqn: *const u8,
    pub parent: *const core::ffi::c_void, /* const pm_type_descriptor_t * */
    pub field_count: u16,
    pub fields: *const pm_types_field_rs,
    pub method_count: u16,
    pub methods: *const core::ffi::c_void, /* const pm_type_method_t * */
}

unsafe impl Sync for pm_types_descriptor_rs {}

/// Host-side type registration (same job as `PM_TYPE_DEFINE_C` in
/// pymergetic/types/__types__.h). Fields resolve their type descriptors
/// from the registry at ctor time (boot-order: card ctors run after the
/// types card's primitives), then the descriptor registers — the same
/// single-threaded-ctor contract the C face uses. The registry calls go
/// through `crate::types::` (the generated face, one declaration).
///
/// ```ignore
/// PM_TYPE_DEFINE_RS!("pymergetic.metal.geo.Point", PM_TYPE_DESC_STRUCT,
///     16, None,
///     &[
///         ("x", "pymergetic.types.f64", 0),
///         ("y", "pymergetic.types.f64", 8),
///     ]);
/// ```
#[macro_export]
macro_rules! PM_TYPE_DEFINE_RS {
    ($fqn:expr, $kind:expr, $inst_size:expr, $parent:expr, $fields:expr) => {
        const _: () = {
            static __PM_TYPE_FIELD_SPEC: &[(&str, &str, u32)] = $fields;
            // Fixed-capacity field rows, filled at ctor time (boot order:
            // types card primitives register before card ctors run).
            static mut __PM_TYPE_FIELDS: [$crate::wasmmod::guest::pm_types_field_rs; 64] =
                [$crate::wasmmod::guest::pm_types_field_rs {
                    name_hash: 0,
                    _flags: 0,
                    offset: 0,
                    type_: core::ptr::null(),
                    name: core::ptr::null(),
                }; 64];
            /* NUL-terminated backing for each row's name / type fqn —
             * &str literals are not NUL-terminated, and C strcmp must
             * never run past (separate array so the ABI rows keep the
             * pm_type_field_t stride). */
            static mut __PM_TYPE_NAME_BUFS: [[u8; 32]; 64] = [[0; 32]; 64];
            static mut __PM_TYPE_TYPE_BUFS: [[u8; 192]; 64] = [[0; 192]; 64];
            static __PM_TYPE_DESC: $crate::wasmmod::guest::pm_types_descriptor_rs = $crate::wasmmod::guest::pm_types_descriptor_rs {
                magic: $crate::wasmmod::guest::PM_TYPE_DESCRIPTOR_MAGIC,
                kind: $kind,
                instance_size: $inst_size,
                name: concat!($fqn, "\0").as_ptr(),
                fqn: concat!($fqn, "\0").as_ptr(),
                parent: core::ptr::null(),
                field_count: __PM_TYPE_FIELD_SPEC.len() as u16,
                fields: unsafe { core::ptr::addr_of!(__PM_TYPE_FIELDS) as *const $crate::wasmmod::guest::pm_types_field_rs },
                method_count: 0,
                methods: core::ptr::null(),
            };
            #[used]
            #[cfg_attr(
                any(target_os = "linux", target_os = "android"),
                unsafe(link_section = ".init_array")
            )]
            #[cfg_attr(
                target_vendor = "apple",
                unsafe(link_section = "__DATA,__mod_init_func")
            )]
            static __PM_TYPE_REG: extern "C" fn() = {
                extern "C" fn __pm_type_ctor() {
                    unsafe {
                        let n = __PM_TYPE_FIELD_SPEC.len();
                        let rows = core::ptr::addr_of_mut!(__PM_TYPE_FIELDS) as *mut u8;
                        let names = core::ptr::addr_of_mut!(__PM_TYPE_NAME_BUFS) as *mut u8;
                        let types = core::ptr::addr_of_mut!(__PM_TYPE_TYPE_BUFS) as *mut u8;
                        /* Row stride: pm_types_field_rs is #[repr(C)] —
                         * read it from the type itself, never hardcode. */
                        let stride = core::mem::size_of::<$crate::wasmmod::guest::pm_types_field_rs>();
                        for i in 0..n {
                            let (name, type_fqn, offset) = __PM_TYPE_FIELD_SPEC[i];
                            let row = rows.add(i * stride)
                                as *mut $crate::wasmmod::guest::pm_types_field_rs;
                            let nb = name.as_bytes();
                            let nlen = nb.len().min(31);
                            let nbuf = names.add(i * 32);
                            for j in 0..nlen {
                                *nbuf.add(j) = nb[j];
                            }
                            *nbuf.add(nlen) = 0;
                            (*row).name = nbuf;
                            (*row).offset = offset;
                            let mut h: u32 = 5381;
                            for &b in &nb[..nlen] {
                                h = h.wrapping_mul(33).wrapping_add(b as u32);
                            }
                            (*row).name_hash = (h & 0xFFFF) as u16;
                            if !type_fqn.is_empty() {
                                /* Primitives: static descriptor refs (same as
                                 * the C face) — boot-order independent.
                                 * Struct fqns: registry find (those cards'
                                 * ctors register descriptors; the gen-time
                                 * scan stages them, and the linked binary
                                 * resolves them after boot completes). */
                                let desc: *const core::ffi::c_void = match type_fqn {
                                    "pymergetic.types.nil" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_NIL_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.i32" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_I32_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.i64" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_I64_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.u32" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_U32_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.u64" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_U64_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.f32" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_F32_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.f64" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_F64_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.bool" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_BOOL_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.str" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_STR_DESC
                                        ).cast()
                                    }
                                    "pymergetic.types.bytes" => {
                                        core::ptr::addr_of!(
                                            $crate::wasmmod::guest::PM_TYPE_BYTES_DESC
                                        ).cast()
                                    }
                                    _ => {
                                        let bytes = type_fqn.as_bytes();
                                        let len = bytes.len().min(191);
                                        let tbuf = types.add(i * 192);
                                        for j in 0..len {
                                            *tbuf.add(j) = bytes[j];
                                        }
                                        *tbuf.add(len) = 0;
                                        $crate::types::pm_types_registry_find(tbuf)
                                            as *const core::ffi::c_void
                                    }
                                };
                                (*row).type_ = desc;
                            }
                        }
                        let _ = $crate::types::pm_types_registry_register(
                            core::ptr::addr_of!(__PM_TYPE_DESC)
                                as *const $crate::types::pm_type_descriptor_t,
                        );
                    }
                }
                __pm_type_ctor
            };
        };
    };
}

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

#[cfg(test)]
mod type_define_tests {
    //! Prove PM_TYPE_DEFINE_RS! end-to-end on the host build: the
    //! .init_array ctor resolves field descriptors from the live
    //! registry and registers the type — the same single-registry
    //! contract PM_TYPE_DEFINE_C uses. The macro runs at module scope
    //! (its ctor fires at process start, before any test).
    #[cfg(feature = "gen")]
    crate::PM_TYPE_DEFINE_RS!(
        "pymergetic.wasmmod.guest_test.Point",
        crate::wasmmod::guest::PM_TYPE_DESC_STRUCT,
        16,
        None,
        &[("x", "pymergetic.types.f64", 0), ("y", "pymergetic.types.f64", 8)]
    );

    #[cfg(feature = "gen")]
    #[test]
    fn rs_type_define_registers() {
        let fqn = b"pymergetic.wasmmod.guest_test.Point\0";
        let d = unsafe { crate::types::pm_types_registry_find(fqn.as_ptr()) };
        assert!(!d.is_null(), "PM_TYPE_DEFINE_RS! ctor registered the type");
        let view = unsafe {
            let mut view = crate::wasmmod::guest::pm_types_descriptor_rs {
                magic: 0,
                kind: 0,
                instance_size: 0,
                name: core::ptr::null(),
                fqn: core::ptr::null(),
                parent: core::ptr::null(),
                field_count: 0,
                fields: core::ptr::null(),
                method_count: 0,
                methods: core::ptr::null(),
            };
            core::ptr::copy_nonoverlapping(d.cast(), &mut view, 1);
            view
        };
        assert_eq!(view.magic, crate::wasmmod::guest::PM_TYPE_DESCRIPTOR_MAGIC);
        assert_eq!(view.instance_size, 16);
        assert_eq!(view.field_count, 2);
        /* Field rows: name pointers must be valid, hashes computed,
         * primitive descriptors resolved from the registry. */
        unsafe {
            let mut prev = 0u16;
            for i in 0..view.field_count as usize {
                let f = &*view.fields.add(i);
                assert!(!f.name.is_null(), "field name set");
                let name = core::ffi::c_str::CStr::from_ptr(f.name.cast()).to_string_lossy();
                assert_eq!(name, if i == 0 { "x" } else { "y" });
                assert!(f.name_hash > 0, "hash computed");
                if i > 0 {
                    assert!(f.name_hash > prev, "sorted by name_hash");
                }
                prev = f.name_hash;
                assert!(!f.type_.is_null(), "primitive descriptor resolved");
            }
        }
    }
}
