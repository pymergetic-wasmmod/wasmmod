//! pymergetic.wasmmod.net.search — unit tests. These prove the JSON parsing /
//! matching semantics without a live CDN: parse_and_query is pure input→rows,
//! so a hardcoded index exercises catalog/search/filter exactly as a fetch
//! would. The result-set ABI (count/name_at/meta_at) is tested against a query
//! injected by calling parse directly — no network.
//!
//! Same ptr+len discipline as the impl: the index bytes are a fixed array,
//! assertions walk byte spans, no closures, no Vec.

use super::*;

/// NUL-terminated test index (the compiler's own `b"...\0"` + explicit-len
/// discipline: rsx refuses `.len()`, so the byte count is walked once here).
const INDEX: &[u8] = b"{\"schema\":1,\"channel\":\"lead\",\"generated\":\"2026-08-04T00:00:00Z\",\"packages\":{\n  \"zlib\":{\"version\":\"1.2.12\",\"artifacts\":[{\"kind\":\"wasm\",\"arch\":\"x86_64\",\"encoding\":\"zlib\"},{\"kind\":\"elf\",\"arch\":\"x86\",\"encoding\":\"zlib\"}]},\n  \"pymergetic.metal.coop\":{\"version\":\"0.1.0\",\"artifacts\":[{\"kind\":\"elf\",\"arch\":\"aarch64\",\"encoding\":\"zlib\"}]},\n  \"hello\":{\"version\":\"0.2.1\",\"description\":\"sample\",\"artifacts\":[{\"kind\":\"aot\",\"arch\":\"x86_64\",\"encoding\":\"zlib\"}]},\n  \"WasmKit\":{\"version\":\"1.0.0\",\"artifacts\":[{\"kind\":\"wasm\",\"arch\":\"wasm32\",\"encoding\":\"direct\"}]}\n}}\n\0";

/// strlen over a NUL-terminated byte span — the subset's length discipline.
unsafe fn zlen(p: *const u8) -> usize {
    let mut n = 0usize;
    while *p.add(n) != 0 {
        n += 1;
    }
    n
}

/// Seed STATE by running parse_and_query on the hardcoded index; returns the
/// count. No network — the stateful ABI is tested against this result set.
unsafe fn seed(q: *const Query) -> i32 {
    parse_and_query(INDEX.as_ptr(), zlen(INDEX.as_ptr()), q)
}

/// i-th name as a fixed span compare — true when it equals want.
unsafe fn name_is(want: *const u8, want_len: usize, i: u32) -> bool {
    let mut buf = [0u8; 256];
    let mut len: u32 = 256;
    if pm_wasmmod_net_search_name_at(i, buf.as_mut_ptr(), &mut len) != 1 {
        return false;
    }
    span_eq(buf.as_ptr(), len as usize, want, want_len)
}

unsafe extern "C" fn case_catalog_lists_every_pack_name_sorted() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_catalog_lists_every_pack_name_sorted_body) }
}
unsafe extern "C" fn case_catalog_lists_every_pack_name_sorted_body() -> i32 {
    let q = query_none();
    let n = seed(&q);
    if n != 4 {
        return 1;
    }
    /* byte-sorted: WasmKit < hello < pymergetic.metal.coop < zlib */
    if !name_is(b"WasmKit\0".as_ptr(), 7, 0) {
        return 1;
    }
    if !name_is(b"hello\0".as_ptr(), 5, 1) {
        return 1;
    }
    if !name_is(b"pymergetic.metal.coop\0".as_ptr(), 21, 2) {
        return 1;
    }
    if !name_is(b"zlib\0".as_ptr(), 4, 3) {
        return 1;
    }
    0
}

unsafe extern "C" fn case_search_matches_name_substring_ci() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_search_matches_name_substring_ci_body) }
}
unsafe extern "C" fn case_search_matches_name_substring_ci_body() -> i32 {
    let mut q = query_none();
    q.q = b"wasm\0".as_ptr();
    q.q_len = 4;
    let n = seed(&q);
    if n != 1 {
        return 1;
    }
    if !name_is(b"WasmKit\0".as_ptr(), 7, 0) {
        return 1;
    }
    q.q = b"metal\0".as_ptr();
    q.q_len = 5;
    let n2 = seed(&q);
    if n2 != 1 {
        return 1;
    }
    if !name_is(b"pymergetic.metal.coop\0".as_ptr(), 21, 0) {
        return 1;
    }
    0
}

unsafe extern "C" fn case_filter_by_prefix() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_filter_by_prefix_body) }
}
unsafe extern "C" fn case_filter_by_prefix_body() -> i32 {
    let mut q = query_none();
    q.prefix = b"pymergetic.\0".as_ptr();
    q.prefix_len = 11;
    let n = seed(&q);
    if n != 1 {
        return 1;
    }
    if !name_is(b"pymergetic.metal.coop\0".as_ptr(), 21, 0) {
        return 1;
    }
    0
}

