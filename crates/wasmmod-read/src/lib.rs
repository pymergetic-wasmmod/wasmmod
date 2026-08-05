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
pub const PACK_SECTION: &str = "wasmmod.pack";
pub const PACK_MAGIC: &[u8; 4] = b"MPWP";
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
            // Real ELF shndx — matches Symbol.section_index / disasm.
            index: i as u32,
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

/// Symbol from ELF `.symtab` or Wasm export section.
#[derive(Debug, Clone)]
pub struct Symbol {
    pub name: String,
    pub section_index: Option<u32>,
    pub offset: u64,
    pub size: u64,
    /// `func` | `data` | `export` | `other`
    pub kind: String,
    pub binding: String,
}

/// Source / symbol location (addr2line, defs, twins).
#[derive(Debug, Clone)]
pub struct Location {
    pub path: String,
    pub line: Option<i32>,
    /// `sym` | `dwarf` | `def` | `decl` | `twin`
    pub role: String,
}

/// One disassembly / hex-dump line.
#[derive(Debug, Clone)]
pub struct DisasmLine {
    pub addr: u64,
    pub raw: Vec<u8>,
    pub text: String,
}

const SHT_SYMTAB: u32 = 2;
const SHT_STRTAB: u32 = 3;
const STT_OBJECT: u8 = 1;
const STT_FUNC: u8 = 2;
const STB_LOCAL: u8 = 0;
const STB_GLOBAL: u8 = 1;
const STB_WEAK: u8 = 2;
const SHN_UNDEF: u16 = 0;
const SHN_LORESERVE: u16 = 0xff00;
const SHN_ABS: u16 = 0xfff1;
const SHN_COMMON: u16 = 0xfff2;
const STT_FILE: u8 = 4;
const ELF64_SYM_SIZE: usize = 24; // name/info/other/shndx/value/size

fn is_elf64_le(buf: &[u8]) -> bool {
    buf.len() >= 64 && &buf[0..4] == b"\x7fELF" && buf[4] == 2 && buf[5] == 1
}

fn elf_shdr_table(elf: &[u8]) -> Result<(usize, usize, usize)> {
    if !is_elf64_le(elf) {
        return Err(Error::Truncated);
    }
    let shoff = u64::from_le_bytes(elf[40..48].try_into().unwrap()) as usize;
    let shentsize = u16::from_le_bytes(elf[58..60].try_into().unwrap()) as usize;
    let shnum = u16::from_le_bytes(elf[60..62].try_into().unwrap()) as usize;
    if shentsize < 64 || shnum == 0 || shoff + shnum * shentsize > elf.len() {
        return Err(Error::Truncated);
    }
    Ok((shoff, shentsize, shnum))
}

fn elf_sh_name(shstrtab: &[u8], name_off: usize) -> Option<&str> {
    if name_off >= shstrtab.len() {
        return None;
    }
    let end = shstrtab[name_off..]
        .iter()
        .position(|&b| b == 0)
        .map(|p| name_off + p)
        .unwrap_or(shstrtab.len());
    std::str::from_utf8(&shstrtab[name_off..end]).ok()
}

fn elf_shstrtab(elf: &[u8]) -> Result<&[u8]> {
    let (shoff, shentsize, shnum) = elf_shdr_table(elf)?;
    let shstrndx = u16::from_le_bytes(elf[62..64].try_into().unwrap()) as usize;
    if shstrndx >= shnum {
        return Err(Error::Truncated);
    }
    let shstr = &elf[shoff + shstrndx * shentsize..];
    let str_off = u64::from_le_bytes(shstr[24..32].try_into().unwrap()) as usize;
    let str_sz = u64::from_le_bytes(shstr[32..40].try_into().unwrap()) as usize;
    if str_off + str_sz > elf.len() {
        return Err(Error::Truncated);
    }
    Ok(&elf[str_off..str_off + str_sz])
}

/// True if ELF has `.debug_line` or `.debug_info`.
pub fn has_dwarf(buf: &[u8]) -> bool {
    if !is_elf64_le(buf) {
        return false;
    }
    let Ok((shoff, shentsize, shnum)) = elf_shdr_table(buf) else {
        return false;
    };
    let Ok(strtab) = elf_shstrtab(buf) else {
        return false;
    };
    for i in 0..shnum {
        let sh = &buf[shoff + i * shentsize..];
        let name_off = u32::from_le_bytes(sh[0..4].try_into().unwrap()) as usize;
        if let Some(name) = elf_sh_name(strtab, name_off) {
            if name == ".debug_line" || name == ".debug_info" {
                return true;
            }
        }
    }
    false
}

fn bind_name(info: u8) -> String {
    match info >> 4 {
        STB_LOCAL => "local".into(),
        STB_GLOBAL => "global".into(),
        STB_WEAK => "weak".into(),
        b => b.to_string(),
    }
}

fn kind_name(info: u8) -> String {
    match info & 0xf {
        STT_FUNC => "func".into(),
        STT_OBJECT => "data".into(),
        _ => "other".into(),
    }
}

