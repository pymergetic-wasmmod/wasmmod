//! Host-side reader for `wasmmod.source` (and helpers for other custom sections).
//!
//! No MicroPython / WAMR dependency — safe to build from a solo wasmmod checkout.

use std::path::{Path, PathBuf};

use flate2::read::ZlibDecoder;
use std::io::Read;
use thiserror::Error;

pub const SOURCE_SECTION: &str = "wasmmod.source";
pub const SOURCE_MAGIC: &[u8; 4] = b"MPSR";
pub const SOURCE_VERSION: u16 = 1;
pub const FILE_FLAG_ZLIB: u8 = 1 << 0;

pub const SIG_SECTION: &str = "wasmmod.sig";
pub const MPWS_MAGIC: &[u8; 4] = b"MPWS";
pub const MPWS_VER: u8 = 1;

#[derive(Debug, Error)]
pub enum Error {
    #[error("not a Wasm or AOT module")]
    NotModule,
    #[error("truncated Wasm / AOT / section")]
    Truncated,
    #[error("missing custom section {0}")]
    MissingSection(String),
    #[error("bad wasmmod.source magic or version")]
    BadSource,
    #[error("bad wasmmod.sig / MPWS")]
    BadSig,
    #[error("path not found: {0}")]
    PathNotFound(String),
    #[error("inflate failed for {0}")]
    Inflate(String),
    #[error(transparent)]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, Clone)]
pub struct SourceTag {
    pub key: String,
    pub value: String,
}

#[derive(Debug, Clone)]
pub struct SourceFile {
    pub path: String,
    pub flags: u8,
    pub raw_len: u32,
    /// On-wire bytes (possibly zlib).
    pub data: Vec<u8>,
}

impl SourceFile {
    pub fn is_zlib(&self) -> bool {
        self.flags & FILE_FLAG_ZLIB != 0
    }

    pub fn bytes(&self) -> Result<Vec<u8>> {
        if !self.is_zlib() {
            return Ok(self.data.clone());
        }
        let mut dec = ZlibDecoder::new(self.data.as_slice());
        let mut out = Vec::with_capacity(self.raw_len as usize);
        dec.read_to_end(&mut out).map_err(|_| Error::Inflate(self.path.clone()))?;
        if out.len() != self.raw_len as usize {
            return Err(Error::Inflate(self.path.clone()));
        }
        Ok(out)
    }
}

#[derive(Debug, Clone)]
pub struct SourceView {
    pub version: u16,
    pub flags: u16,
    pub name: String,
    pub pkg_version: String,
    pub tags: Vec<SourceTag>,
    pub files: Vec<SourceFile>,
}

impl SourceView {
    pub fn open_bytes(wasm: &[u8]) -> Result<Self> {
        let payload = extract_custom_section(wasm, SOURCE_SECTION)?
            .ok_or_else(|| Error::MissingSection(SOURCE_SECTION.into()))?;
        parse_source_payload(payload)
    }

    pub fn open_file(path: impl AsRef<Path>) -> Result<Self> {
        let wasm = std::fs::read(path)?;
        Self::open_bytes(&wasm)
    }

    pub fn file(&self, path: &str) -> Option<&SourceFile> {
        self.files.iter().find(|f| f.path == path)
    }

    pub fn read(&self, path: &str) -> Result<Vec<u8>> {
        self.file(path)
            .ok_or_else(|| Error::PathNotFound(path.into()))?
            .bytes()
    }

    pub fn extract_to(&self, out_dir: impl AsRef<Path>) -> Result<usize> {
        let root = out_dir.as_ref();
        for f in &self.files {
            let dest = root.join(&f.path);
            if let Some(parent) = dest.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::write(&dest, f.bytes()?)?;
        }
        Ok(self.files.len())
    }
}

fn read_u16(buf: &[u8], i: &mut usize) -> Result<u16> {
    if *i + 2 > buf.len() {
        return Err(Error::Truncated);
    }
    let v = u16::from_le_bytes([buf[*i], buf[*i + 1]]);
    *i += 2;
    Ok(v)
}

fn read_u32(buf: &[u8], i: &mut usize) -> Result<u32> {
    if *i + 4 > buf.len() {
        return Err(Error::Truncated);
    }
    let v = u32::from_le_bytes([buf[*i], buf[*i + 1], buf[*i + 2], buf[*i + 3]]);
    *i += 4;
    Ok(v)
}

fn read_bytes<'a>(buf: &'a [u8], i: &mut usize, n: usize) -> Result<&'a [u8]> {
    if *i + n > buf.len() {
        return Err(Error::Truncated);
    }
    let s = &buf[*i..*i + n];
    *i += n;
    Ok(s)
}

