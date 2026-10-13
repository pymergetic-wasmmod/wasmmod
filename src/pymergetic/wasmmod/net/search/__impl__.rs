//! pymergetic.wasmmod.net.search — impl. CDN pack list / search / filter over
//! the fetched channel index.
//!
//! One defining lang = Rust. JSON is walked by hand over byte spans (ptr+len
//! discipline, no serde_json in the muscle — the subset has no closures and
//! this file must compile through the kernel's own rsx chain). The `net.cdn`
//! C card stays the smoke-fetcher (`pm_wasmmod_net_cdn_fetch_index` returns a
//! stdlib-malloc'd buffer we free with libc `free` — the same pairing the µPy
//! face already used). Matching mirrors what the old µPy-only `wm.search` /
//! `wm.filter` did, so seats do not drift.
//!
//! Results are exposed the same way the registry exposes test/bench names: a
//! stateful result set + `count()` + indexed `*_at(buf, &len)`. After a
//! `catalog`/`search`/`filter` call the caller parks on
//! `name_at`/`meta_at` for the returned count. `meta_at` returns the pack's
//! raw JSON entry so any seat can re-inspect metadata, not just names.
//!
//! Result set is fixed-capacity: RES_MAX rows, NAME_CAP/ENTRY_CAP bytes per
//! row, ERR_CAP for the message. Overflow reports -1 with a clear error —
//! same posture as every other fixed-budget state in this tree.

#![allow(non_camel_case_types)]

use core::cell::UnsafeCell;

const ERR_CAP: usize = 160;
const RES_MAX: usize = 256;
const NAME_CAP: usize = 128;
const ENTRY_CAP: usize = 4096;

/* pymergetic.util.lock — the project's one lock card (RS muscle, C ABI),
 * declared the same way asgi declares it: raw primitive, caller payload. */
#[repr(C)]
struct pm_util_lock_t {
    locked: u32,
}

unsafe extern "C" {
    fn pm_util_lock_acquire(l: *mut pm_util_lock_t);
    fn pm_util_lock_release(l: *mut pm_util_lock_t);
    // Pack/io buffers are `MICROPY_WASM_MALLOC` = stdlib malloc (no port
    // overrides it — see pack/alloc.h); `free` is the matching release.
    fn free(p: *mut core::ffi::c_void);
    fn pm_wasmmod_net_cdn_fetch_index(
        channel: *const u8,
        out_bytes: *mut *mut u8,
        out_len: *mut u32,
        errbuf: *mut u8,
        errbuf_len: usize,
    ) -> i32;
}

/* Transparent static discipline (same as asgi): Mut<T> is a one-field tuple
 * struct rsx maps to its inner type, UnsafeCell::get hands out the pointer. */
struct Mut<T>(UnsafeCell<T>);
unsafe impl<T> Sync for Mut<T> {}

struct SearchState {
    /* Matched pack names, sorted, deduped. */
    names: [[u8; NAME_CAP]; RES_MAX],
    names_len: [u32; RES_MAX],
    entries: [[u8; ENTRY_CAP]; RES_MAX],
    entries_len: [u32; RES_MAX],
    n: u32,
    err: [u8; ERR_CAP],
    err_len: u32,
}

static STATE_LOCK: Mut<pm_util_lock_t> = Mut(UnsafeCell::new(pm_util_lock_t { locked: 0 }));
static STATE: Mut<SearchState> = Mut(UnsafeCell::new(SearchState {
    names: [[0; NAME_CAP]; RES_MAX],
    names_len: [0; RES_MAX],
    entries: [[0; ENTRY_CAP]; RES_MAX],
    entries_len: [0; RES_MAX],
    n: 0,
    err: [0; ERR_CAP],
    err_len: 0,
}));

unsafe fn state_lock() -> *mut pm_util_lock_t {
    STATE_LOCK.0.get()
}

unsafe fn state() -> *mut SearchState {
    STATE.0.get()
}

/* Query: byte spans, null ptr = "no constraint". Lengths are explicit —
 * c-strings in, spans out. */
