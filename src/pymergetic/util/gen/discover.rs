//! Host-only discovery when the live registry is empty for a card:
//! - `impl = "py"` → scan `__init__.py` access faces from type hints (no python3)
//! - else if `__impl__.c` has `PM_MOD_EXPORT_C` → scan call sites (guest / unlinked C)
//!
//! Registers into the one registry so [`super::gen_fqn_to_sink`] stays the emit path.

use std::path::Path;

use crate::wasmmod::registry::{
    pm_wasmmod_registry_container_kind_t, pm_wasmmod_registry_ensure,
};

use super::{introspect_exports, register_fn};

/// How facegen obtained exports for a card (registry introspect path).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FaceSource {
    /// Already in the live registry (`PM_MOD_EXPORT_*` ctors / loader publish).
    Live,
    /// Filled from `__init__.py` type hints (`impl = "py"` access faces).
    Py,
    /// Filled from guest `PM_MOD_EXPORT_C` call sites (unlinked chromosome).
    Guest,
    /// Nothing to emit.
    Empty,
}

/// Poison ptr for facegen-only registration (no live µPy / guest link).
unsafe extern "C" fn pm_gen_facegen_stub() {}

fn stub_ptr() -> *mut core::ffi::c_void {
    pm_gen_facegen_stub as *mut core::ffi::c_void
}

/// `pymergetic.util.pysample` + `hello` → `pm_util_pysample_hello`
pub fn c_abi_name(fqn: &str, export: &str) -> String {
    let rest = fqn.strip_prefix("pymergetic.").unwrap_or(fqn);
    format!("pm_{}_{}", rest.replace('.', "_"), export)
}

fn load_impl(card_dir: &Path) -> Option<String> {
    let text = std::fs::read_to_string(card_dir.join("__pmm__.toml")).ok()?;
    let value: toml::Value = text.parse().ok()?;
    value.get("impl")?.as_str().map(|s| s.to_string())
}

/// Ensure `fqn` has registry exports for facegen.
pub fn ensure_card_exports(card_dir: &Path, fqn: &str) -> FaceSource {
    if !introspect_exports(fqn).is_empty() {
        return FaceSource::Live;
    }
    let impl_lang = load_impl(card_dir).unwrap_or_default();
    if impl_lang == "py" {
        if ensure_py_exports(card_dir, fqn) {
            return FaceSource::Py;
        }
        return FaceSource::Empty;
    }
    if impl_lang == "rs" {
        if ensure_rs_export_scan(card_dir, fqn) {
            return FaceSource::Guest;
        }
        return FaceSource::Empty;
    }
    if ensure_c_export_scan(card_dir, fqn) {
        FaceSource::Guest
    } else {
        FaceSource::Empty
    }
}

fn ensure_py_exports(card_dir: &Path, fqn: &str) -> bool {
    let init = card_dir.join("__init__.py");
    let Some(exports) = py_exports_from_init(&init) else {
        return false;
    };
    let mut any = false;
    for (export, sig) in exports {
        let name = c_abi_name(fqn, &export);
        if unsafe { register_fn(fqn, &name, stub_ptr(), &sig) } {
            any = true;
        }
    }
    any
}

/// Parse `__init__.py` type hints → `(export_name, c_sig)`.
pub fn py_exports_from_init(init_py: &Path) -> Option<Vec<(String, String)>> {
    let text = std::fs::read_to_string(init_py).ok()?;
    let out = py_exports_from_init_src(&text);
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
}

