//! pymergetic.util.mem tests — arena creation / walk / check.

extern "C" {
    fn pm_util_mem_arena_create(buf: *mut core::ffi::c_void, size: usize) -> *mut core::ffi::c_void;
    fn pm_util_mem_alloc(arena: *mut core::ffi::c_void, size: usize) -> *mut core::ffi::c_void;
    fn pm_util_mem_free(arena: *mut core::ffi::c_void, ptr: *mut core::ffi::c_void);
    fn pm_util_mem_arena_walk(
        arena: *const core::ffi::c_void,
        cb: pm_util_mem_walk_fn,
        user: *mut core::ffi::c_void,
    );
    fn pm_util_mem_arena_check(arena: *const core::ffi::c_void) -> i32;
}

type pm_util_mem_walk_fn = extern "C" fn(*mut core::ffi::c_void, usize, i32, *mut core::ffi::c_void);

#[test]
fn arena_create_small() {
    // 3 pages = minimal arena (2 pages min span + 1 page guard)
    let mut buf = [0u8; 12288]; // 3 × 4096
    let arena = pm_util_mem_arena_create(
        buf.as_mut_ptr() as *mut core::ffi::c_void,
        buf.len(),
    );
    assert!(!arena.is_null());
    // Check must return 0 for a valid arena
    assert_eq!(pm_util_mem_arena_check(arena as *const core::ffi::c_void), 0);
}

#[test]
fn arena_walk_and_free() {
    let mut buf = [0u8; 12288]; // 3 pages
    let arena = pm_util_mem_arena_create(
        buf.as_mut_ptr() as *mut core::ffi::c_void,
        buf.len(),
    );
    assert!(!arena.is_null());

    // Allocate one block
    let ptr = pm_util_mem_alloc(arena, 32);
    assert!(!ptr.is_null());

    // Walk — we don't assert exact counts since arena internals differ by platform
    // but the walk function must not crash
    extern "C" fn count_blocks(
        _ptr: *mut core::ffi::c_void,
        _size: usize,
        used: i32,
        user: *mut core::ffi::c_void,
    ) {
        if used != 0 {
            let count = unsafe { &mut *(user as *mut u32) };
            *count += 1;
        }
    }

    let mut used = 0u32;
    pm_util_mem_arena_walk(
        arena as *const core::ffi::c_void,
        count_blocks,
        &mut used as *mut u32 as *mut core::ffi::c_void,
    );
    assert!(used >= 1); // at least our allocation is "used"

    pm_util_mem_free(arena, ptr);
    // Free should not crash
    assert_eq!(pm_util_mem_arena_check(arena as *const core::ffi::c_void), 0);
}

#[test]
fn arena_too_small_returns_null() {
    // Less than PM_UTIL_MEM_MIN_SPAN pages
    let mut buf = [0u8; 1];
    let arena = pm_util_mem_arena_create(
        buf.as_mut_ptr() as *mut core::ffi::c_void,
        buf.len(),
    );
    assert!(arena.is_null());
}

#[test]
fn arena_check_null() {
    assert_ne!(
        pm_util_mem_arena_check(core::ptr::null()),
        0
    );
}

#[test]
fn arena_alloc_zero() {
    let mut buf = [0u8; 12288];
    let arena = pm_util_mem_arena_create(
        buf.as_mut_ptr() as *mut core::ffi::c_void,
        buf.len(),
    );
    assert!(!arena.is_null());

    // Zero-size allocation — must return non-null (or null + check clean)
    let ptr = pm_util_mem_alloc(arena, 0);
    // Either valid pointer or null; arena check should still pass
    if !ptr.is_null() {
        pm_util_mem_free(arena, ptr);
    }
    assert_eq!(pm_util_mem_arena_check(arena as *const core::ffi::c_void), 0);
}

#[test]
fn arena_alloc_free_multiple() {
    let mut buf = [0u8; 12288];
    let arena = pm_util_mem_arena_create(
        buf.as_mut_ptr() as *mut core::ffi::c_void,
        buf.len(),
    );
    assert!(!arena.is_null());

    let a = pm_util_mem_alloc(arena, 16);
    let b = pm_util_mem_alloc(arena, 32);
    let c = pm_util_mem_alloc(arena, 8);
    assert!(!a.is_null());
    assert!(!b.is_null());
    assert!(!c.is_null());

    pm_util_mem_free(arena, b);
    pm_util_mem_free(arena, a);
    pm_util_mem_free(arena, c);

    assert_eq!(pm_util_mem_arena_check(arena as *const core::ffi::c_void), 0);
}

#[test]
fn arena_walk_no_allocs() {
    let mut buf = [0u8; 12288];
    let arena = pm_util_mem_arena_create(
        buf.as_mut_ptr() as *mut core::ffi::c_void,
        buf.len(),
    );
    assert!(!arena.is_null());

    // Walk on a fresh arena (no allocations yet) must not crash
    extern "C" fn nop(
        _ptr: *mut core::ffi::c_void,
        _size: usize,
        _used: i32,
        _user: *mut core::ffi::c_void,
    ) {
    }
    pm_util_mem_arena_walk(
        arena as *const core::ffi::c_void,
        nop,
        core::ptr::null_mut(),
    );
    assert_eq!(pm_util_mem_arena_check(arena as *const core::ffi::c_void), 0);
}