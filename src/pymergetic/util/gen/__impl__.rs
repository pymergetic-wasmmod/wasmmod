//! pymergetic.util.gen — emit module faces from **live registry introspection**.
//!
//! No source scanners. Export names + C signature spellings come from
//! `pm_wasmmod_registry_*` after `PM_MOD_EXPORT_*` / `mod_export` registration.
//!
//! Sinks: [`sink::FsSink`] (host FS), [`sink::VfsSink`] (µPy VFS ops),
//! [`sink::MemSink`] (buffer/var). Diff against included autogen bytes is
//! first-class — see [`diff_against_included`].

#![allow(clippy::missing_safety_doc)]

use alloc::boxed::Box;
use alloc::format;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::ffi::c_void;

use crate::wasmmod::registry::{
    pm_wasmmod_registry_container, pm_wasmmod_registry_container_kind_t,
    pm_wasmmod_registry_export_at, pm_wasmmod_registry_export_count,
    pm_wasmmod_registry_export_kind_t, pm_wasmmod_registry_mod_export,
    pm_wasmmod_registry_module_at, pm_wasmmod_registry_module_count,
};

pub mod sink;
pub use sink::{
    apply_faces, apply_one, diff_bytes, ApplyOutcome, GenSink, MemSink, VfsOps, VfsSink,
};

#[cfg(feature = "gen")]
pub use sink::FsSink;

/// One live export as introspected from the registry.
#[derive(Clone, Debug)]
pub struct LiveExport {
    pub name: String,
    pub kind: pm_wasmmod_registry_export_kind_t,
    pub sig: String,
}

/// One live type descriptor as introspected from the type registry
/// (`PM_TYPE_DEFINE_C` / `PM_TYPE_DEFINE_RS!` registration). Gen-only:
/// the freestanding staticlib never introspects for view emission.
#[cfg(feature = "gen")]
#[derive(Clone, Debug)]
pub struct LiveTypeField {
    pub name: String,
    pub name_hash: u16,
    pub offset: u32,
    /// Field type fqn ("" = undeclared / any).
    pub type_fqn: String,
}

#[cfg(feature = "gen")]
#[derive(Clone, Debug)]
pub struct LiveType {
    pub fqn: String,
    pub kind: u16,
    pub instance_size: u16,
    /// Parent fqn ("" = root).
    pub parent_fqn: String,
    pub fields: Vec<LiveTypeField>,
}

/// Face filenames emitted beside a module card / under a sink prefix.
pub const FACE_EXPORTS_H: &str = "__exports__.h";
pub const FACE_EXPORTS_RS: &str = "__exports__.rs";
pub const FACE_INIT_PYI: &str = "__init__.pyi";
/// Typed-view faces (from the live type registry) — same cards, new names.
pub const FACE_VIEW_H: &str = "__view__.h";
pub const FACE_VIEW_RS: &str = "__view__.rs";
pub const FACE_VIEW_PYI: &str = "__view__.pyi";

/// Optional live-µPy provider: fill `out` with `__init__.pyi` text for `fqn`.
/// Return 1 = provided, 0 = no Python surface (use stub), -1 = error.
pub type PyFaceProvider = unsafe extern "C" fn(
    ctx: *mut c_void,
    fqn_ptr: *const u8,
    fqn_len: u32,
    out: *mut u8,
    inout_len: *mut u32,
) -> i32;

struct PyFaceHook {
    f: PyFaceProvider,
    ctx: *mut c_void,
}

// SAFETY: provider is installed/used from the µPy host thread around gen only.
unsafe impl Send for PyFaceHook {}

// SAFETY: set from the µPy port before gen; only used during gen on one thread.
static PY_FACE: crate::util::lock::Mutex<Option<PyFaceHook>> =
    crate::util::lock::Mutex::new(None);

/// Install (or clear with null `f`) the live µPy `__init__.pyi` provider.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_set_py_face_provider(
    f: Option<PyFaceProvider>,
    ctx: *mut c_void,
) {
    *PY_FACE.lock() = f.map(|f| PyFaceHook { f, ctx });
}

fn pyi_via_provider(fqn: &str) -> Option<String> {
    let hook = {
        let guard = PY_FACE.lock();
        match guard.as_ref() {
            Some(h) => PyFaceHook {
                f: h.f,
                ctx: h.ctx,
            },
            None => return None,
        }
    };
    let mut need = 0u32;
    let st = unsafe {
        (hook.f)(
            hook.ctx,
            fqn.as_ptr(),
            fqn.len() as u32,
            core::ptr::null_mut(),
            &mut need,
        )
    };
    if st == 0 {
        return None;
    }
    if st < 0 || need == 0 {
        return None;
    }
    let mut buf = alloc::vec![0u8; need as usize];
    let mut n = need;
    let st = unsafe {
        (hook.f)(
            hook.ctx,
            fqn.as_ptr(),
            fqn.len() as u32,
            buf.as_mut_ptr(),
            &mut n,
        )
    };
    if st != 1 {
        return None;
    }
    buf.truncate(n as usize);
    Some(String::from_utf8_lossy(&buf).into_owned())
}

/// Introspect all exports currently published under `fqn`.
pub fn introspect_exports(fqn: &str) -> Vec<LiveExport> {
    let n = unsafe { pm_wasmmod_registry_export_count(fqn.as_ptr(), fqn.len() as u32) };
    let mut out = Vec::new();
    for i in 0..n {
        let mut name = [0u8; 256];
        let mut name_len = 256u32;
        let mut kind = pm_wasmmod_registry_export_kind_t::Fn;
        let mut sig = [0u8; 512];
        let mut sig_len = 512u32;
        let ok = unsafe {
            pm_wasmmod_registry_export_at(
                fqn.as_ptr(),
                fqn.len() as u32,
                i,
                name.as_mut_ptr(),
                &mut name_len,
                &mut kind,
                sig.as_mut_ptr(),
                &mut sig_len,
            )
        };
        if ok == 0 {
            continue;
        }
        out.push(LiveExport {
            name: String::from_utf8_lossy(&name[..name_len as usize]).into_owned(),
            kind,
            sig: String::from_utf8_lossy(&sig[..sig_len as usize]).into_owned(),
        });
    }
    out
}

