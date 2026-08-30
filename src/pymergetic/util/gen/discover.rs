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

/*----------------------------------------------------------------------
 * PM_TYPE_DEFINE_* source scan — staging types for cards whose muscle
 * is not linked into the gen binary (guest packs, unlinked C/RS).
 * The linked binary remains the runtime truth; staging exists so
 * facegen can emit view faces for every card in one pass.
 *--------------------------------------------------------------------*/

unsafe extern "C" {
    fn pm_types_registry_stage(
        fqn: *const u8,
        kind: u16,
        instance_size: u16,
        parent_fqn: *const u8,
        field_count: u16,
    ) -> i32;
    fn pm_types_registry_stage_field(
        fqn: *const u8,
        field_index: u16,
        name: *const u8,
        offset: u32,
        type_fqn: *const u8,
    ) -> i32;
    fn pm_types_registry_stage_commit(fqn: *const u8) -> i32;
}

/// One scanned type: fqn + fields (source order; commit sorts).
#[derive(Clone, Debug, Default)]
pub struct ScannedType {
    pub fqn: String,
    pub kind: u16,
    pub instance_size: u16,
    pub parent_sym_or_fqn: String,
    pub fields: Vec<(String, u32, String)>, // (name, offset, type_fqn)
}

/// Take a balanced-parens span after `open` (which sits on `(` itself).
fn paren_span(text: &str, open: usize) -> Option<&str> {
    let bytes = text.as_bytes();
    let mut depth = 0i32;
    for (off, &b) in bytes[open..].iter().enumerate() {
        match b {
            b'(' => depth += 1,
            b')' => {
                depth -= 1;
                if depth == 0 {
                    return Some(&text[open + 1..open + off]);
                }
            }
            _ => {}
        }
    }
    None
}

/// Strip surrounding quotes from a C string literal (best-effort).
fn unquote(s: &str) -> String {
    let t = s.trim();
    if t.len() >= 2 && t.starts_with('"') && t.ends_with('"') {
        t[1..t.len() - 1].to_string()
    } else {
        t.to_string()
    }
}

/// `PM_TYPE_DESC_STRUCT` → 0, `PM_TYPE_DESC_LIST` → 1, … (else STRUCT).
fn desc_kind_val(sym: &str) -> u16 {
    match sym.trim() {
        "PM_TYPE_DESC_LIST" => 1,
        "PM_TYPE_DESC_DICT" => 2,
        "PM_TYPE_DESC_ENUM" => 3,
        "PM_TYPE_DESC_UNION" => 4,
        "PM_TYPE_DESC_PRIMITIVE" => 5,
        _ => 0, // STRUCT (0) — default
    }
}

/// Map a C field-type token (as written in the field array) to a
/// types builtin fqn. `&PM_TYPE_F64_DESC` → "pymergetic.types.f64".
fn field_type_fqn(token: &str) -> String {
    let t = token.trim();
    let name = t
        .strip_prefix("&PM_TYPE_")
        .or_else(|| t.strip_prefix("PM_TYPE_"))
        .unwrap_or(t)
        .trim_end_matches("_DESC");
    let fqn = match name {
        "NIL" => "pymergetic.types.nil",
        "I32" => "pymergetic.types.i32",
        "I64" => "pymergetic.types.i64",
        "U32" => "pymergetic.types.u32",
        "U64" => "pymergetic.types.u64",
        "F32" => "pymergetic.types.f32",
        "F64" => "pymergetic.types.f64",
        "BOOL" => "pymergetic.types.bool",
        "STR" => "pymergetic.types.str",
        "BYTES" => "pymergetic.types.bytes",
        _ => "", // struct ref / undeclared — cell view
    };
    fqn.to_string()
}

/// Scan `__impl__.c` text for PM_TYPE_DEFINE_C + its field arrays.
/// Field arrays are matched by the symbol passed to the macro.
pub fn scan_pm_type_define_c(text: &str) -> Vec<ScannedType> {
    let mut out = Vec::new();
    let mut search_from = 0;
    while let Some(rel) = text[search_from..].find("PM_TYPE_DEFINE_C(") {
        let at = search_from + rel;
        let open = at + "PM_TYPE_DEFINE_C".len();
        let Some(inner) = paren_span(text, open) else {
            search_from = open;
            continue;
        };
        let parts = split_top_level_commas(inner);
        // desc_sym, "fqn", kind, inst_size, parent, field_arr, field_n
        if parts.len() >= 7 {
            let fqn = unquote(parts[1]);
            let field_arr = parts[5].trim().to_string();
            if !fqn.is_empty() && !field_arr.is_empty() {
                let fields = scan_field_array_c(text, &field_arr);
                out.push(ScannedType {
                    fqn,
                    kind: desc_kind_val(parts[2]),
                    instance_size: parts[3].trim().parse().unwrap_or(0),
                    parent_sym_or_fqn: parts[4].trim().to_string(),
                    fields,
                });
            }
        }
        search_from = open + inner.len() + 1;
    }
    out
}