struct Query {
    prefix: *const u8,
    prefix_len: usize,
    name_contains: *const u8,
    name_contains_len: usize,
    kind: *const u8,
    kind_len: usize,
    arch: *const u8,
    arch_len: usize,
    q: *const u8,
    q_len: usize,
}

fn query_none() -> Query {
    Query {
        prefix: core::ptr::null(),
        prefix_len: 0,
        name_contains: core::ptr::null(),
        name_contains_len: 0,
        kind: core::ptr::null(),
        kind_len: 0,
        arch: core::ptr::null(),
        arch_len: 0,
        q: core::ptr::null(),
        q_len: 0,
    }
}

const fn lower_ascii(b: u8) -> u8 {
    if b >= b'A' && b <= b'Z' {
        b + (b'a' - b'A')
    } else {
        b
    }
}

/// Case-insensitive ASCII substring — mirrors `wasm_str_contains` so seats
/// agree on what "search" matches.
fn contains_ci(hay: *const u8, hay_len: usize, needle: *const u8, needle_len: usize) -> bool {
    if needle_len == 0 {
        return true;
    }
    if needle_len > hay_len {
        return false;
    }
    let last = hay_len - needle_len;
    let mut i = 0usize;
    while i <= last {
        let mut j = 0usize;
        let mut all_eq = true;
        while j < needle_len {
            unsafe {
                if lower_ascii(*hay.add(i + j)) != lower_ascii(*needle.add(j)) {
                    all_eq = false;
                }
            }
            if !all_eq {
                break;
            }
            j += 1;
        }
        if all_eq {
            return true;
        }
        i += 1;
    }
    false
}

fn span_eq(a: *const u8, a_len: usize, b: *const u8, b_len: usize) -> bool {
    if a_len != b_len {
        return false;
    }
    let mut i = 0usize;
    while i < a_len {
        unsafe {
            if *a.add(i) != *b.add(i) {
                return false;
            }
        }
        i += 1;
    }
    true
}

/* ---- hand JSON span-scanner (no serde_json in the muscle) ---- */

/// Skip whitespace forward from p[0..n); returns the first non-ws index.
fn skip_ws(p: *const u8, n: usize, mut i: usize) -> usize {
    while i < n {
        let c = unsafe { *p.add(i) };
        if c != b' ' && c != b'\t' && c != b'\n' && c != b'\r' {
            break;
        }
        i += 1;
    }
    i
}

/// Find a string value's span: at i expects `"`, returns (start,end)
/// *exclusive of the quotes*, advancing *end to the byte after the closing
/// quote. On malformed input returns (0,0).
unsafe fn scan_string(p: *const u8, n: usize, i: usize, end: *mut usize) -> (usize, usize) {
    let mut at = skip_ws(p, n, i);
    if at >= n || unsafe { *p.add(at) } != b'"' {
        return (0, 0);
    }
    at += 1;
    let start = at;
    while at < n {
        let c = unsafe { *p.add(at) };
        if c == b'\\' {
            /* escaped char — skip the escape pair */
            if at + 1 < n {
                at += 2;
                continue;
            }
            return (0, 0);
        }
        if c == b'"' {
            // SAFETY: end is the caller's writable output index.
            unsafe { *end = at + 1 };
            return (start, at);
        }
        at += 1;
    }
    (0, 0)
}