fn list_symbols_elf(elf: &[u8]) -> Result<Vec<Symbol>> {
    let (shoff, shentsize, shnum) = elf_shdr_table(elf)?;
    let mut out = Vec::new();
    for i in 0..shnum {
        let sh = &elf[shoff + i * shentsize..];
        let sh_type = u32::from_le_bytes(sh[4..8].try_into().unwrap());
        if sh_type != SHT_SYMTAB {
            continue;
        }
        let sym_off = u64::from_le_bytes(sh[24..32].try_into().unwrap()) as usize;
        let sym_sz = u64::from_le_bytes(sh[32..40].try_into().unwrap()) as usize;
        let sh_link = u32::from_le_bytes(sh[40..44].try_into().unwrap()) as usize;
        let entsize = u64::from_le_bytes(sh[56..64].try_into().unwrap()) as usize;
        if entsize < ELF64_SYM_SIZE || sh_link >= shnum {
            continue;
        }
        let str_sh = &elf[shoff + sh_link * shentsize..];
        let str_type = u32::from_le_bytes(str_sh[4..8].try_into().unwrap());
        if str_type != SHT_STRTAB {
            continue;
        }
        let stab_off = u64::from_le_bytes(str_sh[24..32].try_into().unwrap()) as usize;
        let stab_sz = u64::from_le_bytes(str_sh[32..40].try_into().unwrap()) as usize;
        if stab_off + stab_sz > elf.len() || sym_off + sym_sz > elf.len() {
            return Err(Error::Truncated);
        }
        let stab = &elf[stab_off..stab_off + stab_sz];
        let nsym = sym_sz / entsize;
        for k in 0..nsym {
            let off = sym_off + k * entsize;
            let st_name = u32::from_le_bytes(elf[off..off + 4].try_into().unwrap()) as usize;
            let st_info = elf[off + 4];
            let st_shndx = u16::from_le_bytes(elf[off + 6..off + 8].try_into().unwrap());
            let st_value = u64::from_le_bytes(elf[off + 8..off + 16].try_into().unwrap());
            let st_size = u64::from_le_bytes(elf[off + 16..off + 24].try_into().unwrap());
            if st_shndx == SHN_UNDEF
                || st_shndx == SHN_ABS
                || st_shndx == SHN_COMMON
                || st_shndx >= SHN_LORESERVE
                || st_name == 0
                || st_name >= stab.len()
            {
                continue;
            }
            if st_info & 0xf == STT_FILE {
                continue;
            }
            let end = stab[st_name..]
                .iter()
                .position(|&b| b == 0)
                .map(|p| st_name + p)
                .unwrap_or(stab.len());
            let sname = String::from_utf8_lossy(&stab[st_name..end]).into_owned();
            if sname.is_empty() || sname.starts_with('.') {
                continue;
            }
            out.push(Symbol {
                name: sname,
                section_index: Some(u32::from(st_shndx)),
                offset: st_value,
                size: st_size,
                kind: kind_name(st_info),
                binding: bind_name(st_info),
            });
        }
    }
    out.sort_by(|a, b| {
        (
            a.section_index.unwrap_or(0),
            a.offset,
            a.name.as_str(),
        )
            .cmp(&(
                b.section_index.unwrap_or(0),
                b.offset,
                b.name.as_str(),
            ))
    });
    Ok(out)
}

fn skip_importdesc(wasm: &[u8], j: &mut usize, end: usize) -> Result<()> {
    if *j >= end {
        return Err(Error::Truncated);
    }
    let kind = wasm[*j];
    *j += 1;
    match kind {
        0 => {
            let _ = read_uleb(wasm, j)?;
            Ok(())
        }
        1 => {
            if *j >= end {
                return Err(Error::Truncated);
            }
            *j += 1; // reftype
            let flags = read_uleb(wasm, j)?;
            let _ = read_uleb(wasm, j)?;
            if flags & 1 != 0 {
                let _ = read_uleb(wasm, j)?;
            }
            Ok(())
        }
        2 => {
            let flags = read_uleb(wasm, j)?;
            let _ = read_uleb(wasm, j)?;
            if flags & 1 != 0 {
                let _ = read_uleb(wasm, j)?;
            }
            Ok(())
        }
        3 => {
            if *j + 2 > end {
                return Err(Error::Truncated);
            }
            *j += 2;
            Ok(())
        }
        _ => Err(Error::Truncated),
    }
}