/// Scan a `static const pm_type_field_t s_x[] = { { h, f, off, &T, "n" }, … }`
/// array literal by its symbol. Rows: hash, flags, offset, type, name.
fn scan_field_array_c(text: &str, sym: &str) -> Vec<(String, u32, String)> {
    let mut fields = Vec::new();
    // find `sym[] = {` (allow whitespace)
    let pat_suffix: &str = "[]";
    let _ = pat_suffix;
    let mut probe = 0usize;
    let mut decl_at = None;
    while let Some(rel) = text[probe..].find(sym) {
        let at = probe + rel;
        let after = &text[at + sym.len()..];
        let trimmed = after.trim_start();
        if trimmed.starts_with("[]")
            && after[after.len() - trimmed.len()..]
                .starts_with("[]")
        {
            decl_at = Some(at);
            break;
        }
        probe = at + sym.len();
    }
    let Some(decl) = decl_at else {
        return fields;
    };
    let after_sym = &text[decl + sym.len()..];
    let brace_open = match after_sym.find('{') {
        Some(i) => decl + sym.len() + i,
        None => return fields,
    };
    let Some(body) = brace_span(text, brace_open) else {
        return fields;
    };
    for row in split_top_level_commas(body) {
        let row = row.trim();
        let Some(rest) = row.strip_prefix('{') else {
            continue;
        };
        let Some(inner) = rest.strip_suffix('}') else {
            continue;
        };
        let cells = split_top_level_commas(inner);
        // name_hash, _flags, offset, type, name (trailing comma tolerated)
        if cells.len() >= 5 {
            let name = unquote(cells[4]);
            let offset: u32 = cells[2].trim().parse().unwrap_or(0);
            let type_fqn = field_type_fqn(cells[3]);
            if !name.is_empty() {
                fields.push((name, offset, type_fqn));
            }
        }
    }
    fields
}

/// Balanced-brace span after `open` (which sits on `{` itself).
fn brace_span(text: &str, open: usize) -> Option<&str> {
    let bytes = text.as_bytes();
    let mut depth = 0i32;
    for (off, &b) in bytes[open..].iter().enumerate() {
        match b {
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(&text[open + 1..open + off]);
                }
            }
            _ => {}
        }
    }
    None
}

/// Stage every scanned type of a card dir into the live type registry.
/// Returns how many were staged (0 when the muscle defines none).
pub fn ensure_card_types(card_dir: &Path) -> usize {
    let mut scanned = Vec::new();
    for name in ["__impl__.c", "__impl__.rs"] {
        let p = card_dir.join(name);
        let Ok(text) = std::fs::read_to_string(&p) else {
            continue;
        };
        if name == "__impl__.c" {
            scanned.extend(scan_pm_type_define_c(&text));
        } else {
            scanned.extend(scan_pm_type_define_rs(&text));
        }
    }
    let n = scanned.len();
    if n == 0 {
        return 0;
    }
    // Stage with borrowed NUL-terminated statics per call: build CStrings
    // first so every pointer outlives the staging block below.
    let c_fqns: Vec<std::ffi::CString> = scanned
        .iter()
        .map(|t| std::ffi::CString::new(t.fqn.as_str()).unwrap_or_default())
        .collect();
    unsafe {
        for (i, t) in scanned.iter().enumerate() {
            let parent = if t.parent_sym_or_fqn.starts_with('"') {
                unquote(&t.parent_sym_or_fqn)
            } else if t.parent_sym_or_fqn == "NULL"
                || t.parent_sym_or_fqn.is_empty()
                || t.parent_sym_or_fqn == "None"
            {
                String::new()
            } else {
                // C parent is a symbol (&s_entity_desc) — resolve via the
                // scanned set by convention: parent types register first
                // (source order), so look it up among already-staged fqns.
                resolve_parent_fqn(&scanned[..i], &t.parent_sym_or_fqn, &t.fqn)
            };
            let parent_c = if parent.is_empty() {
                None
            } else {
                std::ffi::CString::new(parent).ok()
            };
            let rc = pm_types_registry_stage(
                c_fqns[i].as_ptr() as *const u8,
                t.kind,
                t.instance_size,
                parent_c.as_ref().map_or(core::ptr::null(), |c| c.as_ptr() as *const u8),
                t.fields.len() as u16,
            );
            let _ = rc;
            let names: Vec<std::ffi::CString> = t
                .fields
                .iter()
                .map(|(n, _, _)| std::ffi::CString::new(n.as_str()).unwrap_or_default())
                .collect();
            let types: Vec<std::ffi::CString> = t
                .fields
                .iter()
                .map(|(_, _, ty)| std::ffi::CString::new(ty.as_str()).unwrap_or_default())
                .collect();
            for (j, (name, offset, type_fqn)) in t.fields.iter().enumerate() {
                if type_fqn.is_empty() {
                    // undeclared cell — pass NULL so the descriptor keeps
                    // the 16-byte value-cell contract
                    let _ = pm_types_registry_stage_field(
                        c_fqns[i].as_ptr() as *const u8,
                        j as u16,
                        names[j].as_ptr() as *const u8,
                        *offset,
                        core::ptr::null(),
                    );
                    let _ = (name, type_fqn);
                } else {
                    let _ = pm_types_registry_stage_field(
                        c_fqns[i].as_ptr() as *const u8,
                        j as u16,
                        names[j].as_ptr() as *const u8,
                        *offset,
                        types[j].as_ptr() as *const u8,
                    );
                }
            }
            let _ = pm_types_registry_stage_commit(c_fqns[i].as_ptr() as *const u8);
        }
    }
    n
}