/// Scan a value at i, returning its [start,end) span (whitespace-trimmed)
/// and setting *end to the index just past it. Objects, arrays, strings,
/// numbers, true/false/null — the span is the raw JSON text.
unsafe fn scan_value(p: *const u8, n: usize, i: usize, end: *mut usize) -> (usize, usize) {
    let at = skip_ws(p, n, i);
    if at >= n {
        // SAFETY: end is writable; p is a valid raw_len-byte input span for subsequent scans.
        unsafe { *end = at };
        return (at, at);
    }
    let c = unsafe { *p.add(at) };
    if c == b'"' {
        // SAFETY: p spans n bytes and end is writable, as required by scan_value.
        return unsafe { scan_string(p, n, at, end) };
    }
    if c == b'{' || c == b'[' {
        // SAFETY: p spans n bytes; j stays below n and end is writable.
        unsafe {
            /* balanced-brace walk: strings inside may contain braces, so the
             * walker still honors quotes/escapes. */
            let open = c;
            let close = if open == b'{' { b'}' } else { b']' };
            let mut depth: i32 = 0;
            let mut j = at;
            while j < n {
                let d = *p.add(j);
                if d == b'"' {
                    let mut e: usize = 0;
                    let _ = scan_string(p, n, j, &mut e);
                    if e == 0 {
                        *end = j;
                        return (at, j);
                    }
                    j = e;
                    continue;
                }
                if d == open {
                    depth += 1;
                } else if d == close {
                    depth -= 1;
                    if depth == 0 {
                        *end = j + 1;
                        return (at, j + 1);
                    }
                }
                j += 1;
            }
            *end = j;
            return (at, j);
        }
    }
    /* number / true / false / null: run to the next delimiter. */
    // SAFETY: p spans n bytes; j stays below n and end is writable.
    unsafe {
        let mut j = at;
        while j < n {
            let d = *p.add(j);
            if d == b','
                || d == b'}'
                || d == b']'
                || d == b' '
                || d == b'\t'
                || d == b'\n'
                || d == b'\r'
            {
                break;
            }
            j += 1;
        }
        *end = j;
        (at, j)
    }
}

/// Find member `key` in the object spanning [i..n); returns the value span
/// or (0,0). Only top-level members — nested objects are skipped as raw
/// spans, which is exactly the one-level lookup the search needs.
unsafe fn obj_member(
    p: *const u8,
    n: usize,
    i: usize,
    key: *const u8,
    key_len: usize,
) -> (usize, usize) {
    // SAFETY: p spans n bytes, key spans key_len bytes; scanners stay in bounds.
    unsafe {
        let mut at = skip_ws(p, n, i);
        if at >= n || *p.add(at) != b'{' {
            return (0, 0);
        }
        at += 1;
        loop {
            at = skip_ws(p, n, at);
            if at >= n {
                return (0, 0);
            }
            let c = *p.add(at);
            if c == b'}' {
                return (0, 0);
            }
            if c != b'"' {
                return (0, 0);
            }
            let mut kend: usize = 0;
            let (ks, ke) = scan_string(p, n, at, &mut kend);
            if kend == 0 {
                return (0, 0);
            }
            at = kend;
            at = skip_ws(p, n, at);
            if at >= n || *p.add(at) != b':' {
                return (0, 0);
            }
            at += 1;
            let mut vend: usize = 0;
            let _ = scan_value(p, n, at, &mut vend);
            if span_eq(p.add(ks), ke - ks, key, key_len) {
                /* re-scan the value for the caller: vend already past it */
                let (vs, ve) = scan_value(p, n, at, &mut vend);
                let _ = vend;
                return (vs, ve);
            }
            at = vend;
            at = skip_ws(p, n, at);
            if at < n && *p.add(at) == b',' {
                at += 1;
            }
        }
    }
}

/// Does one `packages` entry satisfy `kind` / `arch`? Looks across its
/// top-level `artifacts` array, matching the µPy `filter` semantics.
unsafe fn entry_matches(
    p: *const u8,
    n: usize,
    vstart: usize,
    _vend_end: usize,
    q: *const Query,
) -> bool {
    // SAFETY: q is a valid query and p spans n bytes; scanners bound all offsets.
    unsafe {
        if (*q).kind_len == 0 && (*q).arch_len == 0 {
            return true;
        }
        let (as_, ae) = obj_member(p, n, vstart, b"artifacts\0".as_ptr(), 9);
        if ae == 0 {
            return false;
        }
        /* artifacts must be an array: walk its members. */
        let mut at = skip_ws(p, n, as_);
        if at >= n || *p.add(at) != b'[' {
            return false;
        }
        at += 1;
        let mut kind_ok = (*q).kind_len == 0;
        let mut arch_ok = (*q).arch_len == 0;
        loop {
            at = skip_ws(p, n, at);
            if at >= ae {
                break;
            }
            let c = *p.add(at);
            if c == b']' {
                break;
            }
            if c == b',' {
                at += 1;
                continue;
            }
            let mut vend2: usize = 0;
            let (os, oe) = scan_value(p, n, at, &mut vend2);
            let _ = oe;
            if os >= ae {
                break;
            }
            if !kind_ok {
                let (ks, ke) = obj_member(p, n, os, b"kind\0".as_ptr(), 4);
                if ke > ks && span_eq(p.add(ks), ke - ks, (*q).kind, (*q).kind_len) {
                    kind_ok = true;
                }
            }
            if !arch_ok {
                let (ks, ke) = obj_member(p, n, os, b"arch\0".as_ptr(), 4);
                if ke > ks && span_eq(p.add(ks), ke - ks, (*q).arch, (*q).arch_len) {
                    arch_ok = true;
                }
            }
            if kind_ok && arch_ok {
                break;
            }
            at = vend2;
        }
        kind_ok && arch_ok
    }
}