fn read_uleb(buf: &[u8], i: &mut usize) -> Result<u32> {
    let mut result: u32 = 0;
    let mut shift = 0u32;
    loop {
        if *i >= buf.len() {
            return Err(Error::Truncated);
        }
        let b = buf[*i];
        *i += 1;
        result |= u32::from(b & 0x7f) << shift;
        if b & 0x80 == 0 {
            return Ok(result);
        }
        shift += 7;
        if shift > 28 {
            return Err(Error::Truncated);
        }
    }
}

/// Extract payload of a named custom section from `.wasm`, `.aot`, or `.elf`.
pub fn extract_custom_section<'a>(buf: &'a [u8], name: &str) -> Result<Option<&'a [u8]>> {
    if buf.len() >= 4 && &buf[0..4] == b"\0asm" {
        return extract_wasm_custom_section(buf, name);
    }
    if buf.len() >= 4 && &buf[0..4] == b"\0aot" {
        return extract_aot_custom_section(buf, name);
    }
    if buf.len() >= 4 && &buf[0..4] == b"\x7fELF" {
        return extract_elf_section(buf, name);
    }
    Err(Error::NotModule)
}

/// Role hint for UI / CLI (code vs wasmmod metadata vs other).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SectionRole {
    Code,
    Meta,
    Other,
}

impl SectionRole {
    pub fn as_str(self) -> &'static str {
        match self {
            SectionRole::Code => "code",
            SectionRole::Meta => "meta",
            SectionRole::Other => "other",
        }
    }
}

/// One container section in a naked `.wasm` / `.aot` / `.elf`.
#[derive(Debug, Clone)]
pub struct ContainerSection {
    pub index: u32,
    pub name: String,
    pub type_id: u32,
    pub offset: u64,
    pub size: u64,
    pub role: SectionRole,
}

fn section_role_meta(name: &str) -> bool {
    let bare = name.trim_start_matches('.');
    bare.starts_with("wasmmod.")
}

fn wasm_section_name(id: u8) -> &'static str {
    match id {
        0 => "custom",
        1 => "type",
        2 => "import",
        3 => "function",
        4 => "table",
        5 => "memory",
        6 => "global",
        7 => "export",
        8 => "start",
        9 => "element",
        10 => "code",
        11 => "data",
        12 => "datacount",
        _ => "section",
    }
}

fn aot_section_name(typ: u32) -> &'static str {
    match typ {
        0 => "target_info",
        1 => "init_data",
        2 => "text",
        3 => "function",
        4 => "export",
        5 => "relocation",
        6 => "signature",
        AOT_SECTION_TYPE_CUSTOM => "custom",
        _ => "section",
    }
}

fn list_sections_wasm(wasm: &[u8]) -> Result<Vec<ContainerSection>> {
    if wasm.len() < 8 {
        return Err(Error::Truncated);
    }
    let mut out = Vec::new();
    let mut i = 8usize;
    while i < wasm.len() {
        let id = wasm[i];
        i += 1;
        let size = read_uleb(wasm, &mut i)? as usize;
        let sec_off = i;
        let sec_end = i + size;
        if sec_end > wasm.len() {
            return Err(Error::Truncated);
        }
        let mut name = wasm_section_name(id).to_string();
        if id == 0 && size > 0 {
            let mut j = sec_off;
            if let Ok(nlen) = read_uleb(wasm, &mut j) {
                let nlen = nlen as usize;
                if j + nlen <= sec_end {
                    name = String::from_utf8_lossy(&wasm[j..j + nlen]).into_owned();
                    if name.is_empty() {
                        name = "custom".into();
                    }
                }
            }
        }
        let role = if id == 10 {
            SectionRole::Code
        } else if section_role_meta(&name) {
            SectionRole::Meta
        } else {
            SectionRole::Other
        };
        out.push(ContainerSection {
            index: out.len() as u32,
            name,
            type_id: u32::from(id),
            offset: sec_off as u64,
            size: size as u64,
            role,
        });
        i = sec_end;
    }
    Ok(out)
}

