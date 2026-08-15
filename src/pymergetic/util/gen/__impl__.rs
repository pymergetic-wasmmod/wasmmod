//! pymergetic.util.gen — emit module faces from **live registry introspection**.
//!
//! No source scanners. Export names + C signature spellings come from
//! `pm_wasmmod_registry_*` after `PM_MOD_EXPORT_*` / `mod_export` registration.
//!
//! Sinks: [`sink::FsSink`] (host FS), [`sink::VfsSink`] (µPy VFS ops),
//! [`sink::MemSink`] (buffer/var). Diff against included autogen bytes is
//! first-class — see [`diff_against_included`].

#![allow(clippy::missing_safety_doc)]

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

/// Face filenames emitted beside a module card / under a sink prefix.
pub const FACE_EXPORTS_H: &str = "__exports__.h";
pub const FACE_EXPORTS_RS: &str = "__exports__.rs";
pub const FACE_INIT_PYI: &str = "__init__.pyi";

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

#[cfg(feature = "gen")]
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

pub fn emit_exports_h(fqn: &str, exports: &[LiveExport]) -> String {
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
    // Module-local types face — only when a prototype names a non-builtin type
    // (otherwise clangd IWYU: "included header __types__.h is not used directly").
    #[cfg(feature = "gen")]
    {
        let crate_dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"));
        let on_disk = crate_dir.join("src").join(&types_rel).is_file()
            || crate_dir.join("../metal/src").join(&types_rel).is_file();
        if on_disk && exports.iter().any(|e| c_sig_needs_local_types(&e.sig)) {
            includes.push(types_rel);
        }
    }
    #[cfg(not(feature = "gen"))]
    {
        let _ = types_rel;
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
    out.push_str("from typing import Any\n\n");
    for (name, stub) in funcs {
        if stub.is_empty() {
            out.push_str(&format!("def {name}(*args: Any, **kwargs: Any) -> Any: ...\n\n"));
        } else {
            out.push_str(stub);
            out.push('\n');
        }
    }
    out
}

/// Build a rich pyi from callable names (µPy port / tests).
pub fn emit_init_pyi_names(fqn: &str, names: &[&str]) -> String {
    let funcs: Vec<(String, String)> = names
        .iter()
        .map(|n| (String::from(*n), String::new()))
        .collect();
    emit_init_pyi(fqn, &funcs)
}

/// Build the three face artifacts for `fqn` from the live registry.
/// Returns `None` when the fqn is not registered / has no exports.
pub fn faces_for_fqn(fqn: &str) -> Option<Vec<(String, String)>> {
    let (_key, exports) = resolve_exports(fqn);
    if exports.is_empty() {
        return None;
    }
    let h = emit_exports_h(fqn, &exports);
    let rs = emit_exports_rs(&exports);
    let pyi = match pyi_via_provider(fqn) {
        Some(rich) => rich,
        None => emit_init_pyi(fqn, &[]),
    };
    Some(alloc::vec![
        (String::from(FACE_EXPORTS_H), h),
        (String::from(FACE_EXPORTS_RS), rs),
        (String::from(FACE_INIT_PYI), pyi),
    ])
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
    let Some(faces) = faces_for_fqn(fqn) else {
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
    let Some(faces) = faces_for_fqn(fqn) else {
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