/// Does one package (name span known, raw value at [vstart, vend)) satisfy
/// the query? Prefix / contains checks on the name, kind/arch on the entry.
unsafe fn matches(
    p: *const u8,
    n: usize,
    ns: usize,
    ne: usize,
    vstart: usize,
    vend: usize,
    q: *const Query,
) -> bool {
    // SAFETY: q and its nonempty constraints are valid; ns..ne and entry span lie in p[0..n].
    unsafe {
        let name_len = ne - ns;
        if (*q).prefix_len > 0 {
            if name_len < (*q).prefix_len {
                return false;
            }
            if !span_eq(p.add(ns), (*q).prefix_len, (*q).prefix, (*q).prefix_len) {
                return false;
            }
        }
        if (*q).name_contains_len > 0 {
            if !contains_ci(
                p.add(ns),
                name_len,
                (*q).name_contains,
                (*q).name_contains_len,
            ) {
                return false;
            }
        }
        if (*q).q_len > 0 {
            if !contains_ci(p.add(ns), name_len, (*q).q, (*q).q_len) {
                return false;
            }
        }
        entry_matches(p, n, vstart, vend, q)
    }
}

/// Store one matched row (name + raw entry) into the fixed-cap state,
/// insertion-sorted by name (byte order — the same order serde_json's
/// sort_by produced, pinned by the tests). Dedups on exact name match.
unsafe fn store_row(
    st: *mut SearchState,
    name: *const u8,
    name_len: usize,
    entry: *const u8,
    entry_len: usize,
) -> i32 {
    // SAFETY: st is exclusively held under STATE_LOCK; name and entry span their given lengths.
    unsafe {
        if name_len > NAME_CAP || entry_len > ENTRY_CAP {
            return -1;
        }
        let n = (*st).n;
        if n as usize >= RES_MAX {
            return -1;
        }
        /* insertion point by byte order */
        let mut pos: u32 = 0;
        while pos < n {
            let clen = (*st).names_len[pos as usize] as usize;
            let cur = (*st).names[pos as usize].as_ptr();
            let ord = span_cmp(cur, clen, name, name_len);
            if ord == 0 {
                /* duplicate name — replace the entry, keep one row */
                copy_into(
                    (*st).entries[pos as usize].as_mut_ptr(),
                    ENTRY_CAP,
                    entry,
                    entry_len,
                );
                (*st).entries_len[pos as usize] = entry_len as u32;
                return 0;
            }
            if ord > 0 {
                break;
            }
            pos += 1;
        }
        /* shift rows [pos..n) up one */
        let mut k = n;
        while k > pos {
            copy_row(st, (k - 1) as usize, k as usize);
            k -= 1;
        }
        copy_into(
            (*st).names[pos as usize].as_mut_ptr(),
            NAME_CAP,
            name,
            name_len,
        );
        (*st).names_len[pos as usize] = name_len as u32;
        copy_into(
            (*st).entries[pos as usize].as_mut_ptr(),
            ENTRY_CAP,
            entry,
            entry_len,
        );
        (*st).entries_len[pos as usize] = entry_len as u32;
        (*st).n = n + 1;
        0
    }
}

