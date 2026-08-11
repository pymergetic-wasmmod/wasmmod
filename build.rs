//! Three independent native builds glued into this crate's link step:
//!
//! 1. `pymergetic.util.mem`'s C impl (+ vendored TLSF) — `impl = "c"`,
//!    called from Rust today only by `pymergetic.wasmmod.loader`
//!    (reserving the shared-heap backing block), but not otherwise part
//!    of a plain `cargo build`/`cargo test` until something links it.
//! 2. WAMR's `vmlib` (`third_party/wamr`, a nested git repo — see
//!    SOURCETREE.md) — the runtime core the loader calls into.
//!    Interpreter + AOT on: AOT costs nothing at runtime for the
//!    plain-interpreter path and needs no LLVM — LLVM is only used by
//!    `wamrc` (below) to *produce* `.aot` files ahead of time, never by
//!    the runtime that loads/executes them. Fast-jit is NOT enabled:
//!    WAMR's own build system hard-rejects WAMR_BUILD_SHARED_HEAP=1 +
//!    WAMR_BUILD_FAST_JIT=1 together (see
//!    build-scripts/unsupported_combination.cmake) — fast-jit's own
//!    codegen (core/iwasm/fast-jit/fe/jit_emit_memory.c) never learned
//!    the shared-heap address-translation rule that both interpreters
//!    and AOT's LLVM-IR codegen have, so a shared-heap app_addr handed
//!    to fast-jit'd code could compute a wrong native pointer. Real
//!    upstream gap, not fixable from our side without patching vendored
//!    WAMR's own JIT backend (see docs/SOURCETREE.md's decision log for
//!    the full investigation) — shared heap is load-bearing for this
//!    loader, so fast-jit is out for as long as that stays true. Still
//!    no WASI/libc-builtin (this milestone's fixtures import nothing),
//!    shared heap on (the one feature the original loader milestone
//!    exists for).
//! 3. `wamrc` (`third_party/wamr/wamr-compiler`) — the AOT compiler CLI,
//!    built unconditionally against the *system* LLVM (never WAMR's own
//!    from-source LLVM build, which takes hours) so tests can compile
//!    real `.aot` fixtures. This is a real, accepted trade-off: a
//!    machine without an LLVM dev package installed cannot build this
//!    crate at all. See docs/SOURCETREE.md's decision log.
//!
//! All three live in one file only because Cargo allows exactly one
//! `build.rs` per crate — not a sign any of them belongs to another.

fn main() {
    build_util_mem();
    build_wamr();
    build_wamrc();
}

/// Runtime, not `env!("CARGO_MANIFEST_DIR")` — the compile-time macro bakes
/// the path into `build-script-build`, so a directory rename (e.g.
/// metalpython-wasmmod → micropython-wasmmod) leaves a stale `-I` until
/// something touches `build.rs`. Cargo always sets this when *running*
/// the script.
fn manifest_dir() -> String {
    std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR")
}

fn build_util_mem() {
    let root = manifest_dir();
    println!("cargo:rerun-if-changed=src/pymergetic/util/mem/__impl__.c");
    println!("cargo:rerun-if-changed=src/pymergetic/util/mem/__types__.h");
    println!("cargo:rerun-if-changed=src/pymergetic/util/mem/__exports__.h");
    println!("cargo:rerun-if-changed=third_party/tlsf/tlsf.c");
    println!("cargo:rerun-if-changed=third_party/tlsf/tlsf.h");

    cc::Build::new()
        .file("src/pymergetic/util/mem/__impl__.c")
        .file("third_party/tlsf/tlsf.c")
        // `-I.` — this tree's own root-relative include rule (see
        // SOURCETREE.md "-I rule"): every `src/pymergetic/...` include
        // inside `__impl__.c` resolves from here, not from the file's
        // own directory.
        .include(&root)
        .include(format!("{root}/third_party/tlsf"))
        .warnings(true)
        .compile("pm_util_mem");
}