/// Scan top-level `def name(...) -> int:` faces (constrained hint set).
/// Same spirit as [`scan_pm_mod_export_c`]: string scan, no foreign runtime.
pub fn py_exports_from_init_src(text: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut i = 0;
    let bytes = text.as_bytes();
    while i < bytes.len() {
        // Start of line (or file).
        if i > 0 && bytes[i - 1] != b'\n' {
            i += 1;
            continue;
        }
        // Skip indent — only module-level defs (no leading whitespace).
        if bytes[i] == b' ' || bytes[i] == b'\t' {
            i += 1;
            continue;
        }
        if !text[i..].starts_with("def ") {
            i += 1;
            continue;
        }
        let after_def = i + 4;
        let Some((name, rest)) = take_ident(&text[after_def..]) else {
            i = after_def;
            continue;
        };
        if name.starts_with('_') {
            i = after_def;
            continue;
        }
        let rest = rest.trim_start();
        if !rest.starts_with('(') {
            i = after_def;
            continue;
        }
        let Some((params, after_params)) = take_balanced(&rest[1..], '(', ')') else {
            i = after_def;
            continue;
        };
        let after = after_params.trim_start();
        if !after.starts_with("->") {
            i = after_def;
            continue;
        }
        let after_arrow = after[2..].trim_start();
        let Some(ret) = take_hint_token(after_arrow) else {
            i = after_def;
            continue;
        };
        let ret = normalize_hint(ret);
        if let Some(sig) = hints_to_sig(ret, &parse_param_hints(params)) {
            out.push((name.to_string(), sig));
        }
        i = after_def;
    }
    out
}

fn take_ident(s: &str) -> Option<(&str, &str)> {
    let mut end = 0;
    for (off, ch) in s.char_indices() {
        let ok = if off == 0 {
            ch == '_' || ch.is_ascii_alphabetic()
        } else {
            ch == '_' || ch.is_ascii_alphanumeric()
        };
        if !ok {
            break;
        }
        end = off + ch.len_utf8();
    }
    if end == 0 {
        return None;
    }
    Some((&s[..end], &s[end..]))
}

/// Return `(inner, rest_after_closing)` where `inner` is between open…close.
fn take_balanced<'a>(s: &'a str, open: char, close: char) -> Option<(&'a str, &'a str)> {
    let mut depth = 1i32;
    for (off, ch) in s.char_indices() {
        if ch == open {
            depth += 1;
        } else if ch == close {
            depth -= 1;
            if depth == 0 {
                return Some((&s[..off], &s[off + close.len_utf8()..]));
            }
        }
    }
    None
}

fn parse_param_hints(params: &str) -> Option<Vec<&str>> {
    let params = params.trim();
    if params.is_empty() {
        return Some(Vec::new());
    }
    let mut hints = Vec::new();
    for part in split_top_level_commas(params) {
        let part = part.trim();
        if part.is_empty() || part == "self" || part == "cls" {
            continue;
        }
        // name: hint  |  name: "hint"
        let Some(colon) = part.find(':') else {
            return None;
        };
        let hint = part[colon + 1..].trim();
        let hint = strip_str_quotes(hint)?;
        if !matches!(
            hint,
            "bytes" | "int" | "mem" | "obj" | "i64" | "int64" | "f32" | "float" | "f64"
        ) {
            return None;
        }
        hints.push(hint);
    }
    Some(hints)
}

fn strip_str_quotes(s: &str) -> Option<&str> {
    let s = s.trim();
    if (s.starts_with('"') && s.ends_with('"') && s.len() >= 2)
        || (s.starts_with('\'') && s.ends_with('\'') && s.len() >= 2)
    {
        return Some(&s[1..s.len() - 1]);
    }
    // Bare Name — reject if trailing junk (default values, etc.)
    let (id, rest) = take_ident(s)?;
    if !rest.trim().is_empty() {
        return None;
    }
    Some(id)
}

fn take_hint_token(s: &str) -> Option<&str> {
    let s = s.trim_start();
    if let Some(q) = s.chars().next() {
        if q == '"' || q == '\'' {
            let end = s[1..].find(q)? + 1;
            return Some(&s[1..end]);
        }
    }
    let (id, _) = take_ident(s)?;
    Some(id)
}

