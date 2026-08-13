//! Gen emit sinks — where face bytes go (Fs / Vfs / Mem).
//!
//! Core of the cell-build emit plane: `util.gen` must not assume POSIX disk
//! is the only destination. See `docs/CELLBUILD.md`.

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;
use core::ffi::c_void;

/// Read/write target for generated face artifacts.
pub trait GenSink {
    /// `Ok(None)` = path absent. `Ok(Some(bytes))` = current contents.
    fn read(&mut self, path: &str) -> Result<Option<Vec<u8>>, String>;
    fn write(&mut self, path: &str, data: &[u8]) -> Result<(), String>;
}

/// In-memory tree — compare, JIT input, or packing without a real path.
#[derive(Clone, Debug, Default)]
pub struct MemSink {
    pub files: BTreeMap<String, Vec<u8>>,
}

impl MemSink {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn insert(&mut self, path: &str, data: impl AsRef<[u8]>) {
        self.files
            .insert(String::from(path), data.as_ref().to_vec());
    }

    pub fn get(&self, path: &str) -> Option<&[u8]> {
        self.files.get(path).map(|v| v.as_slice())
    }
}

impl GenSink for MemSink {
    fn read(&mut self, path: &str) -> Result<Option<Vec<u8>>, String> {
        Ok(self.files.get(path).cloned())
    }

    fn write(&mut self, path: &str, data: &[u8]) -> Result<(), String> {
        self.files.insert(String::from(path), data.to_vec());
        Ok(())
    }
}

/// Host µPy / guest VFS ops table (C ABI). Paths are UTF-8.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct VfsOps {
    /// Return 1 = ok, 0 = missing, -1 = error.
    /// If `buf` is null, write required size into `*inout_len` when present.
    pub read: Option<
        unsafe extern "C" fn(
            ctx: *mut c_void,
            path: *const u8,
            path_len: u32,
            buf: *mut u8,
            inout_len: *mut u32,
        ) -> i32,
    >,
    /// Return 0 = ok, -1 = error.
    pub write: Option<
        unsafe extern "C" fn(
            ctx: *mut c_void,
            path: *const u8,
            path_len: u32,
            data: *const u8,
            data_len: u32,
        ) -> i32,
    >,
}

/// VFS / virtual-tree sink — in-bin or browser µPy-wasm.
pub struct VfsSink {
    pub ctx: *mut c_void,
    pub ops: VfsOps,
}

impl GenSink for VfsSink {
    fn read(&mut self, path: &str) -> Result<Option<Vec<u8>>, String> {
        let Some(read) = self.ops.read else {
            return Err(String::from("vfs sink: no read op"));
        };
        let mut need = 0u32;
        let st = unsafe {
            read(
                self.ctx,
                path.as_ptr(),
                path.len() as u32,
                core::ptr::null_mut(),
                &mut need,
            )
        };
        if st == 0 {
            return Ok(None);
        }
        if st < 0 {
            return Err(String::from("vfs sink: read size failed"));
        }
        let mut buf = alloc::vec![0u8; need as usize];
        let mut n = need;
        let st = unsafe {
            read(
                self.ctx,
                path.as_ptr(),
                path.len() as u32,
                buf.as_mut_ptr(),
                &mut n,
            )
        };
        if st == 0 {
            return Ok(None);
        }
        if st < 0 {
            return Err(String::from("vfs sink: read failed"));
        }
        buf.truncate(n as usize);
        Ok(Some(buf))
    }

    fn write(&mut self, path: &str, data: &[u8]) -> Result<(), String> {
        let Some(write) = self.ops.write else {
            return Err(String::from("vfs sink: no write op"));
        };
        let st = unsafe {
            write(
                self.ctx,
                path.as_ptr(),
                path.len() as u32,
                data.as_ptr(),
                data.len() as u32,
            )
        };
        if st != 0 {
            return Err(String::from("vfs sink: write failed"));
        }
        Ok(())
    }
}

/// Host POSIX filesystem sink (feature `gen` / unix product).
#[cfg(feature = "gen")]
pub struct FsSink;

#[cfg(feature = "gen")]
impl GenSink for FsSink {
    fn read(&mut self, path: &str) -> Result<Option<Vec<u8>>, String> {
        match std::fs::read(path) {
            Ok(b) => Ok(Some(b)),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(e.to_string()),
        }
    }

    fn write(&mut self, path: &str, data: &[u8]) -> Result<(), String> {
        let p = std::path::Path::new(path);
        if let Some(parent) = p.parent() {
            std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
        }
        std::fs::write(p, data).map_err(|e| e.to_string())
    }
}

/// Result of applying one generated face to a sink.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ApplyOutcome {
    Unchanged,
    Wrote,
    Drift,
    Missing,
}

/// Write or check one face path against `sink`.
pub fn apply_one(
    sink: &mut dyn GenSink,
    path: &str,
    content: &str,
    check_only: bool,
) -> Result<ApplyOutcome, String> {
    let want = content.as_bytes();
    match sink.read(path)? {
        Some(old) if old == want => Ok(ApplyOutcome::Unchanged),
        Some(_) if check_only => Ok(ApplyOutcome::Drift),
        None if check_only => Ok(ApplyOutcome::Missing),
        _ => {
            sink.write(path, want)?;
            Ok(ApplyOutcome::Wrote)
        }
    }
}

/// Apply many `(path, content)` faces. Returns `true` if any drift/missing under check.
pub fn apply_faces(
    sink: &mut dyn GenSink,
    faces: &[(String, String)],
    check_only: bool,
) -> Result<bool, String> {
    let mut drift = false;
    for (path, content) in faces {
        match apply_one(sink, path, content, check_only)? {
            ApplyOutcome::Drift | ApplyOutcome::Missing => drift = true,
            ApplyOutcome::Unchanged | ApplyOutcome::Wrote => {}
        }
    }
    Ok(drift)
}

/// Direct byte compare (no sink). `None` baseline skips that face.
pub fn diff_bytes(generated: &str, included: Option<&[u8]>) -> ApplyOutcome {
    match included {
        None => ApplyOutcome::Unchanged, // skipped
        Some(b) if b == generated.as_bytes() => ApplyOutcome::Unchanged,
        Some(_) => ApplyOutcome::Drift,
    }
}