fn list_symbols_wasm(wasm: &[u8]) -> Result<Vec<Symbol>> {
    if wasm.len() < 8 {
        return Ok(Vec::new());
    }
    let mut n_func_imports = 0u32;
    let mut code_sec_index: Option<u32> = None;
    let mut code_entries: Vec<(u64, u64)> = Vec::new();
    let mut export_range: Option<(usize, usize)> = None;
    let mut i = 8usize;
    let mut sec_list_i = 0u32;
    // Best-effort: gather what we can, then parse exports.
    let walk = (|| -> Result<()> {
        while i < wasm.len() {
            let sid = wasm[i];
            i += 1;
            let slen = read_uleb(wasm, &mut i)? as usize;
            let start = i;
            let end = i + slen;
            if end > wasm.len() {
                return Err(Error::Truncated);
            }
            if sid == 2 {
                let mut j = start;
                let nimp = read_uleb(wasm, &mut j)? as usize;
                for _ in 0..nimp {
                    let mlen = read_uleb(wasm, &mut j)? as usize;
                    j += mlen;
                    let flen = read_uleb(wasm, &mut j)? as usize;
                    j += flen;
                    if j >= end {
                        break;
                    }
                    if wasm[j] == 0 {
                        n_func_imports += 1;
                    }
                    skip_importdesc(wasm, &mut j, end)?;
                }
            } else if sid == 10 {
                code_sec_index = Some(sec_list_i);
                let mut j = start;
                let ncode = read_uleb(wasm, &mut j)? as usize;
                for _ in 0..ncode {
                    let entry = j;
                    let size = read_uleb(wasm, &mut j)? as usize;
                    if j + size > end {
                        break;
                    }
                    j += size;
                    code_entries.push(((entry - start) as u64, (j - entry) as u64));
                }
            } else if sid == 7 {
                export_range = Some((start, end));
            }
            i = end;
            sec_list_i += 1;
        }
        Ok(())
    })();
    let _ = walk;

    let mut out = Vec::new();
    let Some((start, end)) = export_range else {
        return Ok(out);
    };
    let parse = (|| -> Result<()> {
        let mut j = start;
        let nexp = read_uleb(wasm, &mut j)? as usize;
        for _ in 0..nexp {
            if j >= end {
                break;
            }
            let nlen = read_uleb(wasm, &mut j)? as usize;
            if j + nlen > end {
                break;
            }
            let name = String::from_utf8_lossy(&wasm[j..j + nlen]).into_owned();
            j += nlen;
            if j >= end {
                break;
            }
            let kind = wasm[j];
            j += 1;
            let idx = read_uleb(wasm, &mut j)? as u32;
            let mut offset = 0u64;
            let mut size = 0u64;
            let mut section_index = code_sec_index;
            if kind == 0 {
                if idx >= n_func_imports {
                    let local = (idx - n_func_imports) as usize;
                    if local < code_entries.len() {
                        offset = code_entries[local].0;
                        size = code_entries[local].1;
                    }
                }
            } else {
                section_index = None;
            }
            out.push(Symbol {
                name,
                section_index,
                offset,
                size,
                kind: if kind == 0 {
                    "export".into()
                } else {
                    "other".into()
                },
                binding: "export".into(),
            });
        }
        Ok(())
    })();
    let _ = parse;
    Ok(out)
}

/// List symbols from ELF `.symtab` or Wasm exports.
pub fn list_symbols(buf: &[u8]) -> Result<Vec<Symbol>> {
    if is_elf64_le(buf) {
        return list_symbols_elf(buf);
    }
    if buf.len() >= 4 && &buf[0..4] == b"\0asm" {
        return list_symbols_wasm(buf);
    }
    Ok(Vec::new())
}

/// Map address → locations.
///
/// Prefer host ``addr2line`` (binutils) when `.debug_*` is present, else the
/// enclosing FUNC as ``role=sym``.
pub fn addr2line(buf: &[u8], addr: u64) -> Result<Vec<Location>> {
    if !is_elf64_le(buf) {
        return Ok(Vec::new());
    }
    if has_dwarf(buf) {
        if let Some(loc) = addr2line_binutils(buf, addr) {
            return Ok(vec![loc]);
        }
    }
    let syms = list_symbols_elf(buf)?;
    let mut best: Option<&Symbol> = None;
    for s in &syms {
        if s.kind != "func" || s.size == 0 {
            continue;
        }
        if s.offset <= addr
            && addr < s.offset + s.size
            && best.map(|b| s.offset >= b.offset).unwrap_or(true)
        {
            best = Some(s);
        }
    }
    Ok(match best {
        Some(s) => vec![Location {
            path: s.name.clone(),
            line: None,
            role: "sym".into(),
        }],
        None => Vec::new(),
    })
}

fn addr2line_binutils(buf: &[u8], addr: u64) -> Option<Location> {
    use std::process::Command;

    let dir = tempfile_dir()?;
    let path = dir.join("obj.elf");
    std::fs::write(&path, buf).ok()?;
    let out = Command::new("addr2line")
        .args(["-e", path.to_str()?, "-C", &format!("{addr:x}")])
        .output()
        .ok()?;
    let _ = std::fs::remove_dir_all(&dir);
    if !out.status.success() && out.stdout.is_empty() {
        return None;
    }
    let text = String::from_utf8_lossy(&out.stdout);
    let raw = text.lines().next()?.trim();
    if raw.is_empty() || raw.starts_with("??") {
        return None;
    }
    let (path_s, line_s) = parse_addr2line_path_line(raw)?;
    let lineno: i32 = line_s.parse().ok()?;
    if lineno <= 0 {
        return None;
    }
    Some(Location {
        path: path_s,
        line: Some(lineno),
        role: "dwarf".into(),
    })
}

fn parse_addr2line_path_line(raw: &str) -> Option<(String, String)> {
    let parts: Vec<&str> = raw.rsplitn(3, ':').collect();
    // rsplitn yields reverse pieces: ``a:b:c`` → [c, b, a]
    let (mut path_s, line_s) = if parts.len() == 3
        && !parts[0].is_empty()
        && parts[0].chars().all(|c| c.is_ascii_digit())
        && parts[1].chars().all(|c| c.is_ascii_digit())
    {
        (parts[2].to_string(), parts[1].to_string())
    } else if parts.len() >= 2
        && !parts[0].is_empty()
        && parts[0].chars().all(|c| c.is_ascii_digit())
    {
        (parts[1].to_string(), parts[0].to_string())
    } else {
        return None;
    };
    if path_s.is_empty() {
        return None;
    }
    if let Some(idx) = path_s.find("/src/") {
        path_s = path_s[idx + 1..].to_string();
    } else if let Some(idx) = path_s.find("/examples/") {
        path_s = path_s[idx + 1..].to_string();
    } else if path_s.starts_with('/') {
        path_s = std::path::Path::new(&path_s)
            .file_name()?
            .to_string_lossy()
            .into_owned();
    }
    Some((path_s, line_s))
}

