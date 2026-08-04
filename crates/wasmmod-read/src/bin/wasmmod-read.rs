//! CLI: inspect / extract `wasmmod.source` and verify `wasmmod.sig`.

use std::env;
use std::io::{self, Write};
use std::path::PathBuf;
use std::process::{Command, ExitCode};

use wasmmod_read::{
    addr2line, has_dwarf, list_sections, list_symbols, locations_for_symbol, mpy_disasm,
    section_payload, without_sig_section, SigView, SourceView,
};

fn usage() -> ! {
    eprintln!(
        "\
wasmmod-read — standalone wasmmod.source / wasmmod.sig / section reader

Usage:
  wasmmod-read meta PATH.wasm|.aot|.elf
  wasmmod-read list PATH
  wasmmod-read read PATH RELPATH
  wasmmod-read extract PATH -o DIR
  wasmmod-read sig PATH
  wasmmod-read verify --trust ROOT.crt.der PATH
  wasmmod-read sections PATH
  wasmmod-read section PATH INDEX [--hex]
  wasmmod-read symbols PATH
  wasmmod-read addr2line PATH ADDR
  wasmmod-read locations PATH NAME
  wasmmod-read mpy PATH [--limit N]
  wasmmod-read has-dwarf PATH

Build (from wasmmod repo root):
  cargo build --release -p wasmmod-read
"
    );
    std::process::exit(2);
}

