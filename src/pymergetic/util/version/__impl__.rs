//! pymergetic.util.version — semver parse / compare / satisfy for pack deps
//! and kernel module pins. One lib; product versions live on registry entries.
//!
//! Kernel-discipline: ptr+len pairs everywhere (the kernel's rsx subset
//! carries no slice lengths), no allocator — the pre-release tag lives in a
//! fixed inline buffer instead of a String.

#![allow(clippy::missing_safety_doc)]

/// Pre-release tag cap: semver pres and PEP 440 suffixes are short tags
/// (`alpha.2`, `rc.1`, `a2`); anything longer parses as unparsable.
const PRE_CAP: usize = 32;

struct SemVer {
    major: u64,
    minor: u64,
    patch: u64,
    /// Empty = release. Pre-release sorts *before* the same numbers without
    /// one. Inline bytes, NUL-terminated, `pre_len == 0` means release.
    pre: [u8; PRE_CAP],
    pre_len: usize,
}

impl SemVer {
    fn release(major: u64, minor: u64, patch: u64) -> SemVer {
        SemVer {
            major,
            minor,
            patch,
            pre: [0; PRE_CAP],
            pre_len: 0,
        }
    }
}

/// Parse an all-ASCII-digit run into a u64. `s..e` is the candidate span;
/// returns `None` (usize::MAX sentinel not used — rsx has no Option<u64>
/// convenience here, so a parse failure is `ok == false` + 0 value).
unsafe fn parse_num(s: *const u8, len: usize, ok: *mut bool) -> u64 {
    let mut v: u64 = 0;
    let mut i: usize = 0;
    *ok = false;
    if len == 0 {
        return 0;
    }
    while i < len {
        let b = unsafe { *s.add(i) };
        if b < b'0' || b > b'9' {
            return 0;
        }
        let d = (b - b'0') as u64;
        /* saturate instead of wrap: a >u64 numeric segment is unparsable,
         * not a wrapped-around huge number */
        if v > 0xFFFFFFFFFFFFFFFFu64 / 10u64 {
            return 0;
        }
        v = v * 10u64;
        if v > 0xFFFFFFFFFFFFFFFFu64 - d {
            return 0;
        }
        v = v + d;
        i += 1;
    }
    *ok = true;
    v
}

/// Skip ASCII whitespace at both ends; returns the trimmed span as
/// (offset, len) within the original buffer.
unsafe fn trim(s: *const u8, len: usize) -> (usize, usize) {
    let mut a: usize = 0;
    let mut b: usize = len;
    while a < b {
        let c = unsafe { *s.add(a) };
        if c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' {
            a += 1;
        } else {
            break;
        }
    }
    while b > a {
        let c = unsafe { *s.add(b - 1) };
        if c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' {
            b -= 1;
        } else {
            break;
        }
    }
    (a, b - a)
}

/// Does the span equal the literal? (length-aware, no NUL needed)
unsafe fn span_eq(s: *const u8, len: usize, lit: *const u8, lit_len: usize) -> bool {
    if len != lit_len {
        return false;
    }
    let mut i: usize = 0;
    while i < len {
        if unsafe { *s.add(i) } != unsafe { *lit.add(i) } {
            return false;
        }
        i += 1;
    }
    true
}

/// Index of the first byte equal to `c` in the span, or usize::MAX.
unsafe fn find_byte(s: *const u8, len: usize, c: u8) -> usize {
    let mut i: usize = 0;
    while i < len {
        if unsafe { *s.add(i) } == c {
            return i;
        }
        i += 1;
    }
    usize::MAX
}

/// Find `needle` (length-aware) in the span; offset or usize::MAX.
unsafe fn find_sub(s: *const u8, len: usize, needle: *const u8, needle_len: usize) -> usize {
    if needle_len == 0 || needle_len > len {
        return usize::MAX;
    }
    let mut i: usize = 0;
    while i + needle_len <= len {
        if unsafe { span_eq(s.add(i), needle_len, needle, needle_len) } {
            return i;
        }
        i += 1;
    }
    usize::MAX
}