fn normalize_hint(h: &str) -> &str {
    match h {
        "int64" => "i64",
        "float" => "f32",
        "float64" => "f64",
        other => other,
    }
}

fn split_top_level_commas(s: &str) -> Vec<&str> {
    let mut out = Vec::new();
    let mut start = 0;
    let mut depth = 0i32;
    let mut in_str: Option<char> = None;
    for (off, ch) in s.char_indices() {
        if let Some(q) = in_str {
            if ch == q {
                in_str = None;
            }
            continue;
        }
        match ch {
            '"' | '\'' => in_str = Some(ch),
            '(' | '[' | '{' => depth += 1,
            ')' | ']' | '}' => depth -= 1,
            ',' if depth == 0 => {
                out.push(&s[start..off]);
                start = off + 1;
            }
            _ => {}
        }
    }
    out.push(&s[start..]);
    out
}

fn hints_to_sig(ret: &str, hints: &Option<Vec<&str>>) -> Option<String> {
    let hints = hints.as_ref()?;
    let args: Vec<&str> = hints.iter().copied().map(normalize_hint).collect();
    let sig = match (ret, args.as_slice()) {
        ("int", []) => "int32_t(void)",
        ("int", ["bytes"]) => "int32_t(const uint8_t *, uint32_t)",
        ("int", ["int"]) => "int32_t(int32_t)",
        ("int", ["int", "int"]) => "int32_t(int32_t, int32_t)",
        ("int", ["int", "int", "int"]) => "int32_t(int32_t, int32_t, int32_t)",
        ("int", ["mem"]) => "int32_t(pm_wasmmod_mem_cookie_t)",
        ("int", ["obj"]) => "int32_t(pm_wasmmod_obj_handle_t)",
        ("i64", ["i64"]) => "int64_t(int64_t)",
        ("f32", ["f32"]) => "float(float)",
        ("f64", ["f64"]) => "double(double)",
        _ => return None,
    };
    Some(sig.to_string())
}

fn ensure_rs_export_scan(card_dir: &Path, fqn: &str) -> bool {
    let impl_rs = card_dir.join("__impl__.rs");
    if !impl_rs.is_file() {
        return false;
    }
    let Ok(text) = std::fs::read_to_string(&impl_rs) else {
        return false;
    };
    let exports = scan_pm_mod_export_rs(&text);
    if exports.is_empty() {
        return false;
    }
    unsafe {
        let _ = pm_wasmmod_registry_ensure(
            fqn.as_ptr(),
            fqn.len() as u32,
            pm_wasmmod_registry_container_kind_t::Resident,
        );
    }
    let mut any = false;
    for (export_name, sig) in exports {
        if unsafe { register_fn(fqn, &export_name, stub_ptr(), &sig) } {
            any = true;
        }
    }
    any
}

/// Scan `PM_MOD_EXPORT_RS!("fqn", ident, "sig")` → `(ident, sig)`.
pub fn scan_pm_mod_export_rs(text: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut search_from = 0;
    const MARK: &str = "PM_MOD_EXPORT_RS!";
    while let Some(rel) = text[search_from..].find(MARK) {
        let at = search_from + rel;
        let after = at + MARK.len();
        let Some(open_rel) = text[after..].find('(') else {
            search_from = after;
            continue;
        };
        let start = after + open_rel + 1;
        let mut depth = 1i32;
        let mut end = None;
        for (off, ch) in text[start..].char_indices() {
            match ch {
                '(' => depth += 1,
                ')' => {
                    depth -= 1;
                    if depth == 0 {
                        end = Some(start + off);
                        break;
                    }
                }
                _ => {}
            }
        }
        let Some(end) = end else {
            search_from = after;
            continue;
        };
        if let Some(pair) = parse_pm_mod_export_rs_args(&text[start..end]) {
            out.push(pair);
        }
        search_from = end + 1;
    }
    out
}

fn strip_rs_str(s: &str) -> String {
    let t = s.trim();
    t.trim_matches('"').to_string()
}