fn tempfile_dir() -> Option<std::path::PathBuf> {
    use std::time::{SystemTime, UNIX_EPOCH};
    let n = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .ok()?
        .as_nanos();
    let dir = std::env::temp_dir().join(format!("wasmmod-read-a2l-{n}"));
    std::fs::create_dir(&dir).ok()?;
    Some(dir)
}

/// Hex `db` lines for a byte window (Capstone-free fallback).
pub fn disasm_db(data: &[u8], base: u64) -> Vec<DisasmLine> {
    let mut out = Vec::new();
    for i in (0..data.len()).step_by(8) {
        let end = (i + 8).min(data.len());
        let raw = data[i..end].to_vec();
        let text = format!(
            "db {}",
            raw.iter()
                .map(|b| format!("{b:02x}"))
                .collect::<Vec<_>>()
                .join(" ")
        );
        out.push(DisasmLine {
            addr: base + i as u64,
            raw,
            text,
        });
    }
    out
}

fn wasm_op_name(op: u8) -> String {
    match op {
        0x0b => "end".into(),
        0x10 => "call".into(),
        0x20 => "local.get".into(),
        0x21 => "local.set".into(),
        0x41 => "i32.const".into(),
        0x42 => "i64.const".into(),
        0x6a => "i32.add".into(),
        _ => format!("op_{op:02x}"),
    }
}

fn wasm_code_window(wasm: &[u8], offset: usize, limit: usize) -> Result<&[u8]> {
    if wasm.len() < 8 {
        return Ok(&[]);
    }
    let mut i = 8usize;
    while i < wasm.len() {
        let sid = wasm[i];
        i += 1;
        let slen = read_uleb(wasm, &mut i)? as usize;
        let start = i;
        let end = i + slen;
        if end > wasm.len() {
            return Err(Error::Truncated);
        }
        if sid == 10 {
            if offset >= slen {
                return Ok(&[]);
            }
            let avail = (slen - offset).min(if limit == 0 { slen - offset } else { limit });
            return Ok(&wasm[start + offset..start + offset + avail]);
        }
        i = end;
    }
    Ok(&[])
}

/// Disassemble a section window. ELF: `section_index` is shndx + hex `db` lines.
/// Wasm: `section_index` is ignored; windows the code section with `op_*` lines
/// (same semantics as C/`tools/wasmmod_inspect.py`).
pub fn disasm(
    buf: &[u8],
    section_index: u32,
    offset: u64,
    limit: usize,
) -> Result<Vec<DisasmLine>> {
    let lim = if limit == 0 { 64 } else { limit };
    let off = offset as usize;
    if is_elf64_le(buf) {
        let payload = section_payload(buf, section_index)?;
        if off >= payload.len() {
            return Ok(Vec::new());
        }
        let end = payload.len().min(off.saturating_add(lim));
        return Ok(disasm_db(&payload[off..end], offset));
    }
    if buf.len() >= 4 && &buf[0..4] == b"\0asm" {
        let chunk = wasm_code_window(buf, off, lim)?;
        let mut out = Vec::new();
        for (j, &op) in chunk.iter().enumerate() {
            out.push(DisasmLine {
                addr: offset + j as u64,
                raw: vec![op],
                text: wasm_op_name(op),
            });
            if out.len() >= 64 {
                break;
            }
        }
        return Ok(out);
    }
    Ok(Vec::new())
}

/// Paths worth scanning for symbol defs (matches CDN `_is_code_source_path`).
fn is_code_source_path(path: &str) -> bool {
    if path.is_empty() || path.ends_with(".mpy") {
        return false;
    }
    let norm = path.replace('\\', "/").trim_start_matches("./").to_ascii_lowercase();
    if norm.starts_with("docs/") || format!("/{norm}").contains("/docs/") {
        return false;
    }
    [
        ".py", ".pyi", ".c", ".h", ".cc", ".cpp", ".hpp", ".hh", ".rs",
    ]
    .iter()
    .any(|sfx| norm.ends_with(sfx))
}

fn line_is_commentish(line: &str) -> bool {
    let s = line.trim();
    if s.is_empty() {
        return false;
    }
    if s.starts_with("//") || s.starts_with("/*") {
        return true;
    }
    if let Some(rest) = s.strip_prefix('*') {
        return rest.is_empty() || rest.starts_with([' ', '\t', '/']);
    }
    false
}

