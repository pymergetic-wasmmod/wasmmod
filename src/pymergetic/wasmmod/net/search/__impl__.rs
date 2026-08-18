//! pymergetic.wasmmod.net.search — impl. CDN pack list / search / filter over
//! the fetched channel index.
//!
//! One defining lang = Rust: serde_json owns the index parsing (no C JSON
//! parser, no duplicate `json.loads` in a Python binding). The `net.cdn` C
//! card stays the smoke-fetcher (`pm_wasmmod_net_cdn_fetch_index` returns a
//! stdlib-malloc'd buffer we free with libc `free` — the same pairing the µPy
//! face already used). Matching mirrors what the old µPy-only `wm.search` /
//! `wm.filter` did, so seats do not drift.
//!
//! Results are exposed the same way the registry exposes test/bench names: a
//! stateful result set + `count()` + indexed `*_at(buf, &len)`. After a
//! `catalog`/`search`/`filter` call the caller parks on
//! `name_at`/`meta_at` for the returned count. `meta_at` returns the pack's
//! raw JSON entry so any seat can re-inspect metadata, not just names.
#![allow(non_camel_case_types)]

use crate::util::lock::Mutex;

// `no_std` + `alloc`: `Vec` and `ToString` are not in the prelude.
use alloc::string::ToString;
use alloc::vec::Vec;

const ERR_CAP: usize = 160;

unsafe extern "C" {
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

#[derive(Default)]
struct SearchState {
    /// Matched pack names, sorted, deduped.
    names: alloc::vec::Vec<alloc::vec::Vec<u8>>,
    /// Raw JSON entry per matched name (same index as `names`).
    entries: alloc::vec::Vec<alloc::vec::Vec<u8>>,
    err: alloc::vec::Vec<u8>,
}

static STATE: Mutex<SearchState> = Mutex::new(SearchState {
    names: alloc::vec::Vec::new(),
    entries: alloc::vec::Vec::new(),
    err: alloc::vec::Vec::new(),
});

#[derive(Default, Clone)]
struct Query {
    prefix: Option<alloc::string::String>,
    name_contains: Option<alloc::string::String>,
    kind: Option<alloc::string::String>,
    arch: Option<alloc::string::String>,
    q: Option<alloc::string::String>,
}

fn c_str_opt(ptr: *const u8) -> Option<alloc::string::String> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: caller (an extern "C" fn below) contracts ptr to a NUL-terminated
    // string that stays alive for the duration of this call.
    let mut end = ptr;
    unsafe {
        while *end != 0 {
            end = end.add(1);
        }
    }
    let len = unsafe { end.offset_from(ptr) } as usize;
    if len == 0 {
        return None;
    }
    let bytes = unsafe { core::slice::from_raw_parts(ptr as *const u8, len) };
    alloc::string::String::from_utf8(bytes.to_vec()).ok()
}

/// Case-insensitive ASCII substring — mirrors `wasm_str_contains` so seats
/// agree on what "search" matches.
fn contains_ci(hay: &str, needle: &str) -> bool {
    if needle.is_empty() {
        return true;
    }
    let h = hay.as_bytes();
    let n = needle.as_bytes();
    if n.len() > h.len() {
        return false;
    }
    'outer: for i in 0..=(h.len() - n.len()) {
        for (j, &nb) in n.iter().enumerate() {
            if lower_ascii(h[i + j]) != lower_ascii(nb) {
                continue 'outer;
            }
        }
        return true;
    }
    false
}

const fn lower_ascii(b: u8) -> u8 {
    if b.is_ascii_uppercase() {
        b + (b'a' - b'A')
    } else {
        b
    }
}