fn list_sections_aot(aot: &[u8]) -> Result<Vec<ContainerSection>> {
    if aot.len() < 8 {
        return Err(Error::Truncated);
    }
    let mut out = Vec::new();
    let mut p = 8usize;
    while p + 8 <= aot.len() {
        let typ = u32::from_le_bytes(aot[p..p + 4].try_into().unwrap());
        let size = u32::from_le_bytes(aot[p + 4..p + 8].try_into().unwrap()) as usize;
        let content = p + 8;
        let end = content + size;
        if end > aot.len() || size > 0x1000_0000 {
            return Err(Error::Truncated);
        }
        let mut name = aot_section_name(typ).to_string();
        if typ == AOT_SECTION_TYPE_CUSTOM && size >= 6 {
            let sub = u32::from_le_bytes(aot[content..content + 4].try_into().unwrap());
            if sub == AOT_CUSTOM_SECTION_RAW {
                let slen =
                    u16::from_le_bytes(aot[content + 4..content + 6].try_into().unwrap()) as usize;
                let name_off = content + 6;
                if name_off + slen <= end {
                    let mut name_bytes = &aot[name_off..name_off + slen];
                    if name_bytes.ends_with(&[0]) {
                        name_bytes = &name_bytes[..name_bytes.len() - 1];
                    }
                    if !name_bytes.is_empty() {
                        name = String::from_utf8_lossy(name_bytes).into_owned();
                    }
                }
            }
        }
        let role = if typ == 2 {
            SectionRole::Code
        } else if section_role_meta(&name) {
            SectionRole::Meta
        } else {
            SectionRole::Other
        };
        out.push(ContainerSection {
            index: out.len() as u32,
            name,
            type_id: typ,
            offset: content as u64,
            size: size as u64,
            role,
        });
        let aligned = (end + 3) & !3;
        p = if aligned <= aot.len() { aligned } else { end };
    }
    Ok(out)
}

fn list_sections_elf(elf: &[u8]) -> Result<Vec<ContainerSection>> {
    if elf.len() < 64 || elf[4] != 2 || elf[5] != 1 {
        return Err(Error::Truncated);
    }
    let shoff = u64::from_le_bytes(elf[40..48].try_into().unwrap()) as usize;
    let shentsize = u16::from_le_bytes(elf[58..60].try_into().unwrap()) as usize;
    let shnum = u16::from_le_bytes(elf[60..62].try_into().unwrap()) as usize;
    let shstrndx = u16::from_le_bytes(elf[62..64].try_into().unwrap()) as usize;
    if shentsize < 64 || shnum == 0 || shstrndx >= shnum {
        return Err(Error::Truncated);
    }
    if shoff + shnum * shentsize > elf.len() {
        return Err(Error::Truncated);
    }
    let shstr = &elf[shoff + shstrndx * shentsize..];
    let str_off = u64::from_le_bytes(shstr[24..32].try_into().unwrap()) as usize;
    let str_sz = u64::from_le_bytes(shstr[32..40].try_into().unwrap()) as usize;
    if str_off + str_sz > elf.len() {
        return Err(Error::Truncated);
    }
    let strtab = &elf[str_off..str_off + str_sz];
    let mut out = Vec::new();
    for i in 0..shnum {
        let sh = &elf[shoff + i * shentsize..];
        let name_off = u32::from_le_bytes(sh[0..4].try_into().unwrap()) as usize;
        let typ = u32::from_le_bytes(sh[4..8].try_into().unwrap());
        let off = u64::from_le_bytes(sh[24..32].try_into().unwrap()) as usize;
        let sz = u64::from_le_bytes(sh[32..40].try_into().unwrap()) as usize;
        if sz == 0 {
            continue;
        }
        if off + sz > elf.len() {
            continue;
        }
        let name = if name_off >= strtab.len() {
            format!("shdr_{i}")
        } else {
            let end = strtab[name_off..]
                .iter()
                .position(|&b| b == 0)
                .map(|p| name_off + p)
                .unwrap_or(strtab.len());
            let s = String::from_utf8_lossy(&strtab[name_off..end]).into_owned();
            if s.is_empty() {
                format!("shdr_{i}")
            } else {
                s
            }
        };
        let role = if name == ".text" || name.starts_with(".text.") {
            SectionRole::Code
        } else if section_role_meta(&name) {
            SectionRole::Meta
        } else {
            SectionRole::Other
        };
        out.push(ContainerSection {
            index: out.len() as u32,
            name,
            type_id: typ,
            offset: off as u64,
            size: sz as u64,
            role,
        });
    }
    Ok(out)
}