/// memcmp-style span compare: <0, 0, >0.
fn span_cmp(a: *const u8, a_len: usize, b: *const u8, b_len: usize) -> i32 {
    let n = if a_len < b_len { a_len } else { b_len };
    let mut i = 0usize;
    while i < n {
        unsafe {
            let ca = *a.add(i);
            let cb = *b.add(i);
            if ca != cb {
                return ca as i32 - cb as i32;
            }
        }
        i += 1;
    }
    if a_len == b_len {
        0
    } else if a_len < b_len {
        -1
    } else {
        1
    }
}

unsafe fn copy_into(dst: *mut u8, dst_cap: usize, src: *const u8, n: usize) {
    // SAFETY: dst spans dst_cap writable bytes; src spans min(n, dst_cap) readable bytes.
    unsafe {
        let mut i = 0usize;
        while i < n && i < dst_cap {
            *dst.add(i) = *src.add(i);
            i += 1;
        }
    }
}

unsafe fn copy_row(st: *mut SearchState, from: usize, to: usize) {
    // SAFETY: st is locked and from/to are both within RES_MAX; row buffers are disjoint.
    unsafe {
        copy_into(
            (*st).names[to].as_mut_ptr(),
            NAME_CAP,
            (*st).names[from].as_ptr(),
            (*st).names_len[from] as usize,
        );
        copy_into(
            (*st).entries[to].as_mut_ptr(),
            ENTRY_CAP,
            (*st).entries[from].as_ptr(),
            (*st).entries_len[from] as usize,
        );
        (*st).names_len[to] = (*st).names_len[from];
        (*st).entries_len[to] = (*st).entries_len[from];
    }
}

unsafe fn set_err(st: *mut SearchState, msg: *const u8, msg_len: usize) {
    // SAFETY: st is locked and writable; msg spans msg_len readable bytes.
    unsafe {
        let n = if msg_len < ERR_CAP { msg_len } else { ERR_CAP };
        copy_into((*st).err.as_mut_ptr(), ERR_CAP, msg, n);
        (*st).err_len = n as u32;
    }
}

/* NUL-terminated error literals (the subset's length discipline: explicit
 * zlen at the call site, never slice `.len()`). */
const ERR_FETCH: &[u8] = b"search: fetch index failed\0";
const ERR_PARSE: &[u8] = b"search: index JSON: no packages object\0";
const ERR_CAPS: &[u8] = b"search: result capacity exceeded\0";

/// strlen over a NUL-terminated byte span.
unsafe fn zlen(p: *const u8) -> usize {
    // SAFETY: p is a readable NUL-terminated string.
    unsafe {
        let mut n = 0usize;
        while *p.add(n) != 0 {
            n += 1;
        }
        n
    }
}

/// Parse the channel index JSON and keep every pack satisfying `q`.
/// Returns the count, or -1 with err set in the state.
unsafe fn parse_and_query(raw: *const u8, raw_len: usize, q: *const Query) -> i32 {
    // SAFETY: raw spans raw_len bytes; q is valid; all state access is locked.
    unsafe {
        /* locate the top-level "packages" object */
        let (ps, pe) = obj_member(raw, raw_len, 0, b"packages\0".as_ptr(), 8);
        if pe == 0 {
            let st = state();
            pm_util_lock_acquire(state_lock());
            set_err(st, ERR_PARSE.as_ptr(), zlen(ERR_PARSE.as_ptr()));
            pm_util_lock_release(state_lock());
            return -1;
        }
        /* walk the packages object's members: name -> raw entry */
        let mut at = skip_ws(raw, raw_len, ps);
        if at >= raw_len || *raw.add(at) != b'{' {
            return -1;
        }
        at += 1;
        /* lock once for the whole fill: rows go straight into the state */
        let st = state();
        pm_util_lock_acquire(state_lock());
        (*st).n = 0;
        (*st).err_len = 0;
        let mut count: i32 = 0;
        loop {
            at = skip_ws(raw, raw_len, at);
            if at >= pe {
                break;
            }
            let c = *raw.add(at);
            if c == b'}' {
                break;
            }
            if c == b',' {
                at += 1;
                continue;
            }
            let mut kend: usize = 0;
            let (ns, ne) = scan_string(raw, raw_len, at, &mut kend);
            if kend == 0 {
                break;
            }
            at = kend;
            at = skip_ws(raw, raw_len, at);
            if at >= raw_len || *raw.add(at) != b':' {
                break;
            }
            at += 1;
            let mut vend: usize = 0;
            let (vs, ve) = scan_value(raw, raw_len, at, &mut vend);
            if ve == 0 {
                break;
            }
            if matches(raw, raw_len, ns, ne, vs, ve, q) {
                if store_row(st, raw.add(ns), ne - ns, raw.add(vs), ve - vs) != 0 {
                    set_err(st, ERR_CAPS.as_ptr(), zlen(ERR_CAPS.as_ptr()));
                    pm_util_lock_release(state_lock());
                    return -1;
                }
                count += 1;
            }
            at = vend;
        }
        pm_util_lock_release(state_lock());
        count
    }
}

