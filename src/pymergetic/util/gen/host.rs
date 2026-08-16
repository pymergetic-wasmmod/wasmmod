//! Host FS path for `pymergetic.util.gen` (feature = "gen").
//!
//! Card walk discovers write *paths*; faces come from the live registry after
//! `PM_MOD_EXPORT_*` ctors and, when empty, host discovery (py hint scan /
//! `PM_MOD_EXPORT_C` scan) via [`super::discover`].

use std::collections::BTreeMap;
use std::fs;
use std::io::{self, IsTerminal, Write};
use std::path::{Path, PathBuf};

use crate::wasmmod::registry::pm_wasmmod_registry_container_kind_t;

use super::discover::{ensure_card_exports, FaceSource};
use super::{
    apply_faces, face_path, faces_for_fqn, introspect_container, introspect_exports, FACE_INIT_PYI,
    FsSink, GenSink,
};

const SKIP: &[&str] = &[".git", "target", "third_party", ".venv", "node_modules"];

#[derive(Clone, Copy, PartialEq, Eq)]
enum Outcome {
    Wrote,
    Ok,
    Drift,
    Skip,
}

/// Where the module's *code* lives: host (resident) vs guest (pack artifact).
/// Guest containers (`wasm`/`aot`/`elf`) come from card `build = […]`.
#[derive(Clone, Copy, PartialEq, Eq, Default)]
struct GuestKinds {
    wasm: bool,
    aot: bool,
    elf: bool,
}

impl GuestKinds {
    fn any(self) -> bool {
        self.wasm || self.aot || self.elf
    }

