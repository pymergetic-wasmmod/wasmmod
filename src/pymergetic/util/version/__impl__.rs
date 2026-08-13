//! pymergetic.util.version — semver parse / compare / satisfy for pack deps
//! and kernel module pins. One lib; product versions live on registry entries.

#![allow(clippy::missing_safety_doc)]

use alloc::string::String;

#[derive(Clone, Debug, PartialEq, Eq)]
struct SemVer {
    major: u64,
    minor: u64,
    patch: u64,
    /// Empty = release. Pre-release sorts *before* the same numbers without one.
    pre: String,
}

fn parse_num(s: &str) -> Option<u64> {
    if s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    s.parse().ok()
}

/// Split a trailing PEP 440 / semver pre-release off the last numeric segment.
/// `1.2.3a2` → (`1.2.3`, `a2`); `1.2.3-rc.1` handled via hyphen path below.
fn split_pep440_pre(core: &str) -> (&str, &str) {
    let bytes = core.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let b = bytes[i];
        if b.is_ascii_digit() || b == b'.' {
            i += 1;
            continue;
        }
        // Letter starts pre (a / b / rc / alpha / beta / preview …).
        if b.is_ascii_alphabetic() {
            return (&core[..i], &core[i..]);
        }
        break;
    }
    (core, "")
}

fn parse_semver(raw: &str) -> Option<SemVer> {
    let s = raw.trim();
    if s.is_empty() || s == "*" {
        return None;
    }
    // Drop build metadata (`+…`) first.
    let s = s.split('+').next().unwrap_or(s);
    let (core, pre) = match s.split_once('-') {
        Some((c, p)) => (c, p),
        None => split_pep440_pre(s),
    };
    let mut parts = core.split('.');
    let major = parse_num(parts.next()?)?;
    let minor = parse_num(parts.next().unwrap_or("0")).unwrap_or(0);
    let patch = parse_num(parts.next().unwrap_or("0"))?;
    if parts.next().is_some() {
        return None;
    }
    Some(SemVer {
        major,
        minor,
        patch,
        pre: String::from(pre),
    })
}

fn cmp_pre(a: &str, b: &str) -> core::cmp::Ordering {
    use core::cmp::Ordering;
    match (a.is_empty(), b.is_empty()) {
        (true, true) => Ordering::Equal,
        (true, false) => Ordering::Greater, // 1.0.0 > 1.0.0-alpha
        (false, true) => Ordering::Less,
        (false, false) => a.cmp(b),
    }
}

fn cmp_semver(a: &SemVer, b: &SemVer) -> core::cmp::Ordering {
    a.major
        .cmp(&b.major)
        .then(a.minor.cmp(&b.minor))
        .then(a.patch.cmp(&b.patch))
        .then(cmp_pre(&a.pre, &b.pre))
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
    let a = match str_from_raw(a_ptr, a_len) {
        Some(s) => s.trim(),
        None => return -2,
    };
    let b = match str_from_raw(b_ptr, b_len) {
        Some(s) => s.trim(),
        None => return -2,
    };
    if a == b {
        return 0;
    }
    match (parse_semver(a), parse_semver(b)) {
        (Some(va), Some(vb)) => match cmp_semver(&va, &vb) {
            core::cmp::Ordering::Less => -1,
            core::cmp::Ordering::Equal => 0,
            core::cmp::Ordering::Greater => 1,
        },
        _ => -2,
    }
}

/// Does `have` satisfy the pin `need`?
///
/// Pins:
/// - `*` or empty → always
/// - exact string match
/// - semver equal (core + pre)
/// - `>=X.Y.Z` → have >= X.Y.Z (semver)
/// - `^X.Y.Z` → same major, have >= need (cargo-like caret, major 0 keeps minor)
///
/// Returns 1 = yes, 0 = no, -1 = parse/error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_version_satisfies(
    have_ptr: *const u8,
    have_len: u32,
    need_ptr: *const u8,
    need_len: u32,
) -> i32 {
    let have = match str_from_raw(have_ptr, have_len) {
        Some(s) => s.trim(),
        None => return -1,
    };
    let need = match str_from_raw(need_ptr, need_len) {
        Some(s) => s.trim(),
        None => return -1,
    };
    if need.is_empty() || need == "*" {
        return 1;
    }
    if have == need {
        return 1;
    }
    if let Some(req) = need.strip_prefix(">=") {
        let req = req.trim();
        return match (parse_semver(have), parse_semver(req)) {
            (Some(h), Some(r)) => (cmp_semver(&h, &r) != core::cmp::Ordering::Less) as i32,
            _ => -1,
        };
    }
    if let Some(req) = need.strip_prefix('^') {
        let req = req.trim();
        return match (parse_semver(have), parse_semver(req)) {
            (Some(h), Some(r)) => {
                if cmp_semver(&h, &r) == core::cmp::Ordering::Less {
                    return 0;
                }
                if r.major > 0 {
                    (h.major == r.major) as i32
                } else if r.minor > 0 {
                    (h.major == 0 && h.minor == r.minor) as i32
                } else {
                    (h.major == 0 && h.minor == 0 && h.patch == r.patch) as i32
                }
            }
            _ => -1,
        };
    }
    match (parse_semver(have), parse_semver(need)) {
        (Some(h), Some(n)) => (cmp_semver(&h, &n) == core::cmp::Ordering::Equal) as i32,
        _ => 0,
    }
}

fn str_from_raw<'a>(ptr: *const u8, len: u32) -> Option<&'a str> {
    if ptr.is_null() {
        return None;
    }
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
    core::str::from_utf8(bytes).ok()
}

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
