//! Thin CLI for `pymergetic.util.gen` — not a separate tools tree.
//!
//!   cargo run --features gen --bin wasmmod-gen -- [--check] [path]
//!   cargo run --features build-machinery --bin wasmmod-gen -- …

use std::env;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut check = false;
    let mut extra: Vec<String> = Vec::new();
    for arg in env::args().skip(1) {
        match arg.as_str() {
            "-h" | "--help" => {
                eprintln!(
                    "wasmmod-gen — pymergetic.util.gen (registry introspection)\n\
                     Sinks: host FS (this CLI). VFS/Mem via pm_util_gen_run_vfs / _mem.\n\
                     Usage: wasmmod-gen [--check] [crate_or_src_path ...]"
                );
                return ExitCode::SUCCESS;
            }
            "--check" => check = true,
            s if s.starts_with('-') => {
                eprintln!("unknown flag: {s}");
                return ExitCode::from(2);
            }
            s => extra.push(s.to_string()),
        }
    }

    // More than one tree: scan them together so one registry pass sees every
    // card. A downstream tree names itself on the command line.
    if extra.len() > 1 {
        let roots: Vec<&std::path::Path> = extra.iter().map(std::path::Path::new).collect();
        return ExitCode::from(pymergetic_wasmmod::util::r#gen::gen_run_paths(&roots, check) as u8);
    }
    let path = extra
        .pop()
        .unwrap_or_else(|| env!("CARGO_MANIFEST_DIR").to_string());
    // Default: scan src/ + examples/ under crate root when given the crate.
    // Any other card tree is a path argument — gen has no list of downstreams.
    let root = std::path::Path::new(&path);
    let code = if root.join("src/pymergetic").is_dir() {
        let mut roots = Vec::new();
        for sub in ["src", "examples"] {
            let p = root.join(sub);
            if p.exists() {
                roots.push(p);
            }
        }
        let refs: Vec<&std::path::Path> = roots.iter().map(|p| p.as_path()).collect();
        pymergetic_wasmmod::util::r#gen::gen_run_paths(&refs, check)
    } else {
        pymergetic_wasmmod::util::r#gen::gen_run_path(&path, check)
    };
    ExitCode::from(code as u8)
}