unsafe extern "C" fn case_filter_by_kind_and_arch() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_filter_by_kind_and_arch_body) }
}
unsafe extern "C" fn case_filter_by_kind_and_arch_body() -> i32 {
    /* x86_64 wasm (zlib) and aot (hello) both match arch; kind narrows. */
    let mut q = query_none();
    q.kind = b"wasm\0".as_ptr();
    q.kind_len = 4;
    q.arch = b"x86_64\0".as_ptr();
    q.arch_len = 6;
    let n = seed(&q);
    if n != 1 {
        return 1;
    }
    if !name_is(b"zlib\0".as_ptr(), 4, 0) {
        return 1;
    }
    /* aarch64 elf only. */
    let mut q2 = query_none();
    q2.kind = b"elf\0".as_ptr();
    q2.kind_len = 3;
    q2.arch = b"aarch64\0".as_ptr();
    q2.arch_len = 7;
    let n2 = seed(&q2);
    if n2 != 1 {
        return 1;
    }
    if !name_is(b"pymergetic.metal.coop\0".as_ptr(), 21, 0) {
        return 1;
    }
    /* Kind alone: anything with a wasm artifact. */
    let mut q3 = query_none();
    q3.kind = b"wasm\0".as_ptr();
    q3.kind_len = 4;
    let n3 = seed(&q3);
    if n3 != 2 {
        return 1;
    }
    if !name_is(b"WasmKit\0".as_ptr(), 7, 0) {
        return 1;
    }
    if !name_is(b"zlib\0".as_ptr(), 4, 1) {
        return 1;
    }
    0
}

unsafe extern "C" fn case_result_set_abi_and_error_state() -> i32 {
    unsafe { crate::util::mod_test::case_rc(case_result_set_abi_and_error_state_body) }
}
unsafe extern "C" fn case_result_set_abi_and_error_state_body() -> i32 {
    let mut q = query_none();
    q.kind = b"wasm\0".as_ptr();
    q.kind_len = 4;
    let _ = seed(&q);
    if pm_wasmmod_net_search_count() != 2 {
        return 1;
    }

    let mut buf = [0u8; 512];
    let mut len: u32 = 512;
    if pm_wasmmod_net_search_name_at(0, buf.as_mut_ptr(), &mut len) != 1 {
        return 1;
    }
    if len != 7 {
        return 1;
    }

    let mut mlen: u32 = 512;
    if pm_wasmmod_net_search_meta_at(0, buf.as_mut_ptr(), &mut mlen) != 1 {
        return 1;
    }
    /* the raw entry must contain "kind":"wasm" */
    let mut found = false;
    let mut i = 0usize;
    while i + 13 <= mlen as usize {
        if span_eq(buf.as_ptr().add(i), 13, b"\"kind\":\"wasm\"\0".as_ptr(), 13) {
            found = true;
        }
        i += 1;
    }
    if !found {
        return 1;
    }

    /* Name 1 is zlib (sorted: WasmKit, zlib). */
    let mut len2: u32 = 512;
    if pm_wasmmod_net_search_name_at(1, buf.as_mut_ptr(), &mut len2) != 1 {
        return 1;
    }
    if len2 != 4 {
        return 1;
    }

    /* Out of range → 0. */
    let mut len3: u32 = 0;
    if pm_wasmmod_net_search_name_at(99, buf.as_mut_ptr(), &mut len3) != 0 {
        return 1;
    }

    /* No error after a clean query. */
    let mut err = [0u8; 64];
    pm_wasmmod_net_search_last_error(err.as_mut_ptr(), 64);
    if err[0] != 0 {
        return 1;
    }
    0
}

crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.net.search",
    "catalog_lists_every_pack_name_sorted",
    case_catalog_lists_every_pack_name_sorted
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.net.search",
    "search_matches_name_substring_ci",
    case_search_matches_name_substring_ci
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.net.search",
    "filter_by_prefix",
    case_filter_by_prefix
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.net.search",
    "filter_by_kind_and_arch",
    case_filter_by_kind_and_arch
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.wasmmod.net.search",
    "result_set_abi_and_error_state",
    case_result_set_abi_and_error_state
);

#[test]
fn catalog_lists_every_pack_name_sorted() {
    assert_eq!(unsafe { case_catalog_lists_every_pack_name_sorted() }, 0);
}

#[test]
fn search_matches_name_substring_ci() {
    assert_eq!(unsafe { case_search_matches_name_substring_ci() }, 0);
}

#[test]
fn filter_by_prefix() {
    assert_eq!(unsafe { case_filter_by_prefix() }, 0);
}

#[test]
fn filter_by_kind_and_arch() {
    assert_eq!(unsafe { case_filter_by_kind_and_arch() }, 0);
}

#[test]
fn result_set_abi_and_error_state() {
    assert_eq!(unsafe { case_result_set_abi_and_error_state() }, 0);
}