fn is_ident_byte(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

/// Next `\bname\b` start index in `hay`, or None.
fn find_word(hay: &str, name: &str) -> Option<usize> {
    if name.is_empty() {
        return None;
    }
    let hb = hay.as_bytes();
    let nb = name.as_bytes();
    let mut i = 0usize;
    while i + nb.len() <= hb.len() {
        if &hb[i..i + nb.len()] == nb {
            let before_ok = i == 0 || !is_ident_byte(hb[i - 1]);
            let after = i + nb.len();
            let after_ok = after >= hb.len() || !is_ident_byte(hb[after]);
            if before_ok && after_ok {
                return Some(i);
            }
        }
        i += 1;
    }
    None
}

fn skip_ws(s: &str) -> &str {
    s.trim_start_matches([' ', '\t'])
}

/// After `name`, match `(…)` with no `;`/`{`/`}` inside the args, return rest after `)`.
fn after_name_paren<'a>(line: &'a str, name: &str) -> Option<&'a str> {
    let start = find_word(line, name)?;
    let mut rest = skip_ws(&line[start + name.len()..]);
    if !rest.starts_with('(') {
        return None;
    }
    rest = &rest[1..];
    let mut depth = 1i32;
    let bytes = rest.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        let c = bytes[i];
        if c == b'(' {
            depth += 1;
        } else if c == b')' {
            depth -= 1;
            if depth == 0 {
                return Some(skip_ws(&rest[i + 1..]));
            }
        } else if c == b';' || c == b'{' || c == b'}' {
            return None;
        }
        i += 1;
    }
    None
}

/// Definition-like hits — mirrors `tools/wasmmod_inspect._source_def_hits`.
fn source_def_hits(path: &str, text: &str, name: &str) -> Vec<(i32, &'static str)> {
    let mut hits = Vec::new();
    let lines: Vec<&str> = text.lines().collect();
    let lower = path.to_ascii_lowercase();
    if lower.ends_with(".py") || lower.ends_with(".pyi") {
        for (i, line) in lines.iter().enumerate() {
            if line.trim_start().starts_with('#') {
                continue;
            }
            let mut s = line.trim_start();
            if let Some(rest) = s.strip_prefix("async") {
                if rest.starts_with([' ', '\t']) {
                    s = skip_ws(rest);
                }
            }
            if let Some(rest) = s.strip_prefix("def") {
                if !rest.starts_with([' ', '\t']) {
                    continue;
                }
                let rest = skip_ws(rest);
                if !rest.starts_with(name) {
                    continue;
                }
                let after = &rest[name.len()..];
                if !after.is_empty() && is_ident_byte(after.as_bytes()[0]) {
                    continue;
                }
                if skip_ws(after).starts_with('(') {
                    hits.push(((i + 1) as i32, "twin"));
                }
            }
        }
        return hits;
    }
    if lower.ends_with(".rs") {
        for (i, line) in lines.iter().enumerate() {
            if line_is_commentish(line) {
                continue;
            }
            let mut s = line.trim_start();
            if let Some(rest) = s.strip_prefix("pub") {
                if rest.starts_with([' ', '\t']) {
                    s = skip_ws(rest);
                }
            }
            if let Some(rest) = s.strip_prefix("async") {
                if rest.starts_with([' ', '\t']) {
                    s = skip_ws(rest);
                }
            }
            if let Some(rest) = s.strip_prefix("fn") {
                if !rest.starts_with([' ', '\t']) {
                    continue;
                }
                let rest = skip_ws(rest);
                if !rest.starts_with(name) {
                    continue;
                }
                let after = &rest[name.len()..];
                if !after.is_empty() && is_ident_byte(after.as_bytes()[0]) {
                    continue;
                }
                let after = skip_ws(after);
                if after.starts_with('(') || after.starts_with('<') {
                    hits.push(((i + 1) as i32, "def"));
                }
            }
        }
        return hits;
    }
    let is_header = lower.ends_with(".h")
        || lower.ends_with(".hpp")
        || lower.ends_with(".hh");
    for (i, line) in lines.iter().enumerate() {
        let lineno = (i + 1) as i32;
        if line_is_commentish(line) {
            continue;
        }
        if let Some(after) = after_name_paren(line, name) {
            if after.starts_with('{') {
                hits.push((lineno, if is_header { "decl" } else { "def" }));
                continue;
            }
            if is_header && after.starts_with(';') {
                hits.push((lineno, "decl"));
                continue;
            }
            if after.is_empty() && !is_header {
                for nxt_line in lines.iter().take(lines.len().min(i + 4)).skip(i + 1) {
                    let nxt = nxt_line.trim();
                    if nxt.is_empty() || line_is_commentish(nxt_line) {
                        continue;
                    }
                    if nxt.starts_with('{') {
                        hits.push((lineno, "def"));
                    }
                    break;
                }
            }
        }
    }
    hits
}

fn inflate_pack_blob(data: &[u8], zlib: bool, raw_len: u32) -> Result<Vec<u8>> {
    if !zlib {
        return Ok(data.to_vec());
    }
    let mut dec = ZlibDecoder::new(data);
    let mut out = Vec::with_capacity(raw_len as usize);
    dec.read_to_end(&mut out).map_err(|_| Error::Inflate("pack".into()))?;
    if out.len() != raw_len as usize {
        return Err(Error::Inflate("pack".into()));
    }
    Ok(out)
}