fn parse_pm_mod_export_rs_args(inner: &str) -> Option<(String, String)> {
    let mut parts: Vec<String> = Vec::new();
    let mut cur = String::new();
    let mut depth = 0i32;
    let mut in_str = false;
    for ch in inner.chars() {
        match ch {
            '"' => {
                in_str = !in_str;
                cur.push(ch);
            }
            '(' if !in_str => {
                depth += 1;
                cur.push(ch);
            }
            ')' if !in_str => {
                depth -= 1;
                cur.push(ch);
            }
            ',' if depth == 0 && !in_str => {
                parts.push(cur.trim().to_string());
                cur.clear();
            }
            _ => cur.push(ch),
        }
    }
    if !cur.trim().is_empty() {
        parts.push(cur.trim().to_string());
    }
    if parts.len() != 3 {
        return None;
    }
    let export_name = parts[1].trim().to_string();
    let sig = strip_rs_str(&parts[2]);
    if export_name.is_empty() || sig.is_empty() {
        return None;
    }
    Some((export_name, sig))
}

fn ensure_c_export_scan(card_dir: &Path, fqn: &str) -> bool {
    let impl_c = card_dir.join("__impl__.c");
    if !impl_c.is_file() {
        return false;
    }
    let Ok(text) = std::fs::read_to_string(&impl_c) else {
        return false;
    };
    let exports = scan_pm_mod_export_c(&text);
    if exports.is_empty() {
        return false;
    }
    // Guest / chromosome card — mark Wasm *before* mod_export stubs land so
    // introspect sees container=Wasm (ensure keeps the first kind).
    unsafe {
        let _ = pm_wasmmod_registry_ensure(
            fqn.as_ptr(),
            fqn.len() as u32,
            pm_wasmmod_registry_container_kind_t::Wasm,
        );
    }
    let mut any = false;
    for (export_name, sig) in exports {
        // Register under card fqn (path == module), not the short macro mod token.
        if unsafe { register_fn(fqn, &export_name, stub_ptr(), &sig) } {
            any = true;
        }
    }
    any
}

/// Scan `PM_MOD_EXPORT_C(mod, export_name, impl_fn, sig)` → `(export_name, sig)`.
pub fn scan_pm_mod_export_c(text: &str) -> Vec<(String, String)> {
    let mut out = Vec::new();
    let mut search_from = 0;
    while let Some(rel) = text[search_from..].find("PM_MOD_EXPORT_C") {
        let at = search_from + rel;
        let after_name = at + "PM_MOD_EXPORT_C".len();
        let Some(open_rel) = text[after_name..].find('(') else {
            search_from = after_name;
            continue;
        };
        let start = after_name + open_rel + 1;
        let mut depth = 1i32;
        let mut end = None;
        for (off, ch) in text[start..].char_indices() {
            match ch {
                '(' => depth += 1,
                ')' => {
                    depth -= 1;
                    if depth == 0 {
                        end = Some(start + off);
                        break;
                    }
                }
                _ => {}
            }
        }
        let Some(end) = end else {
            search_from = after_name;
            continue;
        };
        let inner = &text[start..end];
        if let Some((export_name, sig)) = parse_pm_mod_export_c_args(inner) {
            out.push((export_name, sig));
        }
        search_from = end + 1;
    }
    out
}