/// List every live module fqn in the registry.
/// Live container kind for `fqn`, if published.
pub fn introspect_container(fqn: &str) -> Option<pm_wasmmod_registry_container_kind_t> {
    let k = unsafe { pm_wasmmod_registry_container(fqn.as_ptr(), fqn.len() as u32) };
    match k {
        0 => Some(pm_wasmmod_registry_container_kind_t::Wasm),
        1 => Some(pm_wasmmod_registry_container_kind_t::Aot),
        2 => Some(pm_wasmmod_registry_container_kind_t::Elf),
        3 => Some(pm_wasmmod_registry_container_kind_t::Resident),
        _ => None,
    }
}

pub fn introspect_modules() -> Vec<String> {
    let n = pm_wasmmod_registry_module_count();
    let mut out = Vec::new();
    for i in 0..n {
        let mut buf = [0u8; 512];
        let mut len = 512u32;
        let ok = unsafe { pm_wasmmod_registry_module_at(i, buf.as_mut_ptr(), &mut len) };
        if ok == 0 {
            continue;
        }
        out.push(String::from_utf8_lossy(&buf[..len as usize]).into_owned());
    }
    out
}

/// Resolve exports for `fqn`, falling back to the leaf name when needed.
pub fn resolve_exports(fqn: &str) -> (String, Vec<LiveExport>) {
    let mut exports = introspect_exports(fqn);
    let mut key = String::from(fqn);
    if exports.is_empty()
        && let Some(leaf) = fqn.rsplit('.').next()
    {
        exports = introspect_exports(leaf);
        if !exports.is_empty() {
            key = String::from(leaf);
        }
    }
    (key, exports)
}

/*----------------------------------------------------------------------
 * Live type registry introspection — descriptors registered by
 * PM_TYPE_DEFINE_C / PM_TYPE_DEFINE_RS! (one registry with the
 * module exports). Copy-out C ABI, so no struct-offset assumptions.
 * Declared here directly, not via types/__exports__.rs: gen writes
 * those faces, so it must not depend on them (bootstrap cycle).
 *--------------------------------------------------------------------*/

/* Gen-only: the freestanding staticlib never introspects for views. */
#[cfg(feature = "gen")]
unsafe extern "C" {
    fn pm_types_registry_count() -> u32;
    fn pm_types_registry_type_at(
        index: u32,
        fqn_buf: *mut u8,
        fqn_cap: u32,
        kind: *mut u16,
        instance_size: *mut u16,
        parent_fqn_buf: *mut u8,
        parent_cap: u32,
        field_count: *mut u16,
    ) -> i32;
    fn pm_types_registry_field_at(
        index: u32,
        field: u16,
        name_buf: *mut u8,
        name_cap: u32,
        name_hash: *mut u16,
        offset: *mut u32,
        type_fqn_buf: *mut u8,
        type_cap: u32,
    ) -> i32;
}

#[cfg(feature = "gen")]
fn cstr_from(buf: &[u8]) -> String {
    let n = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    String::from_utf8_lossy(&buf[..n]).into_owned()
}

/// All registered descriptors (index order). Primitive/list/dict
/// builtins are skipped: they carry no view payload.
#[cfg(feature = "gen")]
pub fn introspect_types() -> Vec<LiveType> {
    let n = unsafe { pm_types_registry_count() };
    let mut out = Vec::new();
    for i in 0..n {
        let mut fqn = [0u8; 256];
        let mut kind = 0u16;
        let mut inst = 0u16;
        let mut parent = [0u8; 256];
        let mut nfields = 0u16;
        let ok = unsafe {
            pm_types_registry_type_at(
                i,
                fqn.as_mut_ptr(),
                256,
                &mut kind,
                &mut inst,
                parent.as_mut_ptr(),
                256,
                &mut nfields,
            )
        };
        if ok == 0 {
            continue;
        }
        let fqn_s = cstr_from(&fqn);
        // Builtins (primitives, list, dict) — no view payload.
        if fqn_s.starts_with("pymergetic.types.")
            && matches!(
                fqn_s.rsplit('.').next().unwrap_or(""),
                "nil" | "i32" | "i64" | "u32" | "u64" | "f32" | "f64" | "bool" | "str"
                    | "bytes" | "list" | "dict"
            )
        {
            continue;
        }
        let mut fields = Vec::new();
        for f in 0..nfields {
            let mut name = [0u8; 64];
            let mut hash = 0u16;
            let mut off = 0u32;
            let mut tfqn = [0u8; 256];
            let ok = unsafe {
                pm_types_registry_field_at(
                    i, f, name.as_mut_ptr(), 64, &mut hash, &mut off,
                    tfqn.as_mut_ptr(), 256,
                )
            };
            if ok == 0 {
                // Partial row (NULL name / staged miss): a partial view is
                // worse than none — drop the whole type.
                fields.clear();
                break;
            }
            fields.push(LiveTypeField {
                name: cstr_from(&name),
                name_hash: hash,
                offset: off,
                type_fqn: cstr_from(&tfqn),
            });
        }
        if fields.is_empty() && nfields > 0 {
            continue;
        }
        out.push(LiveType {
            fqn: fqn_s,
            kind,
            instance_size: inst,
            parent_fqn: cstr_from(&parent),
            fields,
        });
    }
    out
}