/// List all container sections (Wasm / AOT / ELF).
pub fn list_sections(buf: &[u8]) -> Result<Vec<ContainerSection>> {
    if buf.len() >= 4 && &buf[0..4] == b"\0asm" {
        return list_sections_wasm(buf);
    }
    if buf.len() >= 4 && &buf[0..4] == b"\0aot" {
        return list_sections_aot(buf);
    }
    if buf.len() >= 4 && &buf[0..4] == b"\x7fELF" {
        return list_sections_elf(buf);
    }
    Err(Error::NotModule)
}

/// Payload bytes for section ``index`` from [`list_sections`].
pub fn section_payload(buf: &[u8], index: u32) -> Result<&[u8]> {
    let sections = list_sections(buf)?;
    let Some(sec) = sections.iter().find(|s| s.index == index) else {
        return Err(Error::PathNotFound(format!("section index {index}")));
    };
    let start = sec.offset as usize;
    let end = start + sec.size as usize;
    if end > buf.len() {
        return Err(Error::Truncated);
    }
    Ok(&buf[start..end])
}

fn extract_elf_section<'a>(elf: &'a [u8], name: &str) -> Result<Option<&'a [u8]>> {
    if elf.len() < 64 || elf[4] != 2 || elf[5] != 1 {
        return Err(Error::Truncated);
    }
    let shoff = u64::from_le_bytes(elf[40..48].try_into().unwrap()) as usize;
    let shentsize = u16::from_le_bytes(elf[58..60].try_into().unwrap()) as usize;
    let shnum = u16::from_le_bytes(elf[60..62].try_into().unwrap()) as usize;
    let shstrndx = u16::from_le_bytes(elf[62..64].try_into().unwrap()) as usize;
    if shentsize < 64 || shnum == 0 || shstrndx >= shnum {
        return Err(Error::Truncated);
    }
    if shoff + shnum * shentsize > elf.len() {
        return Err(Error::Truncated);
    }
    let shstr = &elf[shoff + shstrndx * shentsize..];
    let str_off = u64::from_le_bytes(shstr[24..32].try_into().unwrap()) as usize;
    let str_sz = u64::from_le_bytes(shstr[32..40].try_into().unwrap()) as usize;
    if str_off + str_sz > elf.len() {
        return Err(Error::Truncated);
    }
    let strtab = &elf[str_off..str_off + str_sz];
    let want = name.as_bytes();
    let want_dot = if name.starts_with('.') {
        name.as_bytes().to_vec()
    } else {
        format!(".{name}").into_bytes()
    };
    for i in 0..shnum {
        let sh = &elf[shoff + i * shentsize..];
        let name_off = u32::from_le_bytes(sh[0..4].try_into().unwrap()) as usize;
        let typ = u32::from_le_bytes(sh[4..8].try_into().unwrap());
        if typ != 1 && typ != 7 {
            continue; // PROGBITS / NOTE
        }
        if name_off >= strtab.len() {
            continue;
        }
        let end = strtab[name_off..]
            .iter()
            .position(|&b| b == 0)
            .map(|p| name_off + p)
            .unwrap_or(strtab.len());
        let sname = &strtab[name_off..end];
        if sname != want && sname != want_dot.as_slice() {
            continue;
        }
        let off = u64::from_le_bytes(sh[24..32].try_into().unwrap()) as usize;
        let sz = u64::from_le_bytes(sh[32..40].try_into().unwrap()) as usize;
        if off + sz > elf.len() {
            return Err(Error::Truncated);
        }
        return Ok(Some(&elf[off..off + sz]));
    }
    Ok(None)
}