/// Does one `PackageEntry` value satisfy `kind` / `arch`? Looks across its
/// top-level `artifacts` array, matching the µPy `filter` semantics.
fn entry_matches(value: &serde_json::Value, kind: &Option<alloc::string::String>,
    arch: &Option<alloc::string::String>) -> bool {
    if kind.is_none() && arch.is_none() {
        return true;
    }
    let Some(artifacts) = value.get("artifacts") else {
        return false;
    };
    let Some(arr) = artifacts.as_array() else {
        return false;
    };
    let mut kind_ok = kind.is_none();
    let mut arch_ok = arch.is_none();
    for a in arr {
        if !kind_ok {
            if let Some(v) = a.get("kind").and_then(serde_json::Value::as_str) {
                if v == kind.as_deref().unwrap_or_default() {
                    kind_ok = true;
                }
            }
        }
        if !arch_ok {
            if let Some(v) = a.get("arch").and_then(serde_json::Value::as_str) {
                if v == arch.as_deref().unwrap_or_default() {
                    arch_ok = true;
                }
            }
        }
        if kind_ok && arch_ok {
            break;
        }
    }
    kind_ok && arch_ok
}

fn matches(name: &str, value: &serde_json::Value, q: &Query) -> bool {
    if let Some(prefix) = &q.prefix {
        if !name.starts_with(prefix.as_str()) {
            return false;
        }
    }
    if let Some(nc) = &q.name_contains {
        if !contains_ci(name, nc.as_str()) {
            return false;
        }
    }
    if let Some(qs) = &q.q {
        if !contains_ci(name, qs.as_str()) {
            return false;
        }
    }
    entry_matches(value, &q.kind, &q.arch)
}

/// Parse the channel index JSON and keep every pack satisfying `q`.
fn parse_and_query(raw: &[u8], q: &Query) -> Result<alloc::vec::Vec<(alloc::vec::Vec<u8>, alloc::vec::Vec<u8>)>, alloc::string::String> {
    let doc: serde_json::Value = serde_json::from_slice(raw)
        .map_err(|e| alloc::format!("search: index JSON: {e}"))?;
    let packages = doc
        .get("packages")
        .and_then(serde_json::Value::as_object)
        .ok_or_else(|| alloc::format!("search: missing packages object"))?;

    let mut out = alloc::vec::Vec::new();
    for (name, value) in packages {
        if !matches(name, value, q) {
            continue;
        }
        out.push((name.as_bytes().to_vec(), value.to_string().into_bytes()));
    }
    // Stable, cross-seat ordering (server sorts names; the old µPy catalog
    // returned map order — sort here so host C / Rust / µPy agree).
    out.sort_by(|a, b| a.0.cmp(&b.0));
    Ok(out)
}

fn set_err(state: &mut SearchState, msg: &str) {
    state.err = msg.as_bytes().to_vec();
}

fn reset_ok(state: &mut SearchState, rows: Vec<(alloc::vec::Vec<u8>, alloc::vec::Vec<u8>)>) -> i32 {
    state.names = rows.iter().map(|(n, _)| n.clone()).collect();
    state.entries = rows.into_iter().map(|(_, e)| e).collect();
    state.err.clear();
    state.names.len() as i32
}

/// Fetch the channel index and run `q`, storing the result set.
fn run_query(channel: *const u8, q: &Query) -> i32 {
    let ch = c_str_opt(channel).unwrap_or_else(|| "lead".into());
    let ch_nul = cstr_bytes(&ch);
    let mut buf: *mut u8 = core::ptr::null_mut();
    let mut len: u32 = 0;
    let mut cerr = [0u8; ERR_CAP];
    let rc = unsafe {
        pm_wasmmod_net_cdn_fetch_index(
            ch_nul.as_ptr(),
            &mut buf,
            &mut len,
            cerr.as_mut_ptr(),
            cerr.len(),
        )
    };
    if rc != 0 || buf.is_null() {
        let msg = cstr_bytes_to_str(&cerr).unwrap_or("search: fetch index failed");
        let mut st = STATE.lock();
        set_err(&mut st, msg);
        return -1;
    }
    let raw = unsafe { core::slice::from_raw_parts(buf, len as usize) };
    let parsed = parse_and_query(raw, q);
    unsafe {
        free(buf as *mut core::ffi::c_void);
    }
    let mut st = STATE.lock();
    match parsed {
        Ok(rows) => reset_ok(&mut st, rows),
        Err(e) => {
            set_err(&mut st, &e);
            -1
        }
    }
}