/// Types whose fqn starts with `fqn.` (the card's namespace) —
/// `pymergetic.metal.geo` owns `pymergetic.metal.geo.Point`.
#[cfg(feature = "gen")]
pub fn types_for_fqn(fqn: &str) -> Vec<LiveType> {
    let prefix = alloc::format!("{fqn}.");
    introspect_types()
        .into_iter()
        .filter(|t| t.fqn.starts_with(&prefix))
        .collect()
}

/// Format a C prototype line from registry sig + export name.
/// `sig` is `ret(args)` as stored by `PM_MOD_EXPORT_C` (e.g. `int(void)`).
pub fn prototype_line(name: &str, sig: &str) -> String {
    let sig = sig.trim();
    if sig.is_empty() {
        return format!("/* no sig */ void {name}(void);");
    }
    let Some(open) = sig.find('(') else {
        return format!("{sig} {name}(void);");
    };
    let ret = sig[..open].trim();
    let args = &sig[open..];
    format!("{ret} {name}{args};")
}

/// Pure signature inspection — no host FS, so every sink can call it.
fn c_sig_needs_local_types(sig: &str) -> bool {
    for word in sig.split(|c: char| !(c.is_ascii_alphanumeric() || c == '_')) {
        if word.is_empty() {
            continue;
        }
        match word {
            "void" | "int" | "int32_t" | "uint32_t" | "int64_t" | "uint64_t" | "uint8_t"
            | "int8_t" | "uint16_t" | "int16_t" | "size_t" | "char" | "float" | "double"
            | "bool" | "_Bool" | "unsigned" | "const" | "struct" => {}
            _ => return true,
        }
    }
    false
}

/// `types_on_disk`: does this card ship a `__types__.h`? The caller knows the
/// card directory; this function must not look for one.
pub fn emit_exports_h(fqn: &str, exports: &[LiveExport], types_on_disk: bool) -> String {
    let guard = guard_name(fqn, "EXPORT");
    let mut out = String::new();
    out.push_str("/* DO NOT EDIT — generated by `pymergetic.util.gen` (live registry introspection).\n");
    out.push_str(" * Source of truth: pm_wasmmod_registry_* after PM_MOD_EXPORT_* / PM_MOD_EXPORT_RS! registration.\n");
    out.push_str(" */\n");
    out.push_str(&format!("#ifndef {guard}\n#define {guard}\n\n"));
    out.push_str("#include <stddef.h>\n");
    out.push_str("#include <stdint.h>\n\n");
    let types_rel = alloc::format!("{}/__types__.h", fqn.replace('.', "/"));
    let mut includes: Vec<String> = Vec::new();
    if exports.iter().any(|e| {
        e.sig.contains("pm_wasmmod_registry_")
            || e.sig.contains("pm_addr_t")
            || e.sig.contains("pm_buf_t")
            || e.sig.contains("pm_wasmmod_registry_fn_t")
    }) {
        includes.push(String::from("pymergetic/wasmmod/registry/__types__.h"));
    }
    if exports.iter().any(|e| {
        e.sig.contains("pm_wasmmod_mem_cookie_t") || e.sig.contains("pm_wasmmod_obj_handle_t")
    }) {
        includes.push(String::from("pymergetic/wasmmod/host/__types__.h"));
    }
    if exports.iter().any(|e| e.sig.contains("pm_util_mem_")) {
        includes.push(String::from("pymergetic/util/mem/__types__.h"));
    }
    if exports.iter().any(|e| e.sig.contains("pm_wasmmod_io_")) {
        includes.push(String::from("pymergetic/wasmmod/io/__types__.h"));
    }
    if exports.iter().any(|e| {
        e.sig.contains("pm_type_value_t") || e.sig.contains("pm_type_descriptor_t")
    }) {
        includes.push(String::from("pymergetic/types/__types__.h"));
    }
    // Module-local types face — only when a prototype names a non-builtin type
    // (otherwise clangd IWYU: "included header __types__.h is not used directly").
    // Whether the card has one is the caller's to answer: it holds the card
    // directory, so this emitter never guesses at a tree layout.
    if types_on_disk && exports.iter().any(|e| c_sig_needs_local_types(&e.sig)) {
        includes.push(types_rel);
    }
    includes.sort();
    includes.dedup();
    if !includes.is_empty() {
        for inc in &includes {
            out.push_str(&alloc::format!("#include \"{inc}\"\n"));
        }
        out.push('\n');
    }
    out.push_str("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    for e in exports {
        out.push_str(&prototype_line(&e.name, &e.sig));
        out.push_str("\n\n");
    }
    out.push_str("#ifdef __cplusplus\n}\n#endif\n\n");
    out.push_str(&format!("#endif /* {guard} */\n"));
    out
}