/// Fetch the channel index and run `q`, storing the result set.
unsafe fn run_query(channel: *const u8, q: *const Query) -> i32 {
    // SAFETY: channel is NULL or a C string, q valid for the duration of the fetch;
    // the CDN allocates buf with malloc, and free releases it after parsing.
    unsafe {
        let ch_ptr: *const u8 = if channel.is_null() {
            b"lead\0".as_ptr()
        } else {
            channel
        };
        let mut buf: *mut u8 = core::ptr::null_mut();
        let mut len: u32 = 0;
        let mut cerr = [0u8; ERR_CAP];
        let rc =
            pm_wasmmod_net_cdn_fetch_index(ch_ptr, &mut buf, &mut len, cerr.as_mut_ptr(), ERR_CAP);
        if rc != 0 || buf.is_null() {
            let st = state();
            pm_util_lock_acquire(state_lock());
            let mut msg_at: usize = 0;
            while msg_at < ERR_CAP && cerr[msg_at] != 0 {
                msg_at += 1;
            }
            if msg_at == 0 {
                set_err(st, ERR_FETCH.as_ptr(), zlen(ERR_FETCH.as_ptr()));
            } else {
                set_err(st, cerr.as_ptr(), msg_at);
            }
            pm_util_lock_release(state_lock());
            return -1;
        }
        let out = parse_and_query(buf, len as usize, q);
        free(buf as *mut core::ffi::c_void);
        out
    }
}

/// NUL-terminated arg -> (ptr, len). Null ptr or empty string = no
/// constraint (len 0).
unsafe fn c_arg(ptr: *const u8, out_len: *mut usize) -> *const u8 {
    // SAFETY: out_len is writable; ptr is NULL or a readable NUL-terminated C string.
    unsafe {
        if ptr.is_null() {
            *out_len = 0;
            return core::ptr::null();
        }
        let mut n = 0usize;
        while *ptr.add(n) != 0 {
            n += 1;
        }
        if n == 0 {
            *out_len = 0;
            return core::ptr::null();
        }
        *out_len = n;
        ptr
    }
}

/// Empty the result set (all pack names on `channel`). Returns the count or -1.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_catalog(channel: *const u8) -> i32 {
    let q = query_none();
    // SAFETY: the C caller supplies a NULL or NUL-terminated channel; q lives through run_query.
    unsafe { run_query(channel, &q) }
}

/// Result set = names whose lowercased substring equals `q` (case-insensitive).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_search(q: *const u8, channel: *const u8) -> i32 {
    // SAFETY: C string arguments are NULL or readable NUL-terminated strings;
    // query and its lengths live until run_query finishes.
    unsafe {
        let mut query = query_none();
        let mut l: usize = 0;
        query.q = c_arg(q, &mut l);
        query.q_len = l;
        run_query(channel, &query)
    }
}

/// Result set = names matching all of prefix / name_contains / kind / arch.
/// Each argument is a NUL-terminated string or NULL for "no constraint".
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_filter(
    prefix: *const u8,
    name_contains: *const u8,
    kind: *const u8,
    arch: *const u8,
    channel: *const u8,
) -> i32 {
    // SAFETY: C string arguments are NULL or readable NUL-terminated strings;
    // query and its lengths live until run_query finishes.
    unsafe {
        let mut query = query_none();
        let mut l: usize = 0;
        query.prefix = c_arg(prefix, &mut l);
        query.prefix_len = l;
        query.name_contains = c_arg(name_contains, &mut l);
        query.name_contains_len = l;
        query.kind = c_arg(kind, &mut l);
        query.kind_len = l;
        query.arch = c_arg(arch, &mut l);
        query.arch_len = l;
        run_query(channel, &query)
    }
}

