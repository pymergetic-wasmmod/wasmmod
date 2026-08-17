//! emcc face of lock + lz4 + mtar + registry + loader + version.
//! Same `__impl__.rs` cards as unix cargo, and the same registry: exports
//! stage from a constructor, replayed once a heap exists.
#![no_std]
#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

extern crate alloc;

use core::alloc::{GlobalAlloc, Layout};
use core::ffi::c_void;

unsafe extern "C" {
    fn malloc(size: usize) -> *mut c_void;
    fn realloc(ptr: *mut c_void, size: usize) -> *mut c_void;
    fn free(ptr: *mut c_void);
}

struct LibcAlloc;

unsafe impl GlobalAlloc for LibcAlloc {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        unsafe { malloc(layout.size()) as *mut u8 }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        unsafe { free(ptr as *mut c_void) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, _layout: Layout, new_size: usize) -> *mut u8 {
        unsafe { realloc(ptr as *mut c_void, new_size) as *mut u8 }
    }
}

#[global_allocator]
static A: LibcAlloc = LibcAlloc;

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn rust_eh_personality() {}

#[macro_export]
macro_rules! PM_MOD_EXPORT_RS {
    ($fqn:expr, $fn:ident, $sig:expr) => {
        const _: () = {
            #[used]
            #[unsafe(link_section = ".init_array")]
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

pub mod util;
pub mod wasmmod;