/// Map a C type spelling from registry sigs to a Rust extern type.
/// Returns `(rust_ty, needs_c_void, opaque_typedef_name)`.
fn map_c_type_to_rust(c: &str) -> (String, bool, Option<String>) {
    let t = c.split_whitespace().collect::<Vec<_>>().join(" ");
    let t = t.trim();
    if t.is_empty() || t == "void" {
        return (String::from("()"), false, None);
    }
    // Function pointers / unparseable → opaque void*.
    if t.contains('(') {
        return (String::from("*mut c_void"), true, None);
    }
    let (base, stars, is_const) = {
        let mut s = t.to_string();
        let mut stars = 0usize;
        loop {
            let trimmed = s.trim_end();
            if let Some(rest) = trimmed.strip_suffix('*') {
                stars += 1;
                s = rest.trim_end().to_string();
            } else {
                break;
            }
        }
        let is_const = if let Some(rest) = s.strip_prefix("const ") {
            s = rest.trim().to_string();
            true
        } else {
            false
        };
        let base = s.strip_prefix("const ").unwrap_or(&s).trim().to_string();
        (base, stars, is_const)
    };
    if stars == 0 {
        let rust = match base.as_str() {
            "void" => "()",
            "int" | "int32_t" | "pm_wasmmod_mem_cookie_t" | "pm_wasmmod_obj_handle_t" => "i32",
            "uint16_t" | "int16_t" => "u16",
            "int8_t" => "i8",
            "uint32_t" | "unsigned" | "unsigned int" => "u32",
            "int64_t" => "i64",
            "uint64_t" => "u64",
            "size_t" => "usize",
            "float" => "f32",
            "double" => "f64",
            "bool" | "_Bool" => "bool",
            other => other, // by-value typedef (layout owned by __impl__/__types__)
        };
        return (String::from(rust), false, None);
    }
    // Innermost pointed-to type. Every `*` is peeled (`uint8_t **` → `*mut *mut u8`,
    // not an illegal opaque named `uint8_t *`). Innermost `*` carries `const`.
    let (inner, need_void, opaque): (String, bool, Option<String>) = match base.as_str() {
        "void" => (String::from("c_void"), true, None),
        "uint8_t" | "int8_t" | "char" | "unsigned char" => (String::from("u8"), false, None),
        "uint16_t" | "int16_t" => (String::from("u16"), false, None),
        "int32_t" | "int" => (String::from("i32"), false, None),
        "uint32_t" => (String::from("u32"), false, None),
        other => (other.to_string(), false, Some(other.to_string())),
    };
    let mut ty = inner;
    for i in 0..stars {
        if i == 0 && is_const {
            ty = format!("*const {ty}");
        } else {
            ty = format!("*mut {ty}");
        }
    }
    (ty, need_void, opaque)
}

fn split_c_params(args: &str) -> Vec<String> {
    let args = args.trim();
    if args.is_empty() || args == "void" {
        return Vec::new();
    }
    let mut out = Vec::new();
    let mut depth = 0i32;
    let mut start = 0usize;
    for (i, ch) in args.char_indices() {
        match ch {
            '(' => depth += 1,
            ')' => depth -= 1,
            ',' if depth == 0 => {
                out.push(args[start..i].trim().to_string());
                start = i + 1;
            }
            _ => {}
        }
    }
    let last = args[start..].trim();
    if !last.is_empty() {
        out.push(last.to_string());
    }
    out
}

/// `ret(args)` → `pub fn name(...) -> ...;` plus opaque typedefs needed.
pub fn rust_prototype_line(name: &str, sig: &str) -> (String, Vec<String>, bool) {
    let sig = sig.trim();
    let mut opaques = Vec::new();
    let mut needs_c_void = false;
    if sig.is_empty() {
        return (
            format!("    pub fn {name}();"),
            opaques,
            needs_c_void,
        );
    }
    let Some(open) = sig.find('(') else {
        return (
            format!("    pub fn {name}();"),
            opaques,
            needs_c_void,
        );
    };
    let ret_c = sig[..open].trim();
    let close = sig.rfind(')').unwrap_or(sig.len());
    let args_c = &sig[open + 1..close];
    let (ret_rs, ret_void, ret_op) = map_c_type_to_rust(ret_c);
    needs_c_void |= ret_void;
    if let Some(o) = ret_op {
        opaques.push(o);
    }
    let mut params = Vec::new();
    for (i, p) in split_c_params(args_c).into_iter().enumerate() {
        let (ty, nv, op) = map_c_type_to_rust(&p);
        needs_c_void |= nv;
        if let Some(o) = op {
            opaques.push(o);
        }
        if ty == "()" {
            continue;
        }
        params.push(format!("a{i}: {ty}"));
    }
    let line = if ret_rs == "()" {
        format!("    pub fn {name}({});", params.join(", "))
    } else {
        format!("    pub fn {name}({}) -> {ret_rs};", params.join(", "))
    };
    (line, opaques, needs_c_void)
}

pub fn emit_exports_rs(exports: &[LiveExport]) -> String {
    let mut out = String::new();
    out.push_str("//! DO NOT EDIT — generated by `pymergetic.util.gen` (live registry introspection).\n\n");
    out.push_str("#![allow(non_camel_case_types)]\n\n");
    let mut lines = Vec::new();
    let mut opaques = Vec::new();
    let mut needs_c_void = false;
    for e in exports {
        let (line, ops, nv) = rust_prototype_line(&e.name, &e.sig);
        lines.push(line);
        opaques.extend(ops);
        needs_c_void |= nv;
    }
    opaques.sort();
    opaques.dedup();
    if needs_c_void {
        out.push_str("use core::ffi::c_void;\n\n");
    }
    for ty in &opaques {
        // Opaque C typedef stand-in for IDE / typecheck (real layout lives in __impl__/C).
        out.push_str(&format!(
            "#[repr(C)]\npub struct {ty} {{\n    _opaque: [u8; 0],\n}}\n\n"
        ));
    }
    out.push_str("unsafe extern \"C\" {\n");
    for line in lines {
        out.push_str(&line);
        out.push('\n');
    }
    out.push_str("}\n");
    out
}

