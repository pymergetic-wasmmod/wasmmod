//! Host-only discovery when the live registry is empty for a card:
//! - `impl = "py"` → `ast` on `__init__.py` (C/RS access faces from hints)
//! - else if `__impl__.c` has `PM_MOD_EXPORT_C` → scan call sites (guest / unlinked C)
//!
//! Registers into the one registry so [`super::gen_fqn_to_sink`] stays the emit path.

use std::path::Path;
use std::process::Command;

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
    if ensure_c_export_scan(card_dir, fqn) {
        FaceSource::Guest
    } else {
        FaceSource::Empty
    }
}

fn ensure_py_exports(card_dir: &Path, fqn: &str) -> bool {
    let init = card_dir.join("__init__.py");
    if !init.is_file() {
        return false;
    }
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
    let script = r##"
import ast, sys
src = open(sys.argv[1], encoding="utf-8").read()
tree = ast.parse(src)
for node in tree.body:
    if not isinstance(node, ast.FunctionDef) or node.name.startswith("_"):
        continue
    args = []
    ok = True
    for a in node.args.args:
        ann = a.annotation
        if ann is None:
            ok = False
            break
        if isinstance(ann, ast.Name) and ann.id == "bytes":
            args.append("bytes")
        elif isinstance(ann, ast.Name) and ann.id == "int":
            args.append("int")
        else:
            ok = False
            break
    if not ok:
        continue
    ret = node.returns
    if not (isinstance(ret, ast.Name) and ret.id == "int"):
        continue
    if args == []:
        sig = "int32_t(void)"
    elif args == ["bytes"]:
        sig = "int32_t(const uint8_t *, uint32_t)"
    elif args == ["int"]:
        sig = "int32_t(int32_t)"
    else:
        continue
    # name<TAB>sig — no JSON dep in the Rust host
    print(node.name + "\t" + sig)
"##;
    let output = Command::new("python3")
        .arg("-c")
        .arg(script)
        .arg(init_py)
        .output()
        .ok()?;
    if !output.status.success() {
        eprintln!(
            "pm_util_gen: python3 ast failed for {}: {}",
            init_py.display(),
            String::from_utf8_lossy(&output.stderr)
        );
        return None;
    }
    let stdout = String::from_utf8(output.stdout).ok()?;
    let mut out = Vec::new();
    for line in stdout.lines() {
        let mut parts = line.splitn(2, '\t');
        let name = parts.next()?.trim();
        let sig = parts.next()?.trim();
        if name.is_empty() || sig.is_empty() {
            continue;
        }
        out.push((name.to_string(), sig.to_string()));
    }
    if out.is_empty() {
        None
    } else {
        Some(out)
    }
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
    fn c_abi_name_strips_pymergetic() {
        assert_eq!(
            c_abi_name("pymergetic.util.pysample", "hello"),
            "pm_util_pysample_hello"
        );
    }
}