fn extract_wasm_custom_section<'a>(wasm: &'a [u8], name: &str) -> Result<Option<&'a [u8]>> {
    if wasm.len() < 8 {
        return Err(Error::Truncated);
    }
    let want = name.as_bytes();
    let mut i = 8usize;
    while i < wasm.len() {
        let id = wasm[i];
        i += 1;
        let size = read_uleb(wasm, &mut i)? as usize;
        let sec_end = i + size;
        if sec_end > wasm.len() {
            return Err(Error::Truncated);
        }
        if id == 0 {
            let mut j = i;
            let nlen = read_uleb(wasm, &mut j)? as usize;
            if j + nlen <= sec_end && &wasm[j..j + nlen] == want {
                return Ok(Some(&wasm[j + nlen..sec_end]));
            }
        }
        i = sec_end;
    }
    Ok(None)
}

const AOT_SECTION_TYPE_CUSTOM: u32 = 100;
const AOT_CUSTOM_SECTION_RAW: u32 = 0;

fn extract_aot_custom_section<'a>(aot: &'a [u8], name: &str) -> Result<Option<&'a [u8]>> {
    if aot.len() < 8 {
        return Err(Error::Truncated);
    }
    let want = name.as_bytes();
    let mut p = 8usize;
    while p + 8 <= aot.len() {
        let typ = u32::from_le_bytes(aot[p..p + 4].try_into().unwrap());
        let size = u32::from_le_bytes(aot[p + 4..p + 8].try_into().unwrap()) as usize;
        let content = p + 8;
        let end = content + size;
        if end > aot.len() || size > 0x1000_0000 {
            return Err(Error::Truncated);
        }
        if typ == AOT_SECTION_TYPE_CUSTOM && size >= 6 {
            let sub = u32::from_le_bytes(aot[content..content + 4].try_into().unwrap());
            if sub == AOT_CUSTOM_SECTION_RAW {
                let slen = u16::from_le_bytes(aot[content + 4..content + 6].try_into().unwrap()) as usize;
                let name_off = content + 6;
                if name_off + slen <= end {
                    let name_bytes = &aot[name_off..name_off + slen];
                    let bare = name_bytes.strip_suffix(&[0]).unwrap_or(name_bytes);
                    if bare == want {
                        return Ok(Some(&aot[name_off + slen..end]));
                    }
                }
            }
        }
        p = (end + 3) & !3;
    }
    Ok(None)
}

pub fn parse_source_payload(payload: &[u8]) -> Result<SourceView> {
    if payload.len() < 12 || &payload[0..4] != SOURCE_MAGIC {
        return Err(Error::BadSource);
    }
    let mut i = 4usize;
    let version = read_u16(payload, &mut i)?;
    let flags = read_u16(payload, &mut i)?;
    if version != SOURCE_VERSION {
        return Err(Error::BadSource);
    }
    let name_len = read_u16(payload, &mut i)? as usize;
    let name = String::from_utf8_lossy(read_bytes(payload, &mut i, name_len)?).into_owned();
    let ver_len = read_u16(payload, &mut i)? as usize;
    let pkg_version = String::from_utf8_lossy(read_bytes(payload, &mut i, ver_len)?).into_owned();
    let n_tags = read_u16(payload, &mut i)? as usize;
    let mut tags = Vec::with_capacity(n_tags);
    for _ in 0..n_tags {
        let kl = read_u16(payload, &mut i)? as usize;
        let key = String::from_utf8_lossy(read_bytes(payload, &mut i, kl)?).into_owned();
        let vl = read_u16(payload, &mut i)? as usize;
        let value = String::from_utf8_lossy(read_bytes(payload, &mut i, vl)?).into_owned();
        tags.push(SourceTag { key, value });
    }
    let n_files = read_u32(payload, &mut i)? as usize;
    let mut files = Vec::with_capacity(n_files);
    for _ in 0..n_files {
        let pl = read_u16(payload, &mut i)? as usize;
        let path = String::from_utf8_lossy(read_bytes(payload, &mut i, pl)?).into_owned();
        if i >= payload.len() {
            return Err(Error::Truncated);
        }
        let fflags = payload[i];
        i += 1;
        let raw_len = read_u32(payload, &mut i)?;
        let data_len = read_u32(payload, &mut i)? as usize;
        let data = read_bytes(payload, &mut i, data_len)?.to_vec();
        files.push(SourceFile {
            path,
            flags: fflags,
            raw_len,
            data,
        });
    }
    Ok(SourceView {
        version,
        flags,
        name,
        pkg_version,
        tags,
        files,
    })
}