/// C `PM_TYPE_DEFINE_C` parents are symbols (`&s_entity_desc`). Match by
/// trailing `_desc`/`_fields` convention against previously staged fqns,
/// then fall back to a same-card prefix match on the desc symbol's card.
fn resolve_parent_fqn(
    staged: &[ScannedType],
    parent_sym: &str,
    _own_fqn: &str,
) -> String {
    let want = parent_sym
        .trim_start_matches('&')
        .trim_end_matches("_desc")
        .trim_end_matches("_fields")
        .to_ascii_lowercase();
    for t in staged {
        let leaf = t.fqn.rsplit('.').next().unwrap_or("").to_ascii_lowercase();
        if leaf == want {
            return t.fqn.clone();
        }
    }
    String::new()
}

/// Scan `__impl__.rs` for `PM_TYPE_DEFINE_RS!` invocations.
pub fn scan_pm_type_define_rs(text: &str) -> Vec<ScannedType> {
    let mut out = Vec::new();
    let mut search_from = 0;
    while let Some(rel) = text[search_from..].find("PM_TYPE_DEFINE_RS!(") {
        let at = search_from + rel;
        let open = at + "PM_TYPE_DEFINE_RS!".len();
        let Some(inner) = paren_span(text, open) else {
            search_from = open;
            continue;
        };
        // (fqn, kind, inst_size, parent_expr, fields_array_expr)
        let parts = split_top_level_commas(inner);
        if parts.len() >= 5 {
            let fqn = unquote(parts[0]);
            let fields = scan_fields_rs(&parts[4]);
            out.push(ScannedType {
                fqn,
                kind: desc_kind_val(parts[1]),
                instance_size: parts[2].trim().parse().unwrap_or(0),
                parent_sym_or_fqn: parts[3].trim().to_string(),
                fields,
            });
        }
        search_from = open + inner.len() + 1;
    }
    out
}

