//! Thin CLI for `pymergetic.util.gen` — not a separate tools tree.
//!
//!   cargo run --features gen --bin wasmmod-gen -- [--check] [path]
//!   cargo run --features build-machinery --bin wasmmod-gen -- …

use std::env;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut check = false;
    let mut path = env!("CARGO_MANIFEST_DIR").to_string();
    for arg in env::args().skip(1) {
        match arg.as_str() {
            "-h" | "--help" => {
                eprintln!(
                    "wasmmod-gen — pymergetic.util.gen (registry introspection)\n\
                     Sinks: host FS (this CLI). VFS/Mem via pm_util_gen_run_vfs / _mem.\n\
                     Usage: wasmmod-gen [--check] [crate_or_src_path]"
                );
                return ExitCode::SUCCESS;
            }
            "--check" => check = true,
            s if s.starts_with('-') => {
                eprintln!("unknown flag: {s}");
                return ExitCode::from(2);
            }
            s => path = s.to_string(),
        }
    }
    // Default: scan src/ + examples/ under crate root when given the crate.
    // Metal cards live in the sibling extmod/metal/src tree (path == module).
    let root = std::path::Path::new(&path);
    let code = if root.join("src/pymergetic").is_dir() {
        let mut roots = Vec::new();
        for sub in ["src", "examples"] {
            let p = root.join(sub);
            if p.exists() {
                roots.push(p);
            }
        }
        let metal = root.join("../metal/src");
        if metal.exists() {
            roots.push(metal);
        }
        let refs: Vec<&std::path::Path> = roots.iter().map(|p| p.as_path()).collect();
        pymergetic_wasmmod::util::r#gen::gen_run_paths(&refs, check)
    } else {
        pymergetic_wasmmod::util::r#gen::gen_run_path(&path, check)
    };
    ExitCode::from(code as u8)
}