    fn from_build_list(items: &[String]) -> Self {
        let mut k = Self::default();
        for s in items {
            match s.as_str() {
                "wasm" => k.wasm = true,
                "aot" => k.aot = true,
                "elf" => k.elf = true,
                _ => {}
            }
        }
        k
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Plane {
    Host,
    Guest(GuestKinds),
}

/// Card `impl =` language (muscle SoT).
#[derive(Clone, Copy, PartialEq, Eq)]
enum ImplLang {
    C,
    Rs,
    Py,
    Unknown,
}

/// Which faces were emitted / present for this card.
#[derive(Clone, Copy, PartialEq, Eq)]
struct Faces {
    h: bool,
    rs: bool,
    pyi: bool,
}

struct Row {
    fqn: String,
    outcome: Outcome,
    impl_lang: ImplLang,
    faces: Faces,
    plane: Plane,
    /// Card `version` when set (publish unit / kernel).
    version: Option<String>,
    nfuncs: u32,
}

struct Leaf {
    outcome: Outcome,
    impl_lang: ImplLang,
    faces: Faces,
    plane: Plane,
    version: Option<String>,
    nfuncs: u32,
}

struct TreeNode {
    leaf: Option<Leaf>,
    kids: BTreeMap<String, TreeNode>,
}

impl TreeNode {
    fn new() -> Self {
        Self {
            leaf: None,
            kids: BTreeMap::new(),
        }
    }

    fn insert(&mut self, row: &Row) {
        let mut cur = self;
        for seg in row.fqn.split('.') {
            cur = cur.kids.entry(seg.to_string()).or_insert_with(TreeNode::new);
        }
        cur.leaf = Some(Leaf {
            outcome: row.outcome,
            impl_lang: row.impl_lang,
            faces: row.faces,
            plane: row.plane,
            version: row.version.clone(),
            nfuncs: row.nfuncs,
        });
    }
}

fn impl_of(s: Option<&str>) -> ImplLang {
    match s {
        Some("c") => ImplLang::C,
        Some("rs") => ImplLang::Rs,
        Some("py") => ImplLang::Py,
        _ => ImplLang::Unknown,
    }
}

fn plane_of(source: FaceSource, fqn: &str, build: GuestKinds) -> Plane {
    match source {
        // Card present but nothing to emit this run (empty umbrella, etc.).
        FaceSource::Empty => {
            if build.any() {
                Plane::Guest(build)
            } else {
                Plane::Host
            }
        }
        // Unlinked C: `build = […]` is a guest pack; no build list is in-bin
        // host muscle the gen binary did not link (Metal, etc.).
        FaceSource::Guest => {
            if build.any() {
                Plane::Guest(build)
            } else {
                Plane::Host
            }
        }
        FaceSource::Py | FaceSource::Live => {
            // Card `build` wins when present (guest pack root).
            if build.any() {
                return Plane::Guest(build);
            }
            match introspect_container(fqn) {
                Some(pm_wasmmod_registry_container_kind_t::Wasm) => Plane::Guest(GuestKinds {
                    wasm: true,
                    ..GuestKinds::default()
                }),
                Some(pm_wasmmod_registry_container_kind_t::Aot) => Plane::Guest(GuestKinds {
                    aot: true,
                    ..GuestKinds::default()
                }),
                Some(pm_wasmmod_registry_container_kind_t::Elf) => Plane::Guest(GuestKinds {
                    elf: true,
                    ..GuestKinds::default()
                }),
                Some(pm_wasmmod_registry_container_kind_t::Resident) | None => Plane::Host,
            }
        }
    }
}

fn color_enabled() -> bool {
    if std::env::var_os("NO_COLOR").is_some() {
        return false;
    }
    io::stdout().is_terminal()
}

fn paint(enabled: bool, code: &str, text: &str) -> String {
    if enabled {
        format!("\x1b[{code}m{text}\x1b[0m")
    } else {
        text.to_string()
    }
}

/// Lang colors: c=yellow, rs=red, py=cyan (same for face tags h/rs/pyi).
fn lang_paint(enabled: bool, lang: &str) -> String {
    let code = match lang {
        "c" | "h" => "1;33",   // bold yellow — C / __exports__.h
        "rs" => "1;31",        // bold red — RS / __exports__.rs
        "py" | "pyi" => "1;36", // bold cyan — Py / __init__.pyi
        _ => "2",
    };
    paint(enabled, code, lang)
}

fn outcome_label(o: Outcome, color: bool) -> String {
    match o {
        Outcome::Wrote => paint(color, "1;32", "wrote"),
        Outcome::Ok => paint(color, "32", "ok"),
        Outcome::Drift => paint(color, "1;31", "drift"),
        Outcome::Skip => paint(color, "33", "skip"),
    }
}

fn plane_label(p: Plane, color: bool) -> String {
    match p {
        Plane::Host => paint(color, "1;34", "host"),
        Plane::Guest(k) => {
            let base = paint(color, "1;35", "guest");
            let mut tags = Vec::new();
            if k.wasm {
                tags.push("wasm");
            }
            if k.aot {
                tags.push("aot");
            }
            if k.elf {
                tags.push("elf");
            }
            if tags.is_empty() {
                base
            } else {
                format!("{base}/{}", paint(color, "35", &tags.join("+")))
            }
        }
    }
}

fn version_at(ver: &Option<String>, color: bool) -> String {
    match ver {
        Some(v) => format!("{}{}", paint(color, "33", "@"), paint(color, "1;37", v)),
        None => String::new(),
    }
}

/// Module name with optional `@version` glued on (no space).
fn module_label(name: &str, ver: &Option<String>, color: bool) -> String {
    format!("{}{}", paint(color, "1;36", name), version_at(ver, color))
}

fn module_label_width(name: &str, ver: Option<&str>) -> usize {
    match ver {
        Some(v) => name.len() + 1 + v.len(),
        None => name.len(),
    }
}

fn impl_label(l: ImplLang, color: bool) -> String {
    match l {
        ImplLang::C => lang_paint(color, "c"),
        ImplLang::Rs => lang_paint(color, "rs"),
        ImplLang::Py => lang_paint(color, "py"),
        ImplLang::Unknown => paint(color, "2", "—"),
    }
}

fn faces_label(f: Faces, _impl_lang: ImplLang, color: bool) -> String {
    if !f.h && !f.rs && !f.pyi {
        return paint(color, "2", "—");
    }
    let mut parts = Vec::new();
    if f.h {
        parts.push(lang_paint(color, "h"));
    }
    if f.rs {
        parts.push(lang_paint(color, "rs"));
    }
    if f.pyi {
        // Only for impl=c/rs (no .py). Never beside a real __init__.py.
        parts.push(lang_paint(color, "pyi"));
    }
    parts.join(" ")
}

/// Emit access faces. `impl=py`: `__init__.py` is the typing SoT — no `__init__.pyi`.
fn gen_card_faces(
    sink: &mut dyn GenSink,
    dir: &str,
    fqn: &str,
    impl_lang: ImplLang,
    check: bool,
) -> i32 {
    let Some(faces) = faces_for_fqn(fqn) else {
        return -1;
    };
    let paths: Vec<(String, String)> = faces
        .into_iter()
        .filter(|(name, _)| !(impl_lang == ImplLang::Py && name == FACE_INIT_PYI))
        .map(|(name, content)| (face_path(dir, &name), content))
        .collect();
    match apply_faces(sink, &paths, check) {
        Ok(true) => 1,
        Ok(false) => 0,
        Err(_) => -1,
    }
}

fn scrub_py_pyi(dir: &Path) {
    let _ = fs::remove_file(dir.join("__init__.pyi"));
}

/// Per-card ignore for generated faces. Identical bytes every run (no git
/// noise). The `.gitignore` itself is tracked; the names listed are not.
/// Recreate faces with `wasmmod-gen` after a clean.
const CARD_GITIGNORE: &str = "\
# pymergetic.util.gen — generated faces. Recreate with wasmmod-gen.
__exports__.h
__exports__.rs
__imports__.h
__imports__.rs
__init__.pyi
__version__.h
!.gitignore
";

fn emit_card_gitignore(dir: &Path, check: bool) -> i32 {
    let path = dir.join(".gitignore");
    let same = fs::read_to_string(&path)
        .ok()
        .is_some_and(|existing| existing == CARD_GITIGNORE);
    if same {
        return 0;
    }
    if check {
        return 1;
    }
    match fs::write(&path, CARD_GITIGNORE) {
        Ok(()) => 0,
        Err(_) => 1,
    }
}

fn status_cols(leaf: &Leaf, color: bool) -> String {
    let n = if leaf.outcome == Outcome::Skip {
        paint(color, "2", "—")
    } else {
        format!("{} {}", leaf.nfuncs, paint(color, "2", "fn"))
    };
    if leaf.outcome == Outcome::Skip {
        // Still show card impl + plane; no faces / fns. Version is on the name.
        return format!(
            "{}  {}  {}  {}  {}",
            outcome_label(leaf.outcome, color),
            impl_label(leaf.impl_lang, color),
            paint(color, "2", "—"),
            plane_label(leaf.plane, color),
            n,
        );
    }
    format!(
        "{}  {} {} {}  {}  {}",
        outcome_label(leaf.outcome, color),
        impl_label(leaf.impl_lang, color),
        paint(color, "2", "→"),
        faces_label(leaf.faces, leaf.impl_lang, color),
        plane_label(leaf.plane, color),
        n,
    )
}

fn print_tree(rows: &[Row]) {
    if rows.is_empty() {
        return;
    }
    let color = color_enabled();
    let mut root = TreeNode::new();
    for r in rows {
        root.insert(r);
    }
    let mut out = io::stdout();
    let _ = writeln!(out);
    if root.leaf.is_none() && root.kids.len() == 1 {
        let (name, node) = root.kids.iter().next().unwrap();
        let ver = node.leaf.as_ref().and_then(|l| l.version.clone());
        let name_s = module_label(name, &ver, color);
        if let Some(leaf) = &node.leaf {
            let _ = writeln!(out, "{name_s}  {}", status_cols(leaf, color));
        } else {
            let _ = writeln!(out, "{name_s}");
        }
        print_level(&mut out, &node.kids, "", color);
    } else {
        print_level(&mut out, &root.kids, "", color);
    }

    let mut n_wrote = 0u32;
    let mut n_ok = 0u32;
    let mut n_drift = 0u32;
    let mut n_skip = 0u32;
    let mut n_host = 0u32;
    let mut n_guest = 0u32;
    let mut n_c = 0u32;
    let mut n_rs = 0u32;
    let mut n_py = 0u32;
    let mut n_funcs = 0u32;
    for r in rows {
        match r.outcome {
            Outcome::Wrote => n_wrote += 1,
            Outcome::Ok => n_ok += 1,
            Outcome::Drift => n_drift += 1,
            Outcome::Skip => n_skip += 1,
        }
        if r.outcome == Outcome::Skip {
            continue;
        }
        match r.plane {
            Plane::Host => n_host += 1,
            Plane::Guest(_) => n_guest += 1,
        }
        match r.impl_lang {
            ImplLang::C => n_c += 1,
            ImplLang::Rs => n_rs += 1,
            ImplLang::Py => n_py += 1,
            ImplLang::Unknown => {}
        }
        n_funcs += r.nfuncs;
    }
    let mut parts = Vec::new();
    if n_wrote > 0 {
        parts.push(format!("{} {}", n_wrote, paint(color, "1;32", "wrote")));
    }
    if n_ok > 0 {
        parts.push(format!("{} {}", n_ok, paint(color, "32", "ok")));
    }
    if n_drift > 0 {
        parts.push(format!("{} {}", n_drift, paint(color, "1;31", "drift")));
    }
    if n_skip > 0 {
        parts.push(format!("{} {}", n_skip, paint(color, "33", "skip")));
    }
    let mut mid = Vec::new();
    if n_host > 0 {
        mid.push(format!("{} {}", n_host, paint(color, "1;34", "host")));
    }
    if n_guest > 0 {
        mid.push(format!("{} {}", n_guest, paint(color, "1;35", "guest")));
    }
    let mut langs = Vec::new();
    if n_c > 0 {
        langs.push(format!("{} {}", n_c, lang_paint(color, "c")));
    }
    if n_rs > 0 {
        langs.push(format!("{} {}", n_rs, lang_paint(color, "rs")));
    }
    if n_py > 0 {
        langs.push(format!("{} {}", n_py, lang_paint(color, "py")));
    }
    let mut chunks = vec![parts.join("  ·  ")];
    if !mid.is_empty() {
        chunks.push(mid.join("  "));
    }
    if !langs.is_empty() {
        chunks.push(langs.join("  "));
    }
    chunks.push(format!(
        "{} {}",
        n_funcs,
        paint(color, "1;37", "fn")
    ));
    let _ = writeln!(out, "\n{}", chunks.join("  ·  "));
}

fn print_level(
    out: &mut impl Write,
    kids: &BTreeMap<String, TreeNode>,
    prefix: &str,
    color: bool,
) {
    let n = kids.len();
    let pad = kids
        .iter()
        .map(|(name, node)| {
            let ver = node.leaf.as_ref().and_then(|l| l.version.as_deref());
            module_label_width(name, ver)
        })
        .max()
        .unwrap_or(0);
    for (i, (name, node)) in kids.iter().enumerate() {
        let last = i + 1 == n;
        let branch = if last { "└── " } else { "├── " };
        let cont = if last { "    " } else { "│   " };
        let ver = node.leaf.as_ref().and_then(|l| l.version.clone());
        let name_s = module_label(name, &ver, color);
        if let Some(leaf) = &node.leaf {
            let gap = " ".repeat(
                pad.saturating_sub(module_label_width(name, ver.as_deref())) + 2,
            );
            let _ = writeln!(
                out,
                "{prefix}{branch}{name_s}{gap}{}",
                status_cols(leaf, color)
            );
        } else {
            let _ = writeln!(out, "{prefix}{branch}{name_s}");
        }
        if !node.kids.is_empty() {
            print_level(out, &node.kids, &format!("{prefix}{cont}"), color);
        }
    }
}

fn find_cards(root: &Path) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let mut stack = vec![root.to_path_buf()];
    while let Some(dir) = stack.pop() {
        let Ok(rd) = fs::read_dir(&dir) else {
            continue;
        };
        for ent in rd.flatten() {
            let p = ent.path();
            let name = ent.file_name().to_string_lossy().to_string();
            if p.is_dir() {
                if SKIP.contains(&name.as_str()) {
                    continue;
                }
                stack.push(p);
            } else if name == "__pmm__.toml" {
                out.push(p);
            }
        }
    }
    out.sort();
    out
}

fn load_fqn(card: &Path) -> Option<String> {
    let text = fs::read_to_string(card).ok()?;
    let value: toml::Value = text.parse().ok()?;
    value.get("fqn")?.as_str().map(|s| s.to_string())
}

fn load_version(card: &Path) -> Option<String> {
    let text = fs::read_to_string(card).ok()?;
    let value: toml::Value = text.parse().ok()?;
    value
        .get("version")
        .and_then(|v| v.as_str())
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
}

fn load_build(card: &Path) -> GuestKinds {
    let Ok(text) = fs::read_to_string(card) else {
        return GuestKinds::default();
    };
    let Ok(value) = text.parse::<toml::Value>() else {
        return GuestKinds::default();
    };
    let Some(arr) = value.get("build").and_then(|v| v.as_array()) else {
        return GuestKinds::default();
    };
    let items: Vec<String> = arr
        .iter()
        .filter_map(|v| v.as_str().map(|s| s.to_string()))
        .collect();
    GuestKinds::from_build_list(&items)
}

/// `pymergetic.wasmmod` → `PYMERGETIC_WASMMOD_VERSION`
fn version_macro_name(fqn: &str) -> String {
    let mid = fqn
        .split('.')
        .map(|p| p.to_ascii_uppercase())
        .collect::<Vec<_>>()
        .join("_");
    format!("{mid}_VERSION")
}

fn emit_version_h(dir: &Path, fqn: &str, version: &str, check: bool) -> i32 {
    let mid = fqn
        .split('.')
        .map(|p| p.to_ascii_uppercase())
        .collect::<Vec<_>>()
        .join("_");
    let guard = format!("{mid}_VERHDR_H");
    let macro_n = version_macro_name(fqn);
    let body = format!(
        "/* Generated from __pmm__.toml version — do not edit. */\n\
         #ifndef {guard}\n\
         #define {guard}\n\
         #define {macro_n} \"{version}\"\n\
         #endif\n"
    );
    let path = dir.join("__version__.h");
    if check {
        match fs::read_to_string(&path) {
            Ok(existing) if existing == body => 0,
            _ => 1,
        }
    } else {
        match fs::write(&path, body) {
            Ok(()) => 0,
            Err(_) => 1,
        }
    }
}

fn load_impl(card: &Path) -> Option<String> {
    let text = fs::read_to_string(card).ok()?;
    let value: toml::Value = text.parse().ok()?;
    value.get("impl")?.as_str().map(|s| s.to_string())
}

fn is_pep420(card: &Path) -> bool {
    let Ok(text) = fs::read_to_string(card) else {
        return false;
    };
    let Ok(value) = text.parse::<toml::Value>() else {
        return false;
    };
    value
        .get("pep420")
        .and_then(|v| v.as_bool())
        .unwrap_or(false)
}

fn faces_on_disk(dir: &Path) -> Faces {
    Faces {
        h: dir.join("__exports__.h").is_file(),
        rs: dir.join("__exports__.rs").is_file(),
        pyi: dir.join("__init__.pyi").is_file(),
    }
}

/// Scan one or more roots; print one FQN module tree.
pub fn gen_run_paths(roots: &[&Path], check: bool) -> i32 {
    let mut cards = Vec::new();
    for root in roots {
        if !root.exists() {
            eprintln!(
                "{}",
                paint(
                    color_enabled(),
                    "1;31",
                    &format!("missing root: {}", root.display())
                )
            );
            return 1;
        }
        cards.extend(find_cards(root));
    }
    cards.sort();
    cards.dedup();
    if cards.is_empty() {
        println!("{}", paint(color_enabled(), "33", "no cards"));
        return 0;
    }
    let mut sink = FsSink;
    let mut drift = 0i32;
    let mut rows = Vec::new();
    for card in cards {
        let dir = card.parent().unwrap();
        if emit_card_gitignore(dir, check) != 0 {
            drift = 1;
        }
        if is_pep420(&card) {
            continue;
        }
        let Some(fqn) = load_fqn(&card) else {
            continue;
        };
        let dir_s = dir.display().to_string();
        let impl_lang = impl_of(load_impl(&card).as_deref());
        let source = ensure_card_exports(dir, &fqn);
        let build = load_build(&card);
        let plane = plane_of(source, &fqn, build);
        let version = load_version(&card);
        let nfuncs = introspect_exports(&fqn).len() as u32;
        // Card `version` → `__version__.h` (even when face emit skips).
        if let Some(ver) = version.as_ref() {
            if emit_version_h(dir, &fqn, ver, check) != 0 {
                drift = 1;
            }
        }
        // impl=py: __init__.py is typing SoT — never emit/keep a sibling .pyi.
        if impl_lang == ImplLang::Py {
            scrub_py_pyi(dir);
        }
        match gen_card_faces(&mut sink, &dir_s, &fqn, impl_lang, check) {
            0 => {
                let outcome = if check {
                    Outcome::Ok
                } else {
                    Outcome::Wrote
                };
                rows.push(Row {
                    fqn,
                    outcome,
                    impl_lang,
                    faces: faces_on_disk(dir),
                    plane,
                    version,
                    nfuncs,
                });
            }
            1 => {
                if check {
                    rows.push(Row {
                        fqn,
                        outcome: Outcome::Drift,
                        impl_lang,
                        faces: faces_on_disk(dir),
                        plane,
                        version,
                        nfuncs,
                    });
                }
                drift = 1;
            }
            _ => {
                rows.push(Row {
                    fqn,
                    outcome: Outcome::Skip,
                    impl_lang,
                    faces: Faces {
                        h: false,
                        rs: false,
                        pyi: false,
                    },
                    plane,
                    version,
                    nfuncs: 0,
                });
            }
        }
    }
    rows.sort_by(|a, b| a.fqn.cmp(&b.fqn));
    print_tree(&rows);
    drift
}

pub fn gen_run_path(root: &str, check: bool) -> i32 {
    let root = PathBuf::from(root);
    gen_run_paths(&[&root], check)
}

pub fn gen_run(root: &Path, check: bool) -> i32 {
    gen_run_paths(&[root], check)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::r#gen::{introspect_exports, register_fn};
    use std::fs;

    #[test]
    fn gen_writes_faces_from_registry_not_source_scan() {
        let tmp = std::env::temp_dir().join("wasmmod_gen_test_fixture");
        let _ = fs::remove_dir_all(&tmp);
        let mod_dir = tmp.join("src/pymergetic/util/gen_fixture");
        fs::create_dir_all(&mod_dir).unwrap();
        fs::write(
            mod_dir.join("__pmm__.toml"),
            "fqn = \"pymergetic.util.gen_fixture\"\nimpl = \"rs\"\n",
        )
        .unwrap();
        // Live registration — no __impl__ muscle scan.
        assert!(unsafe {
            register_fn(
                "pymergetic.util.gen_fixture",
                "ping",
                0x7 as *mut _,
                "int(void)",
            )
        });
        assert!(!introspect_exports("pymergetic.util.gen_fixture").is_empty());
        assert_eq!(gen_run_path(&tmp.join("src").display().to_string(), false), 0);
        let h = fs::read_to_string(mod_dir.join("__exports__.h")).unwrap();
        assert!(h.contains("int ping(void);"));
        assert!(h.contains("live registry introspection"));
        let gi = fs::read_to_string(mod_dir.join(".gitignore")).unwrap();
        assert_eq!(gi, CARD_GITIGNORE);
        assert!(gi.contains("__exports__.h"));
        assert!(!gi.lines().any(|l| l == "__init__.py" || l == "__pmm__.toml"));
        assert_eq!(gen_run_path(&tmp.join("src").display().to_string(), true), 0);
        let _ = fs::remove_dir_all(&tmp);
    }
}