/// Resolve a path for CLI help / tests.
pub fn default_out_dir(wasm: &Path) -> PathBuf {
    wasm.with_extension("src")
}

#[derive(Debug, Clone)]
pub struct SigView {
    pub is_mpws: bool,
    pub sig: Vec<u8>,
    pub chain: Vec<u8>,
    /// Length of artifact bytes covered by the signature (without wasmmod.sig).
    pub signed_len: usize,
}

impl SigView {
    pub fn open_file(path: impl AsRef<Path>) -> Result<Self> {
        let data = std::fs::read(path)?;
        Self::open_bytes(&data)
    }

    pub fn open_bytes(buf: &[u8]) -> Result<Self> {
        let payload = extract_custom_section(buf, SIG_SECTION)?
            .ok_or_else(|| Error::MissingSection(SIG_SECTION.into()))?;
        let (is_mpws, sig, chain) = parse_mpws(payload)?;
        let stripped = without_sig_section(buf)?;
        Ok(Self {
            is_mpws,
            sig,
            chain,
            signed_len: stripped.len(),
        })
    }
}

pub fn parse_mpws(payload: &[u8]) -> Result<(bool, Vec<u8>, Vec<u8>)> {
    if payload.len() >= 8 && &payload[0..4] == MPWS_MAGIC && payload[4] == MPWS_VER {
        let sl = u16::from_be_bytes([payload[6], payload[7]]) as usize;
        if 8 + sl > payload.len() || sl == 0 {
            return Err(Error::BadSig);
        }
        let sig = payload[8..8 + sl].to_vec();
        let rest = &payload[8 + sl..];
        let chain = if rest.len() >= 2 {
            let cl = u16::from_be_bytes([rest[0], rest[1]]) as usize;
            if 2 + cl > rest.len() {
                return Err(Error::BadSig);
            }
            rest[2..2 + cl].to_vec()
        } else {
            Vec::new()
        };
        return Ok((true, sig, chain));
    }
    if payload.is_empty() {
        return Err(Error::BadSig);
    }
    Ok((false, payload.to_vec(), Vec::new()))
}

/// Artifact bytes ECDSA covers for an embedded wasmmod.sig.
pub fn without_sig_section(buf: &[u8]) -> Result<Vec<u8>> {
    if buf.len() < 8 {
        return Err(Error::NotModule);
    }
    let want = SIG_SECTION.as_bytes();
    if &buf[0..4] == b"\0asm" {
        let mut out = Vec::with_capacity(buf.len());
        out.extend_from_slice(&buf[..8]);
        let mut i = 8usize;
        while i < buf.len() {
            let sec_start = i;
            let id = buf[i];
            i += 1;
            let size = read_uleb(buf, &mut i)? as usize;
            let sec_end = i + size;
            if sec_end > buf.len() {
                return Err(Error::Truncated);
            }
            let mut skip = false;
            if id == 0 {
                let mut j = i;
                let nlen = read_uleb(buf, &mut j)? as usize;
                if j + nlen <= sec_end && &buf[j..j + nlen] == want {
                    skip = true;
                }
            }
            if !skip {
                out.extend_from_slice(&buf[sec_start..sec_end]);
            }
            i = sec_end;
        }
        return Ok(out);
    }
    if &buf[0..4] == b"\0aot" {
        let mut out = Vec::with_capacity(buf.len());
        out.extend_from_slice(&buf[..8]);
        let mut p = 8usize;
        while p + 8 <= buf.len() {
            let typ = u32::from_le_bytes(buf[p..p + 4].try_into().unwrap());
            let size = u32::from_le_bytes(buf[p + 4..p + 8].try_into().unwrap()) as usize;
            let content = p + 8;
            let end = content + size;
            if end > buf.len() || size > 0x1000_0000 {
                return Err(Error::Truncated);
            }
            let aligned = (end + 3) & !3;
            let next = if aligned <= buf.len() { aligned } else { buf.len() };
            let mut skip = false;
            if typ == AOT_SECTION_TYPE_CUSTOM && size >= 6 {
                let sub = u32::from_le_bytes(buf[content..content + 4].try_into().unwrap());
                if sub == AOT_CUSTOM_SECTION_RAW {
                    let slen =
                        u16::from_le_bytes(buf[content + 4..content + 6].try_into().unwrap()) as usize;
                    let name_off = content + 6;
                    if name_off + slen <= end {
                        let name_bytes = &buf[name_off..name_off + slen];
                        let bare = name_bytes.strip_suffix(&[0]).unwrap_or(name_bytes);
                        if bare == want {
                            skip = true;
                        }
                    }
                }
            }
            if !skip {
                out.extend_from_slice(&buf[p..next]);
            }
            p = next;
        }
        return Ok(out);
    }
    if &buf[0..4] == b"\x7fELF" {
        return without_sig_section_elf(buf);
    }
    Err(Error::NotModule)
}