/* Ordering as i32: -1 less, 0 equal, 1 greater. */
const ORD_LT: i32 = -1;
const ORD_EQ: i32 = 0;
const ORD_GT: i32 = 1;

/// Pre tags: empty > non-empty (release wins); otherwise byte-wise cmp.
unsafe fn cmp_pre(a: *const u8, a_len: usize, b: *const u8, b_len: usize) -> i32 {
    if a_len == 0 && b_len == 0 {
        return ORD_EQ;
    }
    if a_len == 0 {
        return ORD_GT; // 1.0.0 > 1.0.0-alpha
    }
    if b_len == 0 {
        return ORD_LT;
    }
    let n = if a_len < b_len { a_len } else { b_len };
    let mut i: usize = 0;
    while i < n {
        let ca = unsafe { *a.add(i) };
        let cb = unsafe { *b.add(i) };
        if ca < cb {
            return ORD_LT;
        }
        if ca > cb {
            return ORD_GT;
        }
        i += 1;
    }
    if a_len < b_len {
        return ORD_LT;
    }
    if a_len > b_len {
        return ORD_GT;
    }
    ORD_EQ
}

unsafe fn cmp_semver(a: &SemVer, b: &SemVer) -> i32 {
    if a.major < b.major {
        return ORD_LT;
    }
    if a.major > b.major {
        return ORD_GT;
    }
    if a.minor < b.minor {
        return ORD_LT;
    }
    if a.minor > b.minor {
        return ORD_GT;
    }
    if a.patch < b.patch {
        return ORD_LT;
    }
    if a.patch > b.patch {
        return ORD_GT;
    }
    unsafe { cmp_pre(a.pre.as_ptr(), a.pre_len, b.pre.as_ptr(), b.pre_len) }
}

/// Split a trailing PEP 440 pre-release off the last numeric segment:
/// `1.2.3a2` -> core `1.2.3` + pre `a2`. Writes the pre into `pre_out`
/// (returns its length); `core_off`/`core_len` receive the numeric core.
unsafe fn split_pep440_pre(
    s: *const u8,
    len: usize,
    core_off: *mut usize,
    core_len: *mut usize,
    pre_out: *mut u8,
) -> usize {
    let mut i: usize = 0;
    while i < len {
        let b = unsafe { *s.add(i) };
        let is_digit = b >= b'0' && b <= b'9';
        if is_digit || b == b'.' {
            i += 1;
            continue;
        }
        // Letter starts the pre (a / b / rc / alpha / beta / preview ...).
        let is_alpha = (b >= b'a' && b <= b'z') || (b >= b'A' && b <= b'Z');
        if is_alpha {
            unsafe {
                *core_off = 0;
                *core_len = i;
                let pl = len - i;
                let n = if pl < PRE_CAP { pl } else { PRE_CAP };
                let mut j: usize = 0;
                while j < n {
                    *pre_out.add(j) = *s.add(i + j);
                    j += 1;
                }
                if pl > PRE_CAP {
                    return usize::MAX;
                }
                return pl;
            }
        }
        break;
    }
    unsafe {
        *core_off = 0;
        *core_len = len;
    }
    0
}