/// Yield (path, utf-8 text) for code-like pack files (v1–v3 MPWP).
fn for_each_pack_code_text(buf: &[u8], mut visit: impl FnMut(&str, &str)) {
    let Ok(Some(payload)) = extract_custom_section(buf, PACK_SECTION) else {
        return;
    };
    if payload.len() < 12 || &payload[0..4] != PACK_MAGIC {
        return;
    }
    let mut i = 4usize;
    let Ok(version) = read_u16(payload, &mut i) else {
        return;
    };
    let _flags = match read_u16(payload, &mut i) {
        Ok(v) => v,
        Err(_) => return,
    };
    let Ok(name_len) = read_u16(payload, &mut i) else {
        return;
    };
    if read_bytes(payload, &mut i, name_len as usize).is_err() {
        return;
    }
    let Ok(n_files) = read_u32(payload, &mut i) else {
        return;
    };
    for _ in 0..n_files {
        let Ok(pl) = read_u16(payload, &mut i) else {
            return;
        };
        let Ok(path_b) = read_bytes(payload, &mut i, pl as usize) else {
            return;
        };
        let path = String::from_utf8_lossy(path_b).into_owned();
        if i >= payload.len() {
            return;
        }
        let kind = payload[i];
        i += 1;
        let (zlib, raw_len, blob) = if version >= 3 {
            if i >= payload.len() {
                return;
            }
            let fflags = payload[i];
            i += 1;
            let Ok(raw_len) = read_u32(payload, &mut i) else {
                return;
            };
            let Ok(data_len) = read_u32(payload, &mut i) else {
                return;
            };
            let Ok(data) = read_bytes(payload, &mut i, data_len as usize) else {
                return;
            };
            (fflags & FILE_FLAG_ZLIB != 0, raw_len, data)
        } else {
            let Ok(data_len) = read_u32(payload, &mut i) else {
                return;
            };
            let Ok(data) = read_bytes(payload, &mut i, data_len as usize) else {
                return;
            };
            (false, data_len, data)
        };
        // kind 1=.py, 3=raw (may be .c/.h/.rs text); skip mpy/pyc.
        if kind != 1 && kind != 3 {
            continue;
        }
        if !is_code_source_path(&path) {
            continue;
        }
        let Ok(bytes) = inflate_pack_blob(blob, zlib, raw_len) else {
            continue;
        };
        let Ok(text) = std::str::from_utf8(&bytes) else {
            continue;
        };
        if text.contains('\0') {
            continue;
        }
        visit(&path, text);
    }
}

fn append_embedded_source_locations(buf: &[u8], name: &str, out: &mut Vec<Location>) {
    let mut seen_paths = std::collections::HashSet::new();
    if let Ok(view) = SourceView::open_bytes(buf) {
        for f in &view.files {
            if !is_code_source_path(&f.path) {
                continue;
            }
            let Ok(bytes) = f.bytes() else {
                continue;
            };
            let Ok(text) = std::str::from_utf8(&bytes) else {
                continue;
            };
            seen_paths.insert(f.path.clone());
            for (line, role) in source_def_hits(&f.path, text, name) {
                out.push(Location {
                    path: f.path.clone(),
                    line: Some(line),
                    role: role.into(),
                });
            }
        }
    }
    for_each_pack_code_text(buf, |path, text| {
        if seen_paths.contains(path) {
            return;
        }
        for (line, role) in source_def_hits(path, text, name) {
            out.push(Location {
                path: path.into(),
                line: Some(line),
                role: role.into(),
            });
        }
    });
}

/// Locations for a symbol name (addr2line at func start + `role=sym` + source defs).
pub fn locations_for_symbol(buf: &[u8], name: &str) -> Result<Vec<Location>> {
    let mut out = Vec::new();
    for s in list_symbols(buf)? {
        if s.name != name {
            continue;
        }
        if s.kind == "func" && s.size > 0 {
            out.extend(addr2line(buf, s.offset)?);
        }
        let have_sym = out.iter().any(|l| {
            l.role == "sym" && l.line.is_none() && l.path == name
        });
        if !have_sym {
            out.push(Location {
                path: name.into(),
                line: None,
                role: "sym".into(),
            });
        }
        break;
    }
    append_embedded_source_locations(buf, name, &mut out);
    Ok(collapse_locations(out))
}

fn role_rank(role: &str) -> u8 {
    match role {
        "dwarf" => 0,
        "def" => 1,
        "decl" => 2,
        "twin" => 3,
        "sym" => 9,
        _ => 5,
    }
}

/// One chip per source line: merge dwarf+def on the same basename:line.
fn collapse_locations(locs: Vec<Location>) -> Vec<Location> {
    use std::collections::HashMap;

    // ("L", basename, line) or ("N", path, role)
    type K = (u8, String, u64);
    let mut best: HashMap<K, Location> = HashMap::new();
    let mut order: Vec<K> = Vec::new();
    for loc in locs {
        let key = match loc.line {
            Some(n) => {
                let base = loc
                    .path
                    .rsplit('/')
                    .next()
                    .unwrap_or(&loc.path)
                    .to_string();
                (0u8, base, n as u64)
            }
            None => (1u8, format!("{}\0{}", loc.path, loc.role), 0),
        };
        match best.get_mut(&key) {
            None => {
                order.push(key.clone());
                best.insert(key, loc);
            }
            Some(cur) => {
                let rank_new = role_rank(&loc.role);
                let rank_old = role_rank(&cur.role);
                let path = if loc.path.len() >= cur.path.len() {
                    loc.path.clone()
                } else {
                    cur.path.clone()
                };
                let role = if rank_new < rank_old
                    || (rank_new == rank_old && loc.path.len() > cur.path.len())
                {
                    loc.role.clone()
                } else {
                    cur.role.clone()
                };
                *cur = Location {
                    path,
                    line: loc.line,
                    role,
                };
            }
        }
    }
    let mut out: Vec<Location> = order
        .into_iter()
        .filter_map(|k| best.remove(&k))
        .collect();
    out.sort_by(|a, b| {
        role_rank(&a.role)
            .cmp(&role_rank(&b.role))
            .then_with(|| a.path.cmp(&b.path))
    });
    out
}