/// Parse the Rust fields array expr: `pm_type_fields! { x: (i32, 0), … }`
/// or a plain slice literal `&[("x", "i32", 0)]`.
fn scan_fields_rs(expr: &str) -> Vec<(String, u32, String)> {
    let mut fields = Vec::new();
    let expr = expr.trim();
    let Some(brace_open) = expr.find('{') else {
        return fields;
    };
    let Some(body) = brace_span(expr, brace_open) else {
        return fields;
    };
    for row in split_top_level_commas(body) {
        let row = row.trim().trim_end_matches(',');
        if row.is_empty() {
            continue;
        }
        // "name": ("type_fqn", offset)
        let Some(colon) = row.find(':') else {
            continue;
        };
        let name = unquote(row[..colon].trim());
        let rest = row[colon + 1..].trim();
        let Some(popen) = rest.find('(') else {
            continue;
        };
        let Some(inner) = paren_span(rest, popen) else {
            continue;
        };
        let cells = split_top_level_commas(inner);
        if cells.len() >= 2 {
            let type_fqn = unquote(cells[0]);
            let offset: u32 = cells[1].trim().parse().unwrap_or(0);
            if !name.is_empty() {
                fields.push((name, offset, type_fqn));
            }
        }
    }
    fields
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

    /// Canonical PM_TYPE_DEFINE_C shape (pymergetic.metal.trust.Digest):
    /// field array literal + define call with symbol refs. The array rows
    /// carry hash, flags, offset, type, name — the scanner must map the
    /// type token to a builtin fqn and keep source order for staging.
    #[test]
    fn scans_trust_digest_type_define_c() {
        let text = r#"
static const pm_type_field_t s_trust_digest_fields[] = {
    { 0x0A3D, 0, 32, &PM_TYPE_I64_DESC, "created" },
    { 0x79CC, 0, 0, &PM_TYPE_U64_DESC, "w0" },
    { 0x79CD, 0, 8, &PM_TYPE_U64_DESC, "w1" },
    { 0x79CE, 0, 16, &PM_TYPE_U64_DESC, "w2" },
    { 0x79CF, 0, 24, &PM_TYPE_U64_DESC, "w3" },
};
PM_TYPE_DEFINE_C(s_trust_digest_desc, "pymergetic.metal.trust.Digest",
    PM_TYPE_DESC_STRUCT, 40, NULL, s_trust_digest_fields, 5);
"#;
        let ts = scan_pm_type_define_c(text);
        assert_eq!(ts.len(), 1, "one type: {ts:?}");
        let t = &ts[0];
        assert_eq!(t.fqn, "pymergetic.metal.trust.Digest");
        assert_eq!(t.kind, 0, "STRUCT");
        assert_eq!(t.instance_size, 40);
        assert_eq!(t.parent_sym_or_fqn, "NULL");
        assert_eq!(t.fields.len(), 5, "all five rows: {t:?}");
        assert_eq!(t.fields[0], ("created".into(), 32, "pymergetic.types.i64".into()));
        assert_eq!(t.fields[1], ("w0".into(), 0, "pymergetic.types.u64".into()));
        assert_eq!(t.fields[4], ("w3".into(), 24, "pymergetic.types.u64".into()));
    }

    /// The gen binary links PM_TYPE_DEFINE_C ctors of every compiled-in
    /// card (the types card itself), so introspection must skip builtin
    /// descriptor fqns (nil/i32/…/list/dict) — they carry no view payload.
    #[test]
    fn introspect_types_skips_builtin_descriptors() {
        let all = crate::util::r#gen::introspect_types();
        for t in &all {
            let leaf = t.fqn.rsplit('.').next().unwrap_or("");
            assert!(
                !matches!(
                    leaf,
                    "nil" | "i32" | "i64" | "u32" | "u64" | "f32" | "f64" | "bool" | "str"
                        | "bytes" | "list" | "dict"
                ) || !t.fqn.starts_with("pymergetic.types."),
                "builtin leaked into introspection: {}",
                t.fqn
            );
        }
    }

    /// Full staged loop: scan a card's muscle (fixture), stage it, then
    /// view_faces_for_fqn must emit the three view faces with the packed
    /// field rows. This is the metal.trust.Digest end-to-end contract.
    #[test]
    fn staged_type_yields_view_faces() {
        let tmp = std::env::temp_dir().join("wasmmod_gen_type_view_fixture");
        let _ = std::fs::remove_dir_all(&tmp);
        let card = tmp.join("src/pymergetic/metal/geo");
        std::fs::create_dir_all(&card).unwrap();
        std::fs::write(
            card.join("__pmm__.toml"),
            "fqn = \"pymergetic.metal.geo\"\nimpl = \"c\"\n",
        )
        .unwrap();
        std::fs::write(
            card.join("__impl__.c"),
            r#"
static const pm_type_field_t s_point_fields[] = {
    { 0xB61D, 0, 0, &PM_TYPE_F64_DESC, "x" },
    { 0xB61E, 0, 8, &PM_TYPE_F64_DESC, "y" },
};
PM_TYPE_DEFINE_C(s_point_desc, "pymergetic.metal.geo.Point",
    PM_TYPE_DESC_STRUCT, 16, NULL, s_point_fields, 2);
"#,
        )
        .unwrap();
        let n = ensure_card_types(&card);
        assert_eq!(n, 1, "one scanned type");
        let faces = crate::util::r#gen::view_faces_for_fqn("pymergetic.metal.geo").expect("views");
        assert_eq!(faces.len(), 3, "h + rs + pyi: {faces:?}");
        let h = &faces.iter().find(|(n, _)| n == "__view__.h").unwrap().1;
        assert!(h.contains("pm_geo_point_view"), "view struct: {h}");
        assert!(h.contains("double x;"), "f64 cell: {h}");
        assert!(h.contains("#define PM_FIELD_GEO_POINT_X 0xB61Du"));
        let rs = &faces.iter().find(|(n, _)| n == "__view__.rs").unwrap().1;
        assert!(rs.contains("pub struct geo_point_view"), "rs view: {rs}");
        assert!(rs.contains("FIELD_X: u16 = 0xB61D"), "rs const: {rs}");
        let pyi = &faces.iter().find(|(n, _)| n == "__view__.pyi").unwrap().1;
        assert!(pyi.contains("class Point:"), "pyi class: {pyi}");
        assert!(pyi.contains("x: float"), "pyi f64: {pyi}");
        let _ = std::fs::remove_dir_all(&tmp);
    }
}