/// WPSE trailing cookie (matches tools/wasmmod_elf.py).
const WPSE_MAGIC: &[u8; 4] = b"WPSE";
const WPSE_SIZE: usize = 28; // 4s QQ H H I

fn read_wpse_cookie(buf: &[u8]) -> Option<(u64, u64, u16, u16)> {
    if buf.len() < WPSE_SIZE {
        return None;
    }
    let off = buf.len() - WPSE_SIZE;
    if &buf[off..off + 4] != WPSE_MAGIC {
        return None;
    }
    let old_len = u64::from_le_bytes(buf[off + 4..off + 12].try_into().ok()?);
    let old_shoff = u64::from_le_bytes(buf[off + 12..off + 20].try_into().ok()?);
    let old_shnum = u16::from_le_bytes(buf[off + 20..off + 22].try_into().ok()?);
    let old_shstrndx = u16::from_le_bytes(buf[off + 22..off + 24].try_into().ok()?);
    if old_len == 0 || old_len as usize > buf.len() - WPSE_SIZE {
        return None;
    }
    Some((old_len, old_shoff, old_shnum, old_shstrndx))
}

fn without_sig_section_elf(buf: &[u8]) -> Result<Vec<u8>> {
    // No sig → drop trailing cookie only (clean naked digest).
    let has_sig = extract_elf_section(buf, SIG_SECTION)?.is_some();
    if !has_sig {
        if read_wpse_cookie(buf).is_some() {
            return Ok(buf[..buf.len() - WPSE_SIZE].to_vec());
        }
        return Ok(buf.to_vec());
    }
    let Some((old_len, old_shoff, old_shnum, old_shstrndx)) = read_wpse_cookie(buf) else {
        return Err(Error::Truncated);
    };
    // Require sig payload to live at/after old_len (last-append WPSE restore).
    if buf.len() < 64 || buf[4] != 2 || buf[5] != 1 {
        return Err(Error::Truncated);
    }
    let shoff = u64::from_le_bytes(buf[40..48].try_into().unwrap()) as usize;
    let shentsize = u16::from_le_bytes(buf[58..60].try_into().unwrap()) as usize;
    let shnum = u16::from_le_bytes(buf[60..62].try_into().unwrap()) as usize;
    let shstrndx = u16::from_le_bytes(buf[62..64].try_into().unwrap()) as usize;
    if shentsize < 64 || shnum == 0 || shstrndx >= shnum || shoff + shnum * shentsize > buf.len()
    {
        return Err(Error::Truncated);
    }
    let shstr = &buf[shoff + shstrndx * shentsize..];
    let str_off = u64::from_le_bytes(shstr[24..32].try_into().unwrap()) as usize;
    let str_sz = u64::from_le_bytes(shstr[32..40].try_into().unwrap()) as usize;
    if str_off + str_sz > buf.len() {
        return Err(Error::Truncated);
    }
    let strtab = &buf[str_off..str_off + str_sz];
    let want = SIG_SECTION.as_bytes();
    let want_dot = format!(".{SIG_SECTION}");
    let mut ok = false;
    for i in 0..shnum {
        let sh = &buf[shoff + i * shentsize..];
        let name_off = u32::from_le_bytes(sh[0..4].try_into().unwrap()) as usize;
        let typ = u32::from_le_bytes(sh[4..8].try_into().unwrap());
        if typ != 1 && typ != 7 {
            continue;
        }
        if name_off >= strtab.len() {
            continue;
        }
        let end = strtab[name_off..]
            .iter()
            .position(|&b| b == 0)
            .map(|p| name_off + p)
            .unwrap_or(strtab.len());
        let sname = &strtab[name_off..end];
        if sname != want && sname != want_dot.as_bytes() {
            continue;
        }
        let sec_off = u64::from_le_bytes(sh[24..32].try_into().unwrap());
        if sec_off >= old_len {
            ok = true;
            break;
        }
    }
    if !ok {
        return Err(Error::Truncated);
    }
    let mut out = buf[..old_len as usize].to_vec();
    if out.len() < 64 {
        return Err(Error::Truncated);
    }
    out[40..48].copy_from_slice(&old_shoff.to_le_bytes());
    out[60..62].copy_from_slice(&old_shnum.to_le_bytes());
    out[62..64].copy_from_slice(&old_shstrndx.to_le_bytes());
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::process::Command;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn uleb(mut n: u32) -> Vec<u8> {
        let mut out = Vec::new();
        loop {
            let mut b = (n & 0x7f) as u8;
            n >>= 7;
            if n != 0 {
                b |= 0x80;
            }
            out.push(b);
            if n == 0 {
                break;
            }
        }
        out
    }

    #[test]
    fn rejects_non_module() {
        assert!(matches!(SourceView::open_bytes(b"nope"), Err(Error::NotModule)));
        assert!(matches!(without_sig_section(b"nope"), Err(Error::NotModule)));
        assert!(matches!(list_sections(b"nope"), Err(Error::NotModule)));
    }

    #[test]
    fn lists_wasm_code_section() {
        let name = b"wasmmod.pack";
        let payload = b"PACK";
        let mut custom_body = uleb(name.len() as u32);
        custom_body.extend_from_slice(name);
        custom_body.extend_from_slice(payload);
        let mut custom = vec![0u8];
        custom.extend(uleb(custom_body.len() as u32));
        custom.extend(custom_body);

        let code_body = b"\x01\x04\x00\x41\x2a\x0b";
        let mut code = vec![10u8];
        code.extend(uleb(code_body.len() as u32));
        code.extend_from_slice(code_body);

        let mut wasm = b"\0asm\x01\0\0\0".to_vec();
        wasm.extend(custom);
        wasm.extend(code);

        let secs = list_sections(&wasm).unwrap();
        let code_sec = secs.iter().find(|s| s.name == "code").expect("code");
        assert_eq!(code_sec.role, SectionRole::Code);
        assert_eq!(code_sec.type_id, 10);
        assert_eq!(section_payload(&wasm, code_sec.index).unwrap(), code_body);

        let pack = secs.iter().find(|s| s.name == "wasmmod.pack").expect("pack");
        assert_eq!(pack.role, SectionRole::Meta);
    }

    #[test]
    fn elf_without_sig_wpse_roundtrip() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../..")
            .canonicalize()
            .expect("wasmmod root");
        let tools = root.join("tools");
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("wasmmod_read_elf_{stamp}"));
        std::fs::create_dir_all(&dir).unwrap();
        let src = dir.join("t.c");
        let obj = dir.join("t.o");
        let signed = dir.join("t.elf");
        std::fs::write(&src, "int answer(void) { return 42; }\n").unwrap();
        assert!(Command::new("gcc")
            .args([
                "-ffreestanding",
                "-fPIC",
                "-fno-plt",
                "-fno-stack-protector",
                "-O2",
                "-c",
                "-o",
            ])
            .arg(&obj)
            .arg(&src)
            .status()
            .unwrap()
            .success());
        let py = format!(
            "import sys\nsys.path.insert(0, r'{tools}')\nimport wasmmod_elf as elf\n\
raw = open(r'{obj}', 'rb').read()\nout = elf.append_section(raw, 'wasmmod.sig', b'FAKE_SIG_BYTES')\n\
open(r'{signed}', 'wb').write(out)\n",
            tools = tools.display(),
            obj = obj.display(),
            signed = signed.display(),
        );
        let out = Command::new("python3")
            .arg("-c")
            .arg(&py)
            .output()
            .expect("python3");
        assert!(
            out.status.success(),
            "py: {}",
            String::from_utf8_lossy(&out.stderr)
        );
        let naked = std::fs::read(&obj).unwrap();
        let with_sig = std::fs::read(&signed).unwrap();
        assert!(extract_elf_section(&with_sig, "wasmmod.sig")
            .unwrap()
            .is_some());
        let stripped = without_sig_section(&with_sig).expect("strip elf sig");
        assert_eq!(stripped, naked);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