fn cstr_bytes(s: &str) -> alloc::vec::Vec<u8> {
    let mut v = alloc::vec::Vec::with_capacity(s.len() + 1);
    v.extend_from_slice(s.as_bytes());
    v.push(0);
    v
}

fn cstr_bytes_to_str(b: &[u8]) -> Option<&str> {
    let end = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    core::str::from_utf8(&b[..end]).ok().map(|s| s.trim())
}

/// Empty the result set (all pack names on `channel`). Returns the count or -1.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_catalog(channel: *const u8) -> i32 {
    run_query(channel, &Query::default())
}

/// Result set = names whose lowercased substring equals `q` (case-insensitive).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_wasmmod_net_search_search(q: *const u8, channel: *const u8) -> i32 {
    let query = Query {
        q: c_str_opt(q),
        ..Default::default()
    };
    run_query(channel, &query)
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
    let query = Query {
        prefix: c_str_opt(prefix),
        name_contains: c_str_opt(name_contains),
        kind: c_str_opt(kind),
        arch: c_str_opt(arch),
        q: None,
    };
    run_query(channel, &query)
}

/// Number of names in the current result set.
#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_net_search_count() -> u32 {
    STATE.lock().names.len() as u32
}

/// Copy the i-th matched pack name into `buf`, `*buf_len_io` in/out (bytes).
/// Returns 1 on success (0 = out-of-range or too-small capacity reported via
/// `*buf_len_io` = required).
#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_net_search_name_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    let st = STATE.lock();
    let Some(name) = st.names.get(index as usize) else {
        if !buf_len_io.is_null() {
            unsafe { *buf_len_io = 0 };
        }
        return 0;
    };
    copy_bytes_to_buf(name, buf, buf_len_io)
}

/// Copy the i-th matched pack's raw JSON entry into `buf`.
#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_net_search_meta_at(
    index: u32,
    buf: *mut u8,
    buf_len_io: *mut u32,
) -> i32 {
    let st = STATE.lock();
    let Some(entry) = st.entries.get(index as usize) else {
        if !buf_len_io.is_null() {
            unsafe { *buf_len_io = 0 };
        }
        return 0;
    };
    copy_bytes_to_buf(entry, buf, buf_len_io)
}

/// Copy the last error message ("" if none). Same errbuf convention as io.
#[unsafe(no_mangle)]
pub extern "C" fn pm_wasmmod_net_search_last_error(buf: *mut u8, buf_len: usize) -> i32 {
    if buf.is_null() {
        return -1;
    }
    let st = STATE.lock();
    let e = if st.err.is_empty() {
        alloc::vec::Vec::new()
    } else {
        st.err.clone()
    };
    unsafe { core::ptr::write_bytes(buf, 0, buf_len) };
    let n = usize::min(e.len(), buf_len.saturating_sub(1));
    if n > 0 {
        unsafe {
            core::ptr::copy_nonoverlapping(e.as_ptr(), buf, n);
        }
    }
    1
}

fn copy_bytes_to_buf(src: &[u8], buf: *mut u8, buf_len_io: *mut u32) -> i32 {
    if buf_len_io.is_null() {
        return 0;
    }
    let need = src.len();
    let cap = unsafe { *buf_len_io } as usize;
    if cap < need {
        unsafe { *buf_len_io = need as u32 };
        return 0;
    }
    if need > 0 {
        unsafe {
            core::ptr::copy_nonoverlapping(src.as_ptr(), buf, need);
        }
    }
    unsafe { *buf_len_io = need as u32 };
    1
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_catalog, "int32_t(const char *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_search, "int32_t(const char *, const char *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_filter, "int32_t(const char *, const char *, const char *, const char *, const char *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_count, "uint32_t(void)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_name_at, "int32_t(uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_meta_at, "int32_t(uint32_t, uint8_t *, uint32_t *)");
crate::PM_MOD_EXPORT_RS!("pymergetic.wasmmod.net.search", pm_wasmmod_net_search_last_error, "int32_t(uint8_t *, size_t)");

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