/// Number of names in the current result set.
#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_net_search_count() -> u32 {
    unsafe {
        pm_util_lock_acquire(state_lock());
        let n = (*state()).n;
        pm_util_lock_release(state_lock());
        n
    }
}

/// Copy the i-th matched pack name into `buf`, `*buf_len_io` in/out (bytes).
/// Returns 1 on success (0 = out-of-range or too-small capacity reported via
/// `*buf_len_io` = required).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_name_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    // SAFETY: STATE_LOCK guards st; buf_len_io is NULL or writable, buf is writable
    // for the capacity it advertises when enough space is available.
    unsafe {
        pm_util_lock_acquire(state_lock());
        let st = state();
        if index >= (*st).n {
            if !buf_len_io.is_null() {
                *buf_len_io = 0;
            }
            pm_util_lock_release(state_lock());
            return 0;
        }
        let i = index as usize;
        let rc = copy_bytes_to_buf(
            (*st).names[i].as_ptr(),
            (*st).names_len[i] as usize,
            buf,
            buf_len_io,
        );
        pm_util_lock_release(state_lock());
        rc
    }
}

/// Copy the i-th matched pack's raw JSON entry into `buf`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_meta_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    // SAFETY: STATE_LOCK guards st; buf_len_io is NULL or writable, buf is writable
    // for the capacity it advertises when enough space is available.
    unsafe {
        pm_util_lock_acquire(state_lock());
        let st = state();
        if index >= (*st).n {
            if !buf_len_io.is_null() {
                *buf_len_io = 0;
            }
            pm_util_lock_release(state_lock());
            return 0;
        }
        let i = index as usize;
        let rc = copy_bytes_to_buf(
            (*st).entries[i].as_ptr(),
            (*st).entries_len[i] as usize,
            buf,
            buf_len_io,
        );
        pm_util_lock_release(state_lock());
        rc
    }
}

/// Copy the last error message ("" if none). Same errbuf convention as io.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_last_error(buf: *mut u8, buf_len: usize) -> i32 {
    if buf.is_null() {
        return -1;
    }
    // SAFETY: buf spans buf_len writable bytes and STATE_LOCK guards st.
    unsafe {
        pm_util_lock_acquire(state_lock());
        let st = state();
        let e_len = (*st).err_len as usize;
        let mut z = 0usize;
        while z < buf_len {
            *buf.add(z) = 0;
            z += 1;
        }
        let n = if e_len < buf_len.saturating_sub(1) {
            e_len
        } else {
            buf_len.saturating_sub(1)
        };
        if n > 0 {
            core::ptr::copy_nonoverlapping((*st).err.as_ptr(), buf, n);
        }
        pm_util_lock_release(state_lock());
        1
    }
}

fn copy_bytes_to_buf(src: *const u8, src_len: usize, buf: *mut u8, buf_len_io: *mut u32) -> i32 {
    unsafe {
        if buf_len_io.is_null() {
            return 0;
        }
        let need = src_len;
        let cap = *buf_len_io as usize;
        if cap < need {
            *buf_len_io = need as u32;
            return 0;
        }
        if need > 0 {
            core::ptr::copy_nonoverlapping(src, buf, need);
        }
        *buf_len_io = need as u32;
    }
    1
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_catalog,
    "int32_t(const char *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_search,
    "int32_t(const char *, const char *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_filter,
    "int32_t(const char *, const char *, const char *, const char *, const char *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_count,
    "uint32_t(void)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_name_at,
    "int32_t(uint32_t, uint8_t *, uint32_t *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_meta_at,
    "int32_t(uint32_t, uint8_t *, uint32_t *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.net.search",
    pm_wasmmod_net_search_last_error,
    "int32_t(uint8_t *, size_t)"
);

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
