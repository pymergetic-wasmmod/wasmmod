//! pymergetic-wasmmod — crate root.
//!
//! This crate's root doubles as the Rust entry point for the whole
//! `pymergetic` namespace (see docs/SOURCETREE.md "Crate placement" /
//! "Rust and PEP 420"). Rust has no PEP-420 equivalent — no dynamic,
//! multi-distribution namespace merge at compile time, only single-owner
//! `mod`/`use` declarations — so unlike `pymergetic/__pmm__.toml`'s
//! `pep420 = true` (which needs zero Rust or Python files to exist),
//! this level still needs exactly one declaration file. This *is* that
//! file; there's no separate `pymergetic.rs` beside it, since crate root
//! and "the pymergetic namespace's Rust half" are the same thing here.
//!
//! Depend on this crate under the local name `pymergetic`
//! (`pymergetic = { package = "pymergetic-wasmmod", path = "..." }` in a
//! consumer's `Cargo.toml`) so `use pymergetic::util::mem;` reads
//! exactly like the dotted path it mirrors — no
//! `pymergetic_wasmmod::pymergetic::` stutter.
#![cfg_attr(not(any(test, feature = "gen")), no_std)]

// Unlike `core`, `alloc` isn't implicitly linked/path-resolvable even in
// a 2018+-edition crate — it needs one explicit `extern crate alloc;`
// declaration to exist at all as a usable path. Declaring it here, at
// crate root, rather than per-module, is what makes a bare `alloc::`
// path resolve from *any* module in this crate (registry, lz4, ...); a
// leaf module's own `extern crate alloc;` would only be visible inside
// that module. This isn't `no_std`-only, either: `alloc` ships as part
// of every Rust toolchain regardless of `std`, so this line is a no-op
// either way — that's deliberate, it's what lets registry/lz4 use plain
// `alloc::` paths uniformly instead of switching between `alloc::` and
// `std::` per build mode.
extern crate alloc;

/// libc-backed `GlobalAlloc` + `panic_handler` for linking into µPy
/// (`cargo rustc --features upy-host --crate-type staticlib`). Tests use
/// `std` and must not enable this feature. Never combine with ``gen``
/// (host CLI needs real std).
#[cfg(all(feature = "upy-host", not(test), not(feature = "gen")))]
mod upy_host_alloc {
    use core::alloc::{GlobalAlloc, Layout};
    use core::ffi::c_void;

    unsafe extern "C" {
        fn malloc(size: usize) -> *mut c_void;
        fn realloc(ptr: *mut c_void, size: usize) -> *mut c_void;
        fn free(ptr: *mut c_void);
        fn abort() -> !;
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
        unsafe { abort() }
    }

    // Linked into µPy as a staticlib with panic=abort; some LLVM/rustc
    // artifacts still reference this personality symbol — provide a stub.
    #[unsafe(no_mangle)]
    pub extern "C" fn rust_eh_personality() {}
}

#[path = "pymergetic/util.rs"]
pub mod util;

#[path = "pymergetic/wasmmod.rs"]
pub mod wasmmod;

// RS Metal cards live in extmod/metal (path == module). Only the upy-host
// staticlib (unix METAL=1 / host prove) includes them; metal.mk supplies the
// C at final link. gen CLI and `cargo test` stay free of pm_metal_*.
#[cfg(all(feature = "upy-host", not(test)))]
#[path = "../../metal/src/pymergetic/metal.rs"]
pub mod metal;