fn build_wamr() {
    let root = manifest_dir();
    let wamr_dir = format!("{root}/third_party/wamr");
    println!("cargo:rerun-if-changed={wamr_dir}/CMakeLists.txt");

    // AOT on (free at runtime, no LLVM involved — see this file's
    // header comment); fast-jit deliberately left off — incompatible
    // with WAMR_BUILD_SHARED_HEAP=1 in this WAMR version, see header
    // comment. Still no WASI/libc-builtin (this milestone's fixtures
    // import nothing), shared heap on (the original loader milestone's
    // one reason this build exists). See the manual `cmake`-configure/
    // build proof run during design (recorded in SOURCETREE.md's
    // decision log) for why each remaining flag is set this way rather
    // than left at upstream's default.
    // Own `out_dir`, not the default (bare `$OUT_DIR`) — `build_wamrc`
    // below configures a second, entirely different CMake project
    // (`wamr-compiler`, not `third_party/wamr`) via its own
    // `cmake::Config`. The `cmake` crate derives its build tree as
    // `{out_dir}/build` and *clears* that directory on reconfigure
    // (`Config::maybe_clear`) — without distinct `out_dir`s, both
    // configs collide on the same `$OUT_DIR/build`, and whichever
    // configures second wipes out the first one's `libiwasm.a`
    // (discovered the hard way: a real link failure, not a hypothetical).
    let out_dir = std::env::var("OUT_DIR").expect("OUT_DIR set by cargo for every build script");
    let dst = cmake::Config::new(&wamr_dir)
        .out_dir(format!("{out_dir}/vmlib"))
        .define("WAMR_BUILD_INTERP", "1")
        .define("WAMR_BUILD_FAST_INTERP", "1")
        .define("WAMR_BUILD_AOT", "1")
        .define("WAMR_BUILD_JIT", "0")
        .define("WAMR_BUILD_FAST_JIT", "0")
        .define("WAMR_BUILD_LIBC_BUILTIN", "0")
        .define("WAMR_BUILD_LIBC_WASI", "0")
        .define("WAMR_BUILD_SHARED_HEAP", "1")
        .define("WAMR_BUILD_SIMD", "0")
        .define("WAMR_BUILD_REF_TYPES", "0")
        .define("WAMR_BUILD_MULTI_MODULE", "0")
        .define("WAMR_BUILD_LIB_PTHREAD", "0")
        .define("WAMR_BUILD_LIB_WASI_THREADS", "0")
        .define("CMAKE_BUILD_TYPE", "Release")
        // `vmlib` alone; skips WAMR's own `install(...)` (headers we
        // already vendor/include directly, see loader's WAMR extern
        // block) — no separate install root to relocate out of.
        .build_target("vmlib")
        .build();

    // `build_target("vmlib")` builds in-place; the `cmake` crate still
    // reports `dst` as the configured build tree root either way.
    println!("cargo:rustc-link-search=native={}/build", dst.display());
    println!("cargo:rustc-link-lib=static=iwasm");
    println!("cargo:rustc-link-lib=dylib=pthread");
    println!("cargo:rustc-link-lib=dylib=dl");
    println!("cargo:rustc-link-lib=dylib=m");
}

/// Finds a working `llvm-config` to point `wamrc`'s build at the
/// *system* LLVM (never WAMR's own from-source LLVM build). Tries an
/// explicit override first, then the unversioned name, then a range of
/// versioned names (Debian/Ubuntu package naming) — first one that
/// actually runs wins.
fn find_llvm_config() -> String {
    if let Ok(path) = std::env::var("LLVM_CONFIG_PATH") {
        return path;
    }
    let candidates = [
        "llvm-config",
        "llvm-config-20",
        "llvm-config-19",
        "llvm-config-18",
        "llvm-config-17",
        "llvm-config-16",
        "llvm-config-15",
        "llvm-config-14",
    ];
    for candidate in candidates {
        if std::process::Command::new(candidate).arg("--version").output().is_ok_and(|o| o.status.success()) {
            return candidate.to_string();
        }
    }
    panic!(
        "wamrc (the AOT compiler) needs a system LLVM to build against \
         (WAMR_BUILD_WITH_CUSTOM_LLVM) — this crate builds it unconditionally, \
         see build.rs's header comment. None of llvm-config{{,-14..-20}} were \
         found on PATH. Install an llvm-*-dev package (e.g. `apt install \
         llvm-18-dev`) or set LLVM_CONFIG_PATH to a working llvm-config binary."
    );
}

fn llvm_cmake_dir(llvm_config: &str) -> String {
    let output = std::process::Command::new(llvm_config)
        .arg("--cmakedir")
        .output()
        .unwrap_or_else(|e| panic!("failed to run `{llvm_config} --cmakedir`: {e}"));
    if !output.status.success() {
        panic!(
            "`{llvm_config} --cmakedir` exited with {}: {}",
            output.status,
            String::from_utf8_lossy(&output.stderr)
        );
    }
    String::from_utf8(output.stdout).expect("llvm-config --cmakedir printed non-UTF8 output").trim().to_string()
}

fn find_file_named(dir: &std::path::Path, name: &str) -> Option<std::path::PathBuf> {
    for entry in std::fs::read_dir(dir).ok()?.flatten() {
        let path = entry.path();
        if path.is_dir() {
            if let Some(found) = find_file_named(&path, name) {
                return Some(found);
            }
        } else if path.file_name().and_then(|n| n.to_str()) == Some(name) {
            return Some(path);
        }
    }
    None
}

fn build_wamrc() {
    let root = manifest_dir();
    let wamrc_dir = format!("{root}/third_party/wamr/wamr-compiler");
    println!("cargo:rerun-if-changed={wamrc_dir}/CMakeLists.txt");

    let llvm_config = find_llvm_config();
    let llvm_dir = llvm_cmake_dir(&llvm_config);

    // See build_wamr()'s comment on why this needs its own `out_dir`,
    // distinct from vmlib's.
    let out_dir = std::env::var("OUT_DIR").expect("OUT_DIR set by cargo for every build script");
    let dst = cmake::Config::new(&wamrc_dir)
        .out_dir(format!("{out_dir}/wamrc"))
        .define("WAMR_BUILD_WITH_CUSTOM_LLVM", "1")
        .define("LLVM_DIR", &llvm_dir)
        .define("CMAKE_BUILD_TYPE", "Release")
        .build_target("wamrc")
        .build();

    let build_dir = dst.join("build");
    let wamrc_path = find_file_named(&build_dir, "wamrc")
        .unwrap_or_else(|| panic!("wamrc binary not found anywhere under {}", build_dir.display()));
    println!("cargo:rustc-env=WAMRC_PATH={}", wamrc_path.display());
}