/// Basic `.mpy` dump: header + bytecode bytes.
pub fn mpy_disasm(mpy: &[u8], limit: usize) -> Vec<DisasmLine> {
    let mut out = Vec::new();
    if mpy.len() < 4 {
        out.push(DisasmLine {
            addr: 0,
            raw: mpy.to_vec(),
            text: "truncated mpy".into(),
        });
        return out;
    }
    out.push(DisasmLine {
        addr: 0,
        raw: mpy[..4].to_vec(),
        text: format!(
            "mpy_hdr {:02x} {:02x} {:02x} {:02x}",
            mpy[0], mpy[1], mpy[2], mpy[3]
        ),
    });
    let n = (mpy.len() - 4).min(limit);
    for i in 0..n {
        let b = mpy[4 + i];
        out.push(DisasmLine {
            addr: 4 + i as u64,
            raw: vec![b],
            text: format!("bc 0x{b:02x}"),
        });
    }
    out
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

    #[test]
    fn wasm_export_name_leb128() {
        let name = vec![b'a'; 130];
        let mut body = vec![1u8]; // nexp
        body.extend(uleb(name.len() as u32));
        body.extend_from_slice(&name);
        body.push(0); // func
        body.push(0); // index
        let mut sec = vec![7u8];
        sec.extend(uleb(body.len() as u32));
        sec.extend(body);
        let mut wasm = b"\0asm\x01\0\0\0".to_vec();
        wasm.extend(sec);
        let syms = list_symbols(&wasm).unwrap();
        assert_eq!(syms.len(), 1);
        assert_eq!(syms[0].name, "a".repeat(130));
    }

    #[test]
    fn wasm_disasm_ignores_index_uses_code() {
        // code section: one function body bytes 0x41 0x2a 0x0b (i32.const / end)
        let code_body = b"\x01\x04\x00\x41\x2a\x0b";
        let mut code = vec![10u8];
        code.extend(uleb(code_body.len() as u32));
        code.extend_from_slice(code_body);
        let mut wasm = b"\0asm\x01\0\0\0".to_vec();
        wasm.extend(code);
        // index 99 must still window code (parity with C/Python).
        let lines = disasm(&wasm, 99, 0, 16).unwrap();
        assert!(!lines.is_empty());
        assert!(lines.iter().any(|l| l.text == "i32.const" || l.text.starts_with("op_")));
    }

    #[test]
    fn wasm_func_export_code_offsets() {
        // type + function + export hello/add + two code bodies
        let mut wasm = b"\0asm\x01\0\0\0".to_vec();
        // type: 1 func type ()->i32
        let typ = b"\x01\x60\x00\x01\x7f";
        wasm.push(1);
        wasm.extend(uleb(typ.len() as u32));
        wasm.extend_from_slice(typ);
        // function: 2 funcs, both type 0
        let func = b"\x02\x00\x00";
        wasm.push(3);
        wasm.extend(uleb(func.len() as u32));
        wasm.extend_from_slice(func);
        // export: hello → func 0, add → func 1
        let mut exp = vec![2u8];
        for (name, idx) in [(b"hello".as_slice(), 0u8), (b"add".as_slice(), 1u8)] {
            exp.extend(uleb(name.len() as u32));
            exp.extend_from_slice(name);
            exp.push(0); // func
            exp.push(idx);
        }
        wasm.push(7);
        wasm.extend(uleb(exp.len() as u32));
        wasm.extend(exp);
        // code: two entries
        let mut code = vec![2u8];
        let b0 = b"\x00\x41\x2a\x0b"; // size-prefixed via outer
        code.extend(uleb(b0.len() as u32));
        code.extend_from_slice(b0);
        let b1 = b"\x00\x20\x00\x20\x01\x6a\x0b";
        code.extend(uleb(b1.len() as u32));
        code.extend_from_slice(b1);
        wasm.push(10);
        wasm.extend(uleb(code.len() as u32));
        wasm.extend(code);

        let syms = list_symbols(&wasm).unwrap();
        let hello = syms.iter().find(|s| s.name == "hello").unwrap();
        let add = syms.iter().find(|s| s.name == "add").unwrap();
        assert!(hello.size > 0 && add.size > 0);
        assert_ne!(hello.offset, add.offset);
        assert_eq!(hello.section_index, add.section_index);
    }

    #[test]
    fn locations_dedupe_sym_role() {
        // Minimal ET_REL-ish ELF not required: empty locations for missing name.
        assert!(locations_for_symbol(b"nope", "x").unwrap().is_empty());
    }

    #[test]
    fn collapse_locations_prefers_dwarf_and_longer_path() {
        let out = collapse_locations(vec![
            Location {
                path: "hello.c".into(),
                line: Some(4),
                role: "dwarf".into(),
            },
            Location {
                path: "hello".into(),
                line: None,
                role: "sym".into(),
            },
            Location {
                path: "src/hello.c".into(),
                line: Some(4),
                role: "def".into(),
            },
            Location {
                path: "src/hello.h".into(),
                line: Some(1),
                role: "decl".into(),
            },
        ]);
        assert_eq!(out.len(), 3);
        assert_eq!(out[0].path, "src/hello.c");
        assert_eq!(out[0].line, Some(4));
        assert_eq!(out[0].role, "dwarf");
        assert_eq!(out[1].path, "src/hello.h");
        assert_eq!(out[1].role, "decl");
        assert_eq!(out[2].role, "sym");
    }

    #[test]
    fn source_def_hits_skips_comments_and_calls() {
        let text = "\
int hello(void);\n\
int helper(void) { return hello(); }\n\
/* int hello(void) { */\n\
// int hello(void) {\n\
/*\n\
 * int hello(void) { return 0; }\n\
 */\n\
int hello(void)\n\
{\n\
    return 42;\n\
}\n";
        let hits = source_def_hits("src/hello.c", text, "hello");
        assert!(hits.iter().any(|(l, r)| *l == 8 && *r == "def"));
        assert!(!hits.iter().any(|(l, _)| matches!(l, 1 | 2 | 3 | 4 | 6)));

        let hdr = "int hello(void);\n/* Call hello(x). */\n";
        let decls = source_def_hits("src/hello.h", hdr, "hello");
        assert_eq!(decls, vec![(1, "decl")]);

        let py = "def hello():\n    return 42\n\ndef other():\n    return hello()\n";
        let twins = source_def_hits("__init__.py", py, "hello");
        assert_eq!(twins, vec![(1, "twin")]);
    }

    #[test]
    fn locations_scan_embedded_source() {
        // Minimal Wasm + MPSR with one C file defining hello.
        let mut mpsr = Vec::new();
        mpsr.extend_from_slice(b"MPSR");
        mpsr.extend_from_slice(&1u16.to_le_bytes()); // version
        mpsr.extend_from_slice(&0u16.to_le_bytes()); // flags
        let name = b"hello";
        mpsr.extend_from_slice(&(name.len() as u16).to_le_bytes());
        mpsr.extend_from_slice(name);
        mpsr.extend_from_slice(&0u16.to_le_bytes()); // pkg_version len
        mpsr.extend_from_slice(&0u16.to_le_bytes()); // n_tags
        mpsr.extend_from_slice(&1u32.to_le_bytes()); // n_files
        let path = b"src/hello.c";
        mpsr.extend_from_slice(&(path.len() as u16).to_le_bytes());
        mpsr.extend_from_slice(path);
        mpsr.push(0); // flags
        let body = b"/* int hello(void) { */\nint hello(void) { return 1; }\n";
        mpsr.extend_from_slice(&(body.len() as u32).to_le_bytes()); // raw_len
        mpsr.extend_from_slice(&(body.len() as u32).to_le_bytes()); // data_len
        mpsr.extend_from_slice(body);

        let mut wasm = b"\0asm\x01\x00\x00\x00".to_vec();
        let mut custom = Vec::new();
        let sec_name = b"wasmmod.source";
        custom.extend(uleb(sec_name.len() as u32));
        custom.extend_from_slice(sec_name);
        custom.extend_from_slice(&mpsr);
        wasm.push(0);
        wasm.extend(uleb(custom.len() as u32));
        wasm.extend(custom);

        let locs = locations_for_symbol(&wasm, "hello").unwrap();
        assert!(locs.iter().any(|l| {
            l.path == "src/hello.c" && l.line == Some(2) && l.role == "def"
        }));
        assert!(!locs.iter().any(|l| l.path == "src/hello.c" && l.line == Some(1)));
    }

    #[test]
    fn locations_scan_pack_py_twin() {
        let body = b"def hello():\n    return 1\n";
        let mut mpwp = Vec::new();
        mpwp.extend_from_slice(b"MPWP");
        mpwp.extend_from_slice(&3u16.to_le_bytes());
        mpwp.extend_from_slice(&0u16.to_le_bytes());
        let name = b"hello";
        mpwp.extend_from_slice(&(name.len() as u16).to_le_bytes());
        mpwp.extend_from_slice(name);
        mpwp.extend_from_slice(&1u32.to_le_bytes());
        let path = b"__init__.py";
        mpwp.extend_from_slice(&(path.len() as u16).to_le_bytes());
        mpwp.extend_from_slice(path);
        mpwp.push(1);
        mpwp.push(0);
        mpwp.extend_from_slice(&(body.len() as u32).to_le_bytes());
        mpwp.extend_from_slice(&(body.len() as u32).to_le_bytes());
        mpwp.extend_from_slice(body);

        let mut wasm = b"\0asm\x01\x00\x00\x00".to_vec();
        let mut custom = Vec::new();
        let sec_name = b"wasmmod.pack";
        custom.extend(uleb(sec_name.len() as u32));
        custom.extend_from_slice(sec_name);
        custom.extend_from_slice(&mpwp);
        wasm.push(0);
        wasm.extend(uleb(custom.len() as u32));
        wasm.extend(custom);

        let locs = locations_for_symbol(&wasm, "hello").unwrap();
        assert!(locs.iter().any(|l| {
            l.path == "__init__.py" && l.line == Some(1) && l.role == "twin"
        }));
    }
}