/// Parse a semver string (ptr+len) into a SemVer. `ok` receives whether the
/// parse succeeded; on failure the out value is garbage (callers check ok).
unsafe fn parse_semver(raw: *const u8, raw_len: usize, ok: *mut bool, out: *mut SemVer) {
    *ok = false;
    let (toff, tlen) = unsafe { trim(raw, raw_len) };
    let s: *const u8 = unsafe { raw.add(toff) };
    let mut len = tlen;
    if len == 0 {
        return;
    }
    if unsafe { span_eq(s, len, b"*\0".as_ptr(), 1) } {
        return;
    }
    // Drop build metadata (`+...`): the first `+` ends the compared part.
    let plus = unsafe { find_byte(s, len, b'+') };
    if plus != usize::MAX {
        len = plus;
    }
    if len == 0 {
        return;
    }
    let mut core_off: usize = 0;
    let mut core_len: usize = len;
    let mut pre: [u8; PRE_CAP] = [0; PRE_CAP];
    let mut pre_len: usize = 0;
    let dash = unsafe { find_byte(s, len, b'-') };
    if dash != usize::MAX {
        core_len = dash;
        let pl = len - dash - 1;
        if pl > PRE_CAP {
            return;
        }
        let mut j: usize = 0;
        while j < pl {
            pre[j] = unsafe { *s.add(dash + 1 + j) };
            j += 1;
        }
        pre_len = pl;
    } else {
        let pl = unsafe { split_pep440_pre(s, len, &mut core_off, &mut core_len, pre.as_mut_ptr()) };
        if pl == usize::MAX {
            return;
        }
        pre_len = pl;
    }
    if core_len == 0 {
        return;
    }
    // core = up to three dot-separated numeric segments; a fourth is an error.
    let core: *const u8 = unsafe { s.add(core_off) };
    let mut rest_off: usize = 0;
    let mut rest_len: usize = core_len;
    let mut got: usize = 0;
    let mut vals: [u64; 3] = [0; 3];
    let mut num_ok = false;
    while got < 3 {
        let dot = unsafe { find_byte(core.add(rest_off), rest_len, b'.') };
        let seg_len = if dot == usize::MAX { rest_len } else { dot };
        if seg_len == 0 {
            return;
        }
        vals[got] = unsafe { parse_num(core.add(rest_off), seg_len, &mut num_ok) };
        if !num_ok {
            return;
        }
        got += 1;
        if dot == usize::MAX {
            rest_off = core_len;
            rest_len = 0;
            break;
        }
        rest_off += dot + 1;
        rest_len -= dot + 1;
        if rest_len == 0 {
            // trailing dot (`1.2.`) — no fourth segment content, stop clean
            break;
        }
    }
    if rest_len != 0 {
        return; // fourth+ segment present
    }
    if got == 0 {
        return;
    }
    let major = vals[0];
    let minor = if got > 1 { vals[1] } else { 0 };
    let patch = if got > 2 { vals[2] } else { 0 };
    let mut v = SemVer::release(major, minor, patch);
    /* C has no array assignment — copy the pre bytes element-wise. */
    let mut pj: usize = 0;
    while pj < PRE_CAP {
        v.pre[pj] = pre[pj];
        pj += 1;
    }
    v.pre_len = pre_len;
    unsafe {
        *out = v;
    }
    *ok = true;
}

/// Compare two version strings.
/// Returns `-1` / `0` / `1` like strcmp; `-2` if either side fails to parse
/// (unless both are identical raw strings, then `0`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_version_cmp(
    a_ptr: *const u8,
    a_len: u32,
    b_ptr: *const u8,
    b_len: u32,
) -> i32 {
    if a_ptr.is_null() || b_ptr.is_null() {
        return -2;
    }
    let mut a_ok = false;
    let mut a_v = SemVer::release(0, 0, 0);
    let mut b_ok = false;
    let mut b_v = SemVer::release(0, 0, 0);
    unsafe { parse_semver(a_ptr, a_len as usize, &mut a_ok, &mut a_v) };
    unsafe { parse_semver(b_ptr, b_len as usize, &mut b_ok, &mut b_v) };
    if a_ok && b_ok {
        return unsafe { cmp_semver(&a_v, &b_v) };
    }
    /* Unparsable sides: identical raw strings still compare equal (the
     * registry pins by exact string when the shape is unknown). */
    let (ao, al) = unsafe { trim(a_ptr, a_len as usize) };
    let (bo, bl) = unsafe { trim(b_ptr, b_len as usize) };
    if al == bl
        && al > 0
        && unsafe { span_eq((a_ptr as *const u8).add(ao), al, (b_ptr as *const u8).add(bo), bl) }
    {
        return 0;
    }
    -2
}