pub fn emit_init_pyi(fqn: &str, funcs: &[(String, String)]) -> String {
    let mut out = String::new();
    out.push_str("# DO NOT EDIT — generated by `pymergetic.util.gen` (live µPy import).\n");
    out.push_str(&format!("# {fqn}\n\n"));
    if funcs.is_empty() {
        out.push_str("# No live Python callables introspected.\n");
        return out;
    }
    let mut kws: Vec<&str> = Vec::new();
    out.push_str("from typing import Any\n\n");
    for (name, stub) in funcs {
        if stub.is_empty() {
            if is_python_keyword(name) {
                kws.push(name);
            } else {
                out.push_str(&format!("def {name}(*args: Any, **kwargs: Any) -> Any: ...\n\n"));
            }
        } else {
            out.push_str(stub);
            out.push('\n');
        }
    }
    // A keyword can never be a def name or an annotation target in valid
    // Python syntax. The runtime attribute is real (the loader sets it via
    // setattr), so expose it through module __getattr__ with a Literal of
    // the name — the only PEP 484 way to type a dynamic attribute.
    if !kws.is_empty() {
        out.push_str("from typing import Callable, Literal\n\n");
        for kw in kws {
            out.push_str(&format!(
                "def __getattr__(name: Literal[\"{kw}\"]) -> Callable[..., Any]: ...\n\n"
            ));
        }
    }
    out
}

/// Reserved words that cannot appear as a def name in valid Python syntax.
/// Soft keywords (match, case, _) are legal function names — excluded.
pub fn is_python_keyword(name: &str) -> bool {
    matches!(
        name,
        "False" | "None" | "True" | "and" | "as" | "assert" | "async"
            | "await" | "break" | "class" | "continue" | "def" | "del"
            | "elif" | "else" | "except" | "finally" | "for" | "from"
            | "global" | "if" | "import" | "in" | "is" | "lambda" | "nonlocal"
            | "not" | "or" | "pass" | "raise" | "return" | "try" | "while"
            | "with" | "yield"
    )
}

/// Build a rich pyi from callable names (µPy port / tests).
pub fn emit_init_pyi_names(fqn: &str, names: &[&str]) -> String {
    let funcs: Vec<(String, String)> = names
        .iter()
        .map(|n| (String::from(*n), String::new()))
        .collect();
    emit_init_pyi(fqn, &funcs)
}

/// Python attribute hostready attaches for a C/RS export.
/// `pymergetic.wasmmod.loader` + `pm_wasmmod_loader_open` → `open`.
pub fn py_attr_from_export(fqn: &str, cname: &str) -> String {
    let mut prefix = String::from("pm_");
    if let Some((_, rest)) = fqn.split_once('.') {
        for c in rest.chars() {
            prefix.push(if c == '.' { '_' } else { c });
        }
    }
    prefix.push('_');
    if let Some(leaf) = cname.strip_prefix(prefix.as_str())
        && !leaf.is_empty()
    {
        return String::from(leaf);
    }
    match cname.rsplit_once('_') {
        Some((_, leaf)) if !leaf.is_empty() => String::from(leaf),
        _ => String::from(cname),
    }
}

/// `__init__.pyi` from registry C/RS names when live µPy introspection is absent.
pub fn emit_init_pyi_from_exports(fqn: &str, exports: &[LiveExport]) -> String {
    let mut names: Vec<String> = exports
        .iter()
        .map(|e| py_attr_from_export(fqn, &e.name))
        .collect();
    names.sort();
    names.dedup();
    let refs: Vec<&str> = names.iter().map(String::as_str).collect();
    emit_init_pyi_names(fqn, &refs)
}

/*----------------------------------------------------------------------
 * Typed-view faces — emitted from the live *type* registry (one pass
 * with export faces; same source of truth, same card dirs).
 * Gen-only (host facegen); the freestanding staticlib never emits.
 *--------------------------------------------------------------------*/

/// Field type fqn → C spelling in the view struct (packed cell).
#[cfg(feature = "gen")]
fn view_c_type(t: &LiveTypeField) -> &'static str {
    match t.type_fqn.as_str() {
        "pymergetic.types.i32" => "int32_t",
        "pymergetic.types.i64" => "int64_t",
        "pymergetic.types.u32" => "uint32_t",
        "pymergetic.types.u64" => "uint64_t",
        "pymergetic.types.f32" => "float",
        "pymergetic.types.f64" => "double",
        "pymergetic.types.bool" => "uint8_t",
        _ => "pm_type_value_t", // str/bytes/struct/undeclared → value cell
    }
}

/// Field type fqn → Rust spelling in the view struct.
#[cfg(feature = "gen")]
fn view_rs_type(t: &LiveTypeField) -> &'static str {
    match t.type_fqn.as_str() {
        "pymergetic.types.i32" => "i32",
        "pymergetic.types.i64" => "i64",
        "pymergetic.types.u32" => "u32",
        "pymergetic.types.u64" => "u64",
        "pymergetic.types.f32" => "f32",
        "pymergetic.types.f64" => "f64",
        "pymergetic.types.bool" => "bool",
        _ => "pm_type_value_t",
    }
}

/// Field type fqn → pyi annotation.
#[cfg(feature = "gen")]
fn view_py_type(t: &LiveTypeField) -> String {
    match t.type_fqn.as_str() {
        "pymergetic.types.i32" | "pymergetic.types.i64" | "pymergetic.types.u32"
        | "pymergetic.types.u64" => String::from("int"),
        "pymergetic.types.f32" | "pymergetic.types.f64" => String::from("float"),
        "pymergetic.types.bool" => String::from("bool"),
        "pymergetic.types.str" => String::from("str"),
        "pymergetic.types.bytes" => String::from("bytes"),
        _ => String::from("Any"), // struct/undeclared
    }
}

/// Field size in bytes within a packed instance (must match the
/// registry's packing rule — see pm_types_registry_stage_commit's
/// instance_size math, which uses the same table).
#[cfg(feature = "gen")]
fn view_field_size(t: &LiveTypeField) -> u32 {
    match t.type_fqn.as_str() {
        "pymergetic.types.i32" | "pymergetic.types.u32" => 4,
        "pymergetic.types.i64" | "pymergetic.types.u64" | "pymergetic.types.f64" => 8,
        "pymergetic.types.f32" => 4,
        "pymergetic.types.bool" => 1,
        _ => 16, // value cell (str/bytes/struct/undeclared)
    }
}