fn openssl_ok(args: &[&str]) -> Result<(), String> {
    let status = Command::new("openssl")
        .args(args)
        .status()
        .map_err(|e| format!("openssl: {e}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!("openssl {args:?} failed"))
    }
}

fn cmd_verify(path: &PathBuf, trust: &PathBuf) -> Result<(), String> {
    let data = std::fs::read(path).map_err(|e| e.to_string())?;
    let view = SigView::open_bytes(&data).map_err(|e| e.to_string())?;
    if !view.is_mpws || view.chain.is_empty() {
        return Err("verify: need MPWS chain in wasmmod.sig (sign with --chain)".into());
    }
    let stripped = without_sig_section(&data).map_err(|e| e.to_string())?;
    let dir = tempfile_dir()?;
    let payload = dir.join("payload.bin");
    let sig = dir.join("sig.der");
    let leaf_der = dir.join("leaf.der");
    let leaf_pem = dir.join("leaf.pem");
    let ca_pem = dir.join("ca.pem");
    let pub_pem = dir.join("leaf.pub.pem");
    let untrusted = dir.join("untrusted.pem");
    std::fs::write(&payload, &stripped).map_err(|e| e.to_string())?;
    std::fs::write(&sig, &view.sig).map_err(|e| e.to_string())?;
    let certs = der_certs(&view.chain)?;
    std::fs::write(&leaf_der, certs[0]).map_err(|e| e.to_string())?;
    let trust_bytes = std::fs::read(trust).map_err(|e| e.to_string())?;
    if trust_bytes.first() == Some(&0x30) {
        openssl_ok(&[
            "x509",
            "-inform",
            "DER",
            "-in",
            trust.to_str().unwrap(),
            "-out",
            ca_pem.to_str().unwrap(),
        ])?;
    } else {
        std::fs::write(&ca_pem, trust_bytes).map_err(|e| e.to_string())?;
    }
    openssl_ok(&[
        "x509",
        "-inform",
        "DER",
        "-in",
        leaf_der.to_str().unwrap(),
        "-out",
        leaf_pem.to_str().unwrap(),
    ])?;
    openssl_ok(&[
        "x509",
        "-inform",
        "DER",
        "-in",
        leaf_der.to_str().unwrap(),
        "-pubkey",
        "-noout",
        "-out",
        pub_pem.to_str().unwrap(),
    ])?;
    let mut untrusted_pem = Vec::new();
    for (i, c) in certs.iter().skip(1).enumerate() {
        let mid_der = dir.join(format!("mid{i}.der"));
        let mid_pem = dir.join(format!("mid{i}.pem"));
        std::fs::write(&mid_der, c).map_err(|e| e.to_string())?;
        openssl_ok(&[
            "x509",
            "-inform",
            "DER",
            "-in",
            mid_der.to_str().unwrap(),
            "-out",
            mid_pem.to_str().unwrap(),
        ])?;
        untrusted_pem.extend(std::fs::read(&mid_pem).map_err(|e| e.to_string())?);
    }
    let mut verify_args: Vec<&str> = vec!["verify", "-CAfile", ca_pem.to_str().unwrap()];
    if !untrusted_pem.is_empty() {
        std::fs::write(&untrusted, &untrusted_pem).map_err(|e| e.to_string())?;
        verify_args.extend_from_slice(&["-untrusted", untrusted.to_str().unwrap()]);
    }
    verify_args.push(leaf_pem.to_str().unwrap());
    openssl_ok(&verify_args)?;
    openssl_ok(&[
        "dgst",
        "-sha256",
        "-verify",
        pub_pem.to_str().unwrap(),
        "-signature",
        sig.to_str().unwrap(),
        payload.to_str().unwrap(),
    ])?;
    let _ = std::fs::remove_dir_all(&dir);
    Ok(())
}

fn der_certs(blob: &[u8]) -> Result<Vec<&[u8]>, String> {
    let mut out = Vec::new();
    let mut i = 0;
    while i < blob.len() {
        if blob[i] != 0x30 {
            return Err(format!("bad DER cert at {i}"));
        }
        let (hdr, len) = der_len(&blob[i..])?;
        let end = i + hdr + len;
        if end > blob.len() {
            return Err("truncated DER cert".into());
        }
        out.push(&blob[i..end]);
        i = end;
    }
    if out.is_empty() {
        return Err("empty chain".into());
    }
    Ok(out)
}

fn der_len(blob: &[u8]) -> Result<(usize, usize), String> {
    if blob.len() < 2 {
        return Err("truncated DER".into());
    }
    let b1 = blob[1];
    if b1 & 0x80 == 0 {
        return Ok((2, b1 as usize));
    }
    let n = (b1 & 0x7f) as usize;
    if n == 0 || 2 + n > blob.len() {
        return Err("bad DER length".into());
    }
    let mut len = 0usize;
    for i in 0..n {
        len = (len << 8) | blob[2 + i] as usize;
    }
    Ok((2 + n, len))
}

fn tempfile_dir() -> Result<PathBuf, String> {
    let mut dir = std::env::temp_dir();
    dir.push(format!("wasmmod-read-verify-{}", std::process::id()));
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    Ok(dir)
}

fn main() -> ExitCode {
    let mut args: Vec<String> = env::args().skip(1).collect();
    if args.is_empty() || matches!(args[0].as_str(), "-h" | "--help") {
        usage();
    }
    let cmd = args.remove(0);
    match cmd.as_str() {
        "meta" => {
            let path = args.first().map(PathBuf::from).unwrap_or_else(|| usage());
            match SourceView::open_file(&path) {
                Ok(v) => {
                    println!("name={}", v.name);
                    println!("version={}", v.pkg_version);
                    println!("format={}", v.version);
                    println!("files={}", v.files.len());
                    for t in &v.tags {
                        println!("tag {}={}", t.key, t.value);
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "sig" => {
            let path = args.first().map(PathBuf::from).unwrap_or_else(|| usage());
            match SigView::open_file(&path) {
                Ok(v) => {
                    println!("section=wasmmod.sig");
                    println!("mpws={}", v.is_mpws as u8);
                    println!("sig_len={}", v.sig.len());
                    println!("chain_len={}", v.chain.len());
                    println!("signed_len={}", v.signed_len);
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "verify" => {
            let mut trust: Option<PathBuf> = None;
            let mut path: Option<PathBuf> = None;
            let mut i = 0;
            while i < args.len() {
                if args[i] == "--trust" {
                    i += 1;
                    if i >= args.len() {
                        usage();
                    }
                    trust = Some(PathBuf::from(&args[i]));
                } else if path.is_none() {
                    path = Some(PathBuf::from(&args[i]));
                } else {
                    usage();
                }
                i += 1;
            }
            let Some(path) = path else { usage() };
            let Some(trust) = trust else {
                eprintln!("wasmmod-read: verify needs --trust ROOT.crt.der");
                return ExitCode::from(2);
            };
            match cmd_verify(&path, &trust) {
                Ok(()) => {
                    println!("{}: OK", path.display());
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "list" => {
            let path = args.first().map(PathBuf::from).unwrap_or_else(|| usage());
            match SourceView::open_file(&path) {
                Ok(v) => {
                    for f in &v.files {
                        let z = if f.is_zlib() { 'z' } else { '-' };
                        println!("{z} {:8} {}", f.raw_len, f.path);
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "read" => {
            if args.len() < 2 {
                usage();
            }
            let path = PathBuf::from(&args[0]);
            let rel = &args[1];
            match SourceView::open_file(&path).and_then(|v| v.read(rel)) {
                Ok(bytes) => {
                    let _ = io::stdout().write_all(&bytes);
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "extract" => {
            let mut out: Option<PathBuf> = None;
            let mut wasm: Option<PathBuf> = None;
            let mut i = 0;
            while i < args.len() {
                if args[i] == "-o" || args[i] == "--output" {
                    i += 1;
                    if i >= args.len() {
                        usage();
                    }
                    out = Some(PathBuf::from(&args[i]));
                } else if wasm.is_none() {
                    wasm = Some(PathBuf::from(&args[i]));
                } else {
                    usage();
                }
                i += 1;
            }
            let Some(wasm) = wasm else { usage() };
            let out = out.unwrap_or_else(|| wasmmod_read::default_out_dir(&wasm));
            match SourceView::open_file(&wasm).and_then(|v| {
                let n = v.extract_to(&out)?;
                Ok((n, v))
            }) {
                Ok((n, _)) => {
                    eprintln!("extracted {n} files → {}", out.display());
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "sections" => {
            let path = args.first().map(PathBuf::from).unwrap_or_else(|| usage());
            match std::fs::read(&path)
                .map_err(wasmmod_read::Error::from)
                .and_then(|data| list_sections(&data))
            {
                Ok(secs) => {
                    for s in secs {
                        println!(
                            "{:>3}  {:8}  {:6}  type={:<4}  {}",
                            s.index,
                            s.size,
                            s.role.as_str(),
                            s.type_id,
                            s.name
                        );
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "section" => {
            if args.len() < 2 {
                usage();
            }
            let path = PathBuf::from(&args[0]);
            let index: u32 = match args[1].parse() {
                Ok(v) => v,
                Err(_) => {
                    eprintln!("wasmmod-read: section INDEX must be an integer");
                    return ExitCode::from(2);
                }
            };
            let hex = args.iter().any(|a| a == "--hex");
            match std::fs::read(&path)
                .map_err(wasmmod_read::Error::from)
                .and_then(|data| {
                    let payload = section_payload(&data, index)?.to_vec();
                    Ok(payload)
                }) {
                Ok(bytes) => {
                    if hex {
                        for (i, chunk) in bytes.chunks(16).enumerate() {
                            let off = i * 16;
                            print!("{off:08x}  ");
                            for (j, b) in chunk.iter().enumerate() {
                                if j == 8 {
                                    print!(" ");
                                }
                                print!("{b:02x} ");
                            }
                            for j in chunk.len()..16 {
                                if j == 8 {
                                    print!(" ");
                                }
                                print!("   ");
                            }
                            print!(" |");
                            for b in chunk {
                                let c = if (32..127).contains(b) {
                                    *b as char
                                } else {
                                    '.'
                                };
                                print!("{c}");
                            }
                            println!("|");
                        }
                    } else {
                        let _ = io::stdout().write_all(&bytes);
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "symbols" => {
            let path = args.first().map(PathBuf::from).unwrap_or_else(|| usage());
            match std::fs::read(&path)
                .map_err(wasmmod_read::Error::from)
                .and_then(|data| list_symbols(&data))
            {
                Ok(syms) => {
                    for s in syms {
                        println!(
                            "{:<6} {:<6} +0x{:04x} sz={:<5} {}",
                            s.kind, s.binding, s.offset, s.size, s.name
                        );
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "addr2line" => {
            if args.len() < 2 {
                usage();
            }
            let path = PathBuf::from(&args[0]);
            let addr = match parse_addr(&args[1]) {
                Ok(v) => v,
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    return ExitCode::from(2);
                }
            };
            match std::fs::read(&path)
                .map_err(wasmmod_read::Error::from)
                .and_then(|data| addr2line(&data, addr))
            {
                Ok(locs) => {
                    for loc in locs {
                        let ln = match loc.line {
                            Some(n) => format!(":{n}"),
                            None => String::new(),
                        };
                        println!("{:<6} {}{}", loc.role, loc.path, ln);
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "has-dwarf" => {
            let path = args.first().map(PathBuf::from).unwrap_or_else(|| usage());
            match std::fs::read(&path) {
                Ok(data) => {
                    println!("{}", if has_dwarf(&data) { "yes" } else { "no" });
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "locations" => {
            if args.len() < 2 {
                usage();
            }
            let path = PathBuf::from(&args[0]);
            let name = &args[1];
            match std::fs::read(&path)
                .map_err(wasmmod_read::Error::from)
                .and_then(|data| locations_for_symbol(&data, name))
            {
                Ok(locs) => {
                    for loc in locs {
                        let ln = match loc.line {
                            Some(n) => format!(":{n}"),
                            None => String::new(),
                        };
                        println!("{:<6} {}{}", loc.role, loc.path, ln);
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        "mpy" => {
            let mut path: Option<PathBuf> = None;
            let mut limit = 80usize;
            let mut i = 0;
            while i < args.len() {
                if args[i] == "--limit" {
                    i += 1;
                    if i >= args.len() {
                        usage();
                    }
                    limit = args[i].parse().unwrap_or(80);
                } else if path.is_none() {
                    path = Some(PathBuf::from(&args[i]));
                } else {
                    usage();
                }
                i += 1;
            }
            let Some(path) = path else { usage() };
            match std::fs::read(&path) {
                Ok(data) => {
                    for line in mpy_disasm(&data, limit) {
                        println!("0x{:04x}: {}", line.addr, line.text);
                    }
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("wasmmod-read: {e}");
                    ExitCode::from(1)
                }
            }
        }
        _ => usage(),
    }
}

fn parse_addr(s: &str) -> Result<u64, String> {
    let t = s.trim();
    if let Some(hex) = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")) {
        u64::from_str_radix(hex, 16).map_err(|e| format!("bad ADDR: {e}"))
    } else {
        t.parse::<u64>().map_err(|e| format!("bad ADDR: {e}"))
    }
}