/// Does `have` satisfy the pin `need`?
///
/// Pins:
/// - `*` or empty -> always
/// - exact string match
/// - semver equal (core + pre)
/// - `>=X.Y.Z` -> have >= X.Y.Z (semver)
/// - `^X.Y.Z` -> same major, have >= need (cargo-like caret, major 0 keeps minor)
///
/// Returns 1 = yes, 0 = no, -1 = parse/error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_version_satisfies(
    have_ptr: *const u8,
    have_len: u32,
    need_ptr: *const u8,
    need_len: u32,
) -> i32 {
    if have_ptr.is_null() || need_ptr.is_null() {
        return -1;
    }
    let (ho, hl) = unsafe { trim(have_ptr, have_len as usize) };
    let (no, nl) = unsafe { trim(need_ptr, need_len as usize) };
    let have: *const u8 = unsafe { have_ptr.add(ho) };
    let need: *const u8 = unsafe { need_ptr.add(no) };
    if nl == 0 {
        return 1;
    }
    if unsafe { span_eq(need, nl, b"*\0".as_ptr(), 1) } {
        return 1;
    }
    if hl == nl && unsafe { span_eq(have, hl, need, nl) } {
        return 1;
    }
    /* `>=X.Y.Z` */
    if nl >= 2 && unsafe { *need.add(0) } == b'>' && unsafe { *need.add(1) } == b'=' {
        let mut a_ok = false;
        let mut a_v = SemVer::release(0, 0, 0);
        let mut b_ok = false;
        let mut b_v = SemVer::release(0, 0, 0);
        let (ro, rl) = unsafe { trim(need.add(2), nl - 2) };
        unsafe { parse_semver(have, hl, &mut a_ok, &mut a_v) };
        unsafe { parse_semver(need.add(2).add(ro), rl, &mut b_ok, &mut b_v) };
        if !a_ok || !b_ok {
            return -1;
        }
        let c = unsafe { cmp_semver(&a_v, &b_v) };
        if c == ORD_LT {
            return 0;
        }
        return 1;
    }
    /* `^X.Y.Z` */
    if nl >= 1 && unsafe { *need.add(0) } == b'^' {
        let mut a_ok = false;
        let mut a_v = SemVer::release(0, 0, 0);
        let mut b_ok = false;
        let mut b_v = SemVer::release(0, 0, 0);
        let (ro, rl) = unsafe { trim(need.add(1), nl - 1) };
        unsafe { parse_semver(have, hl, &mut a_ok, &mut a_v) };
        unsafe { parse_semver(need.add(1).add(ro), rl, &mut b_ok, &mut b_v) };
        if !a_ok || !b_ok {
            return -1;
        }
        let c = unsafe { cmp_semver(&a_v, &b_v) };
        if c == ORD_LT {
            return 0;
        }
        if b_v.major > 0 {
            if a_v.major == b_v.major {
                return 1;
            }
            return 0;
        }
        if b_v.minor > 0 {
            if a_v.major == 0 && a_v.minor == b_v.minor {
                return 1;
            }
            return 0;
        }
        if a_v.major == 0 && a_v.minor == 0 && a_v.patch == b_v.patch {
            return 1;
        }
        return 0;
    }
    /* exact semver equal */
    let mut a_ok = false;
    let mut a_v = SemVer::release(0, 0, 0);
    let mut b_ok = false;
    let mut b_v = SemVer::release(0, 0, 0);
    unsafe { parse_semver(have, hl, &mut a_ok, &mut a_v) };
    unsafe { parse_semver(need, nl, &mut b_ok, &mut b_v) };
    if !a_ok || !b_ok {
        return 0;
    }
    if unsafe { cmp_semver(&a_v, &b_v) } == ORD_EQ {
        return 1;
    }
    0
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.version",
    pm_util_version_cmp,
    "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.version",
    pm_util_version_satisfies,
    "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t)"
);

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;

#[cfg(test)]
#[path = "__bench__.rs"]
mod __bench__;