/// `pymergetic.metal.geo.Point` → (`geo_point`, `Point`)
#[cfg(feature = "gen")]
fn view_names(fqn: &str) -> (String, String) {
    let mut it = fqn.rsplit('.');
    let leaf = String::from(it.next().unwrap_or("point"));
    let card = it.next().unwrap_or("t");
    (format!("{}_{}", card.to_ascii_lowercase(), leaf.to_ascii_lowercase()), leaf)
}

/// `__view__.h` — packed C view structs + field-hash constants +
/// ergonomic constructors for every live type in the card namespace.
#[cfg(feature = "gen")]
pub fn emit_view_h(fqn: &str, types: &[LiveType]) -> String {
    let guard = guard_name(fqn, "VIEW");
    let mut out = String::new();
    out.push_str("/* DO NOT EDIT — generated by `pymergetic.util.gen` (live type registry).\n");
    out.push_str(" * Source of truth: pm_types_registry_* after PM_TYPE_DEFINE_C / RS registration.\n");
    out.push_str(" */\n");
    out.push_str(&alloc::format!("#ifndef {guard}\n#define {guard}\n\n"));
    out.push_str("#include <stddef.h>\n#include <stdint.h>\n\n");
    out.push_str("#include \"pymergetic/types/__types__.h\"\n\n");
    out.push_str("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
    for t in types {
        let (low, _leaf) = view_names(&t.fqn);
        out.push_str(&alloc::format!("/*-- {fqn_leaf} (kind {k}, {sz} bytes) --*/\n",
            fqn_leaf = t.fqn, k = t.kind, sz = t.instance_size));
        /* Offset order + explicit padding: a zero-copy view must mirror
         * the packed layout, not the name-hash lookup order. */
        let mut fields = t.fields.clone();
        fields.sort_by_key(|f| f.offset);
        let mut body = String::new();
        let mut cursor: u32 = 0;
        let mut pad = 0u32;
        for f in &fields {
            if f.offset > cursor {
                body.push_str(&alloc::format!(
                    "    uint8_t pm_pad{p}[0x{n:X}];\n",
                    p = pad, n = f.offset - cursor));
                pad += 1;
            }
            body.push_str(&alloc::format!(
                "    {} {};\n", view_c_type(f), f.name));
            cursor = f.offset + view_field_size(f);
        }
        if (t.instance_size as u32) > cursor {
            body.push_str(&alloc::format!(
                "    uint8_t pm_pad{p}[0x{n:X}];\n",
                p = pad, n = t.instance_size as u32 - cursor));
        }
        out.push_str(&alloc::format!(
            "typedef struct {{\n{fields}}} pm_{low}_view_t;\n\n",
            fields = body,
        ));
        for f in &t.fields {
            out.push_str(&alloc::format!(
                "#define PM_FIELD_{low}_{name} 0x{hash:04X}u\n",
                low = low.to_ascii_uppercase(),
                name = f.name.to_ascii_uppercase(),
                hash = f.name_hash
            ));
        }
        out.push('\n');
    }
    out.push_str("#ifdef __cplusplus\n}\n#endif\n\n");
    out.push_str(&alloc::format!("#endif /* {guard} */\n"));
    out
}

/// `__view__.rs` — `#[repr(C)]` view structs with field-hash consts.
#[cfg(feature = "gen")]
pub fn emit_view_rs(fqn: &str, types: &[LiveType]) -> String {
    let _ = fqn;
    let mut out = String::new();
    out.push_str("//! DO NOT EDIT — generated by `pymergetic.util.gen` (live type registry).\n\n");
    out.push_str("#![allow(non_camel_case_types)]\n\n");
    out.push_str("use pymergetic::types::{pm_type_value_t, pm_util_mem_arena_t};\n\n");
    for t in types {
        let (low, _leaf) = view_names(&t.fqn);
        out.push_str(&alloc::format!("/// {fqn_t} — {k} bytes instance data.\n",
            fqn_t = t.fqn, k = t.instance_size));
        /* Offset order + explicit padding — mirror the C view. */
        let mut fields = t.fields.clone();
        fields.sort_by_key(|f| f.offset);
        let mut body = String::new();
        let mut cursor: u32 = 0;
        let mut pad = 0u32;
        for f in &fields {
            if f.offset > cursor {
                body.push_str(&alloc::format!(
                    "    pub pm_pad{p}: [u8; 0x{n:X}],\n",
                    p = pad, n = f.offset - cursor));
                pad += 1;
            }
            body.push_str(&alloc::format!(
                "    pub {}: {},\n", f.name, view_rs_type(f)));
            cursor = f.offset + view_field_size(f);
        }
        if (t.instance_size as u32) > cursor {
            body.push_str(&alloc::format!(
                "    pub pm_pad{p}: [u8; 0x{n:X}],\n",
                p = pad, n = t.instance_size as u32 - cursor));
        }
        out.push_str(&alloc::format!(
            "#[repr(C)]\npub struct {low}_view {{\n{fields}}}\n\n",
            fields = body
        ));
        out.push_str(&alloc::format!("impl {low}_view {{\n"));
        for f in &t.fields {
            out.push_str(&alloc::format!(
                "    pub const FIELD_{}: u16 = 0x{:04X};\n",
                f.name.to_ascii_uppercase(),
                f.name_hash
            ));
        }
        out.push_str("}\n\n");
    }
    out
}

/// `__view__.pyi` — Python model classes for the card's types.
#[cfg(feature = "gen")]
pub fn emit_view_pyi(fqn: &str, types: &[LiveType]) -> String {
    let _ = fqn;
    let mut out = String::new();
    out.push_str("# DO NOT EDIT — generated by `pymergetic.util.gen` (live type registry).\n\n");
    out.push_str("from typing import Any\n\n");
    for t in types {
        let (_low, leaf) = view_names(&t.fqn);
        out.push_str(&alloc::format!(
            "class {leaf}:\n    \"\"\"{fqn_t} — {k} bytes instance data.\"\"\"\n",
            fqn_t = t.fqn,
            k = t.instance_size
        ));
        for f in &t.fields {
            out.push_str(&alloc::format!("    {}: {} = ...\n", f.name, view_py_type(f)));
        }
        out.push('\n');
    }
    out
}

/// View faces for a card fqn. `None` when the card owns no types.
#[cfg(feature = "gen")]
pub fn view_faces_for_fqn(fqn: &str) -> Option<Vec<(String, String)>> {
    let types = types_for_fqn(fqn);
    if types.is_empty() {
        return None;
    }
    Some(alloc::vec![
        (String::from(FACE_VIEW_H), emit_view_h(fqn, &types)),
        (String::from(FACE_VIEW_RS), emit_view_rs(fqn, &types)),
        (String::from(FACE_VIEW_PYI), emit_view_pyi(fqn, &types)),
    ])
}

/// Build the face artifacts for `fqn` from the live registry.
/// Returns `None` when the fqn is not registered / has no exports.
pub fn faces_for_fqn(fqn: &str, types_on_disk: bool) -> Option<Vec<(String, String)>> {
    let (_key, exports) = resolve_exports(fqn);
    if exports.is_empty() {
        return None;
    }
    #[allow(unused_mut)]
    let mut faces = alloc::vec![
        (String::from(FACE_EXPORTS_H), emit_exports_h(fqn, &exports, types_on_disk)),
        (String::from(FACE_EXPORTS_RS), emit_exports_rs(&exports)),
        (String::from(FACE_INIT_PYI), match pyi_via_provider(fqn) {
            Some(rich) => rich,
            None => emit_init_pyi_from_exports(fqn, &exports),
        }),
    ];
    #[cfg(feature = "gen")]
    if let Some(mut views) = view_faces_for_fqn(fqn) {
        faces.append(&mut views);
    }
    Some(faces)
}

/// Join sink directory prefix with a face filename (`""` → bare filename).
pub fn face_path(dir: &str, name: &str) -> String {
    if dir.is_empty() {
        return String::from(name);
    }
    let mut out = String::from(dir);
    if !out.ends_with('/') {
        out.push('/');
    }
    out.push_str(name);
    out
}

/// Emit faces for `fqn` into `sink` under `dir` (check or write).
/// Returns 0 = ok, 1 = drift/missing under check, -1 = not in registry / sink error.
pub fn gen_fqn_to_sink(sink: &mut dyn GenSink, dir: &str, fqn: &str, check_only: bool) -> i32 {
    let Some(faces) = faces_for_fqn(fqn, false) else {
        return -1;
    };
    let paths: Vec<(String, String)> = faces
        .into_iter()
        .map(|(name, content)| (face_path(dir, &name), content))
        .collect();
    match apply_faces(sink, &paths, check_only) {
        Ok(true) => 1,
        Ok(false) => 0,
        Err(_) => -1,
    }
}

/// Diff live-generated faces against **included** (in-image / baseline) bytes.
///
/// Pass `None` for a face to skip it. Empty slice counts as present and must
/// match generated content. Returns 0 = match, 1 = drift, -1 = fqn missing.
pub fn diff_against_included(
    fqn: &str,
    included_h: Option<&[u8]>,
    included_rs: Option<&[u8]>,
    included_pyi: Option<&[u8]>,
) -> i32 {
    let Some(faces) = faces_for_fqn(fqn, false) else {
        return -1;
    };
    let mut drift = false;
    for (name, content) in faces {
        let included = match name.as_str() {
            FACE_EXPORTS_H => included_h,
            FACE_EXPORTS_RS => included_rs,
            FACE_INIT_PYI => included_pyi,
            _ => None,
        };
        match diff_bytes(&content, included) {
            ApplyOutcome::Drift | ApplyOutcome::Missing => drift = true,
            ApplyOutcome::Unchanged | ApplyOutcome::Wrote => {}
        }
    }
    if drift {
        1
    } else {
        0
    }
}

fn guard_name(fqn: &str, kind: &str) -> String {
    let mid = fqn
        .split('.')
        .skip(1)
        .map(|p| p.to_ascii_uppercase())
        .collect::<Vec<_>>()
        .join("_");
    format!("PYMERGETIC_{mid}_{kind}_H")
}

/// Register a resident export with signature (Rust callers / tests).
///
/// # Safety
/// `ptr` must be a valid native function pointer for `sig` (or a facegen stub).
pub unsafe fn register_fn(fqn: &str, name: &str, ptr: *mut c_void, sig: &str) -> bool {
    // `pm_wasmmod_registry_mod_export` stages pointer-only records for
    // constructor literals. Discover/tests pass transient `&str` — copy to
    // leaked statics so `drain_staged` still sees valid UTF-8.
    let fqn = Box::leak(fqn.to_string().into_boxed_str());
    let name = Box::leak(name.to_string().into_boxed_str());
    let sig = Box::leak(sig.to_string().into_boxed_str());
    unsafe {
        pm_wasmmod_registry_mod_export(
            fqn.as_ptr(),
            fqn.len() as u32,
            name.as_ptr(),
            name.len() as u32,
            pm_wasmmod_registry_export_kind_t::Fn,
            ptr,
            sig.as_ptr(),
            sig.len() as u32,
        ) != 0
    }
}

#[cfg(feature = "gen")]
mod discover;

#[cfg(feature = "gen")]
mod host;

#[cfg(feature = "gen")]
pub use host::{gen_run, gen_run_path, gen_run_paths};

#[cfg(not(feature = "gen"))]
/// Stub when `gen` feature is off (freestanding links).
pub fn gen_run_path(_root: &str, _check: bool) -> i32 {
    -1
}

fn root_from_ptr(root_ptr: *const u8, root_len: u32) -> Option<&'static str> {
    if root_ptr.is_null() {
        return None;
    }
    let bytes = unsafe { core::slice::from_raw_parts(root_ptr, root_len as usize) };
    core::str::from_utf8(bytes).ok()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_run(
    root_ptr: *const u8,
    root_len: u32,
    check_only: i32,
) -> i32 {
    let Some(root) = root_from_ptr(root_ptr, root_len) else {
        return -1;
    };
    #[cfg(feature = "gen")]
    {
        gen_run_path(root, check_only != 0)
    }
    #[cfg(not(feature = "gen"))]
    {
        let _ = (root, check_only);
        -1
    }
}

/// Emit/check one fqn into a VFS sink (`ops` + `ctx`). `dir` may be empty
/// (`dir_ptr == NULL && dir_len == 0`).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_run_vfs(
    dir_ptr: *const u8,
    dir_len: u32,
    fqn_ptr: *const u8,
    fqn_len: u32,
    check_only: i32,
    ops: VfsOps,
    ctx: *mut c_void,
) -> i32 {
    let dir = if dir_ptr.is_null() && dir_len == 0 {
        ""
    } else {
        match root_from_ptr(dir_ptr, dir_len) {
            Some(s) => s,
            None => return -1,
        }
    };
    let Some(fqn) = root_from_ptr(fqn_ptr, fqn_len) else {
        return -1;
    };
    let mut sink = VfsSink { ctx, ops };
    gen_fqn_to_sink(&mut sink, dir, fqn, check_only != 0)
}

