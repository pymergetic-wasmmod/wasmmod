//! pymergetic.util.mem tests — arena creation / walk / check.
//!
//! These call the canonical C border that `mem/__exports__.rs` declares and
//! `util/mem.rs` re-exports — a second *user* of that spelling, never a second
//! spelling of it (no local `extern` mirror here). The C implementation is
//! `mem/__impl__.c`, linked into the test binary by the crate's build script.

use super::{
    pm_util_mem_alloc, pm_util_mem_arena_check, pm_util_mem_arena_create, pm_util_mem_arena_walk,
    pm_util_mem_free,
};
use core::ffi::c_void;

/// A page-aligned byte buffer. `pm_util_mem_arena_create` rounds the caller's
/// region up to a page boundary and requires the resulting span to cover
/// `PM_UTIL_MEM_MIN_SPAN` (8 pages = 32 KiB); a bare stack array is neither
/// page-aligned nor (for the small cases) large enough, so wrap it.
#[repr(align(4096))]
struct Pages<const N: usize>([u8; N]);

#[test]
fn arena_create_small() {
    // 8 pages = PM_UTIL_MEM_MIN_SPAN — the smallest span the arena accepts
    // (header + structural/guard overhead is folded into that minimum).
    let mut buf = Pages([0u8; 8 * 4096]);
    let arena = unsafe { pm_util_mem_arena_create(buf.0.as_mut_ptr() as *mut c_void, buf.0.len()) };
    assert!(!arena.is_null());
    // Check must return 0 for a valid arena
    assert_eq!(unsafe { pm_util_mem_arena_check(arena.cast_const()) }, 0);
}

#[test]
fn arena_walk_and_free() {
    let mut buf = Pages([0u8; 16 * 4096]);
    let arena = unsafe { pm_util_mem_arena_create(buf.0.as_mut_ptr() as *mut c_void, buf.0.len()) };
    assert!(!arena.is_null());

    // Allocate one block
    let ptr = unsafe { pm_util_mem_alloc(arena, 32) };
    assert!(!ptr.is_null());

    // Walk — we don't assert exact counts since arena internals differ by
    // platform, but the walk must not crash. The canonical walk takes the
    // callback as a `*mut c_void` slot, so hand the fn's address across.
    extern "C" fn count_blocks(_ptr: *mut c_void, _size: usize, used: i32, user: *mut c_void) {
        if used != 0 {
            let count = unsafe { &mut *(user as *mut u32) };
            *count += 1;
        }
    }

    let mut used = 0u32;
    unsafe {
        pm_util_mem_arena_walk(
            arena.cast_const(),
            count_blocks as *mut c_void,
            &mut used as *mut u32 as *mut c_void,
        );
    }
    assert!(used >= 1); // at least our allocation is "used"

    unsafe { pm_util_mem_free(arena, ptr) };
    // Free should not crash
    assert_eq!(unsafe { pm_util_mem_arena_check(arena.cast_const()) }, 0);
}

#[test]
fn arena_too_small_returns_null() {
    // One page is well under PM_UTIL_MEM_MIN_SPAN (8 pages): the arena refuses.
    let mut buf = Pages([0u8; 4096]);
    let arena = unsafe { pm_util_mem_arena_create(buf.0.as_mut_ptr() as *mut c_void, buf.0.len()) };
    assert!(arena.is_null());
}

#[test]
fn arena_check_null() {
    assert_ne!(unsafe { pm_util_mem_arena_check(core::ptr::null()) }, 0);
}

#[test]
fn arena_alloc_zero() {
    let mut buf = Pages([0u8; 16 * 4096]);
    let arena = unsafe { pm_util_mem_arena_create(buf.0.as_mut_ptr() as *mut c_void, buf.0.len()) };
    assert!(!arena.is_null());

    // Zero-size allocation — must return non-null (or null + check clean)
    let ptr = unsafe { pm_util_mem_alloc(arena, 0) };
    // Either valid pointer or null; arena check should still pass
    if !ptr.is_null() {
        unsafe { pm_util_mem_free(arena, ptr) };
    }
    assert_eq!(unsafe { pm_util_mem_arena_check(arena.cast_const()) }, 0);
}

#[test]
fn arena_alloc_free_multiple() {
    let mut buf = Pages([0u8; 16 * 4096]);
    let arena = unsafe { pm_util_mem_arena_create(buf.0.as_mut_ptr() as *mut c_void, buf.0.len()) };
    assert!(!arena.is_null());

    let a = unsafe { pm_util_mem_alloc(arena, 16) };
    let b = unsafe { pm_util_mem_alloc(arena, 32) };
    let c = unsafe { pm_util_mem_alloc(arena, 8) };
    assert!(!a.is_null());
    assert!(!b.is_null());
    assert!(!c.is_null());

    unsafe { pm_util_mem_free(arena, b) };
    unsafe { pm_util_mem_free(arena, a) };
    unsafe { pm_util_mem_free(arena, c) };

    assert_eq!(unsafe { pm_util_mem_arena_check(arena.cast_const()) }, 0);
}

#[test]
fn arena_walk_no_allocs() {
    let mut buf = Pages([0u8; 16 * 4096]);
    let arena = unsafe { pm_util_mem_arena_create(buf.0.as_mut_ptr() as *mut c_void, buf.0.len()) };
    assert!(!arena.is_null());

    // Walk on a fresh arena (no allocations yet) must not crash
    extern "C" fn nop(_ptr: *mut c_void, _size: usize, _used: i32, _user: *mut c_void) {}
    unsafe {
        pm_util_mem_arena_walk(arena.cast_const(), nop as *mut c_void, core::ptr::null_mut());
    }
    assert_eq!(unsafe { pm_util_mem_arena_check(arena.cast_const()) }, 0);
}