fn parse_pm_mod_export_c_args(inner: &str) -> Option<(String, String)> {
    // mod, export_name, impl_fn, sig — sig may contain commas inside ().
    let mut parts: Vec<String> = Vec::new();
    let mut cur = String::new();
    let mut depth = 0i32;
    for ch in inner.chars() {
        match ch {
            '(' => {
                depth += 1;
                cur.push(ch);
            }
            ')' => {
                depth -= 1;
                cur.push(ch);
            }
            ',' if depth == 0 => {
                parts.push(cur.trim().to_string());
                cur.clear();
            }
            _ => cur.push(ch),
        }
    }
    if !cur.trim().is_empty() {
        parts.push(cur.trim().to_string());
    }
    if parts.len() != 4 {
        return None;
    }
    let export_name = parts[1].clone();
    let sig = parts[3].clone();
    if export_name.is_empty() || sig.is_empty() {
        return None;
    }
    Some((export_name, sig))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scans_hello_style_exports() {
        let text = r#"
int hello(void) { return 42; }
PM_MOD_EXPORT_C(hello, hello, hello, int(void));
int add(int a, int b) { return a + b; }
PM_MOD_EXPORT_C(hello, add, add, int(int, int));
"#;
        let ex = scan_pm_mod_export_c(text);
        assert_eq!(ex.len(), 2);
        assert_eq!(ex[0], ("hello".into(), "int(void)".into()));
        assert_eq!(ex[1], ("add".into(), "int(int, int)".into()));
    }

    #[test]
    fn scans_rs_export_macro() {
        let text = r#"
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.wasmmod.loader",
    pm_wasmmod_loader_init,
    "int32_t(pm_util_mem_arena_t *)"
);
"#;
        let ex = scan_pm_mod_export_rs(text);
        assert_eq!(ex.len(), 1);
        assert_eq!(
            ex[0],
            (
                "pm_wasmmod_loader_init".into(),
                "int32_t(pm_util_mem_arena_t *)".into()
            )
        );
    }

    #[test]
    fn c_abi_name_strips_pymergetic() {
        assert_eq!(
            c_abi_name("pymergetic.util.pysample", "hello"),
            "pm_util_pysample_hello"
        );
    }

    #[test]
    fn scans_pysample_style_init() {
        let text = r#"
# comment
def hello() -> int:
    return 42

def echo_len(data: bytes) -> int:
    return len(data)

def echo_mem(data: "mem") -> int:
    return len(data)

def is_none(o: 'obj') -> int:
    return 1 if o is None else 0

def add(a: int, b: int) -> int:
    return a + b

def add3(a: int, b: int, c: int) -> int:
    return a + b + c

def wid(x: i64) -> i64:
    return x

def fl(x: f32) -> f32:
    return x

def _skip() -> int:
    return 0

def bad(x: str) -> int:
    return 0
"#;
        let ex = py_exports_from_init_src(text);
        assert_eq!(
            ex,
            vec![
                ("hello".into(), "int32_t(void)".into()),
                (
                    "echo_len".into(),
                    "int32_t(const uint8_t *, uint32_t)".into()
                ),
                ("echo_mem".into(), "int32_t(pm_wasmmod_mem_cookie_t)".into()),
                ("is_none".into(), "int32_t(pm_wasmmod_obj_handle_t)".into()),
                ("add".into(), "int32_t(int32_t, int32_t)".into()),
                ("add3".into(), "int32_t(int32_t, int32_t, int32_t)".into()),
                ("wid".into(), "int64_t(int64_t)".into()),
                ("fl".into(), "float(float)".into()),
            ]
        );
    }

    /// Scans a real card of this crate, not a fixture — the point is that the
    /// scanner survives a full-size `__impl__.c`. It must be one of ours: a
    /// wasmmod test that reads a downstream tree fails wherever that tree
    /// isn't checked out.
    #[test]
    fn scans_own_cdn_impl() {
        let p = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("src/pymergetic/wasmmod/net/cdn/__impl__.c");
        let text = std::fs::read_to_string(&p).expect("wasmmod net.cdn __impl__.c");
        let ex = scan_pm_mod_export_c(&text);
        assert!(
            ex.iter().any(|(n, _)| n.starts_with("pm_wasmmod_net_cdn_")),
            "wasmmod.net.cdn PM_MOD_EXPORT_C missing: {ex:?}"
        );
        assert!(ex.len() > 1, "expected a full card's exports: {ex:?}");
    }
}