/// Diff live faces for `fqn` vs included autogen bytes (NULL ptr = skip face).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_diff_included(
    fqn_ptr: *const u8,
    fqn_len: u32,
    h_ptr: *const u8,
    h_len: u32,
    rs_ptr: *const u8,
    rs_len: u32,
    pyi_ptr: *const u8,
    pyi_len: u32,
) -> i32 {
    let Some(fqn) = root_from_ptr(fqn_ptr, fqn_len) else {
        return -1;
    };
    let h = if h_ptr.is_null() {
        None
    } else {
        Some(unsafe { core::slice::from_raw_parts(h_ptr, h_len as usize) })
    };
    let rs = if rs_ptr.is_null() {
        None
    } else {
        Some(unsafe { core::slice::from_raw_parts(rs_ptr, rs_len as usize) })
    };
    let pyi = if pyi_ptr.is_null() {
        None
    } else {
        Some(unsafe { core::slice::from_raw_parts(pyi_ptr, pyi_len as usize) })
    };
    diff_against_included(fqn, h, rs, pyi)
}

/// Emit faces for `fqn` into an opaque MemSink handle (heap). Caller frees with
/// `pm_util_gen_mem_sink_free`. Returns 0 ok, 1 drift (check), -1 error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_run_mem(
    dir_ptr: *const u8,
    dir_len: u32,
    fqn_ptr: *const u8,
    fqn_len: u32,
    check_only: i32,
    sink_out: *mut *mut MemSink,
) -> i32 {
    if sink_out.is_null() {
        return -1;
    }
    let dir = if dir_ptr.is_null() && dir_len == 0 {
        ""
    } else {
        match root_from_ptr(dir_ptr, dir_len) {
            Some(s) => s,
            None => return -1,
        }
    };
    let Some(fqn) = root_from_ptr(fqn_ptr, fqn_len) else {
        return -1;
    };
    let sink = if unsafe { *sink_out }.is_null() {
        alloc::boxed::Box::new(MemSink::new())
    } else {
        unsafe { alloc::boxed::Box::from_raw(*sink_out) }
    };
    let mut sink = sink;
    let rc = gen_fqn_to_sink(&mut *sink, dir, fqn, check_only != 0);
    unsafe {
        *sink_out = alloc::boxed::Box::into_raw(sink);
    }
    rc
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_mem_sink_free(sink: *mut MemSink) {
    if !sink.is_null() {
        drop(unsafe { alloc::boxed::Box::from_raw(sink) });
    }
}

/// Read one path from a MemSink. Returns length written, 0 if missing, -1 error.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_gen_mem_sink_read(
    sink: *const MemSink,
    path_ptr: *const u8,
    path_len: u32,
    buf: *mut u8,
    buf_len: u32,
) -> i32 {
    if sink.is_null() {
        return -1;
    }
    let Some(path) = root_from_ptr(path_ptr, path_len) else {
        return -1;
    };
    let sink = unsafe { &*sink };
    let Some(data) = sink.get(path) else {
        return 0;
    };
    if buf.is_null() {
        return data.len() as i32;
    }
    let n = core::cmp::min(data.len(), buf_len as usize);
    unsafe {
        core::ptr::copy_nonoverlapping(data.as_ptr(), buf, n);
    }
    n as i32
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_set_py_face_provider,
    "void(pm_util_gen_py_face_fn, void *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_run,
    "int32_t(const uint8_t *, uint32_t, int32_t)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_run_vfs,
    "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, int32_t, pm_util_gen_vfs_ops_t, void *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_diff_included,
    "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t, const uint8_t *, uint32_t)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_run_mem,
    "int32_t(const uint8_t *, uint32_t, const uint8_t *, uint32_t, int32_t, pm_util_gen_mem_sink_t **)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_mem_sink_free,
    "void(pm_util_gen_mem_sink_t *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.gen",
    pm_util_gen_mem_sink_read,
    "int32_t(const pm_util_gen_mem_sink_t *, const uint8_t *, uint32_t, uint8_t *, uint32_t)"
);

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
