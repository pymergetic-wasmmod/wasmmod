//! pymergetic.util.mtar — read-only iteration over a classic/ustar tar
//! archive already fully in memory. No writer, no filesystem, no streaming:
//! callers (a pack loader walking `wasmmod.tools` output, e.g.) already
//! have the whole blob mapped/loaded, they just need entry boundaries.
//!
//! Faces (mtar.types.rs / mtar.export.rs) are meant to be hand-written today
//! and machine-mirrored later; this file is the impl (`impl = "rs"`), see
//! mtar.pmm.toml.

#![allow(clippy::missing_safety_doc)]

const BLOCK: usize = 512;
const NAME_OFF: usize = 0;
const NAME_LEN: usize = 100;
const SIZE_OFF: usize = 124;
const SIZE_LEN: usize = 12;
const TYPEFLAG_OFF: usize = 156;
const CHKSUM_OFF: usize = 148;
const CHKSUM_LEN: usize = 8;

/// One entry's location + metadata inside the archive buffer. All offsets
/// are relative to the buffer `pm_util_mtar_first`/`pm_util_mtar_next` were called with —
/// no ownership, no allocation, this just describes a slice of it.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MtarEntry {
    pub name_ptr: *const u8,
    pub name_len: u32,
    pub data_ptr: *const u8,
    pub data_len: u32,
    pub is_dir: i32,
    /// Byte offset of this entry's 512-byte header block; `pm_util_mtar_next` needs
    /// it (plus data_len) to find the next header without re-scanning.
    pub header_off: u32,
}

pub const MTAR_OK: i32 = 0;
pub const MTAR_END: i32 = 1;
pub const MTAR_ERR_TRUNCATED: i32 = -1;
pub const MTAR_ERR_CHECKSUM: i32 = -2;
pub const MTAR_ERR_SIZE: i32 = -3;

/// Length-passing form: the subset compiler's slices carry no length, so
/// the caller states it (every caller slices a fixed-size header field,
/// so `len` is the field size by construction — same bytes, same loop).
fn parse_octal(field: &[u8], len: usize) -> Option<u64> {
    let mut val: u64 = 0;
    for k in 0..len {
        let b = field[k];
        match b {
            b'0'..=b'7' => val = val * 8 + (b - b'0') as u64,
            b' ' | 0 => break,
            _ => return None,
        }
    }
    Some(val)
}

fn header_checksum_ok(header: &[u8], len: usize) -> bool {
    // Per POSIX tar: sum of all header bytes as unsigned, with the
    // checksum field itself treated as eight ASCII spaces during the sum.
    let stored = match parse_octal(&header[CHKSUM_OFF..CHKSUM_OFF + CHKSUM_LEN], CHKSUM_LEN) {
        Some(v) => v,
        None => return false,
    };
    let mut sum: u64 = 0;
    for i in 0..len {
        let b = header[i];
        if (CHKSUM_OFF..CHKSUM_OFF + CHKSUM_LEN).contains(&i) {
            sum += b' ' as u64;
        } else {
            sum += b as u64;
        }
    }
    sum == stored
}

fn is_zero_block(block: &[u8], len: usize) -> bool {
    for i in 0..len {
        if block[i] != 0 {
            return false;
        }
    }
    true
}

/// Reads one entry's header at `offset`; on success also returns the
/// offset the *next* header would start at.
unsafe fn read_entry_at(buf: *const u8, buf_len: u32, offset: u32, out: *mut MtarEntry) -> i32 {
    /* no param shadowing: the subset's C lowering has one name per fn scope */
    let blen = buf_len as usize;
    let off = offset as usize;

    if off >= blen {
        return MTAR_END;
    }
    if off + BLOCK > blen {
        return MTAR_ERR_TRUNCATED;
    }

    let mut header = [0u8; BLOCK];
    // SAFETY: caller (pm_util_mtar_first/_next) guarantees `buf` points to
    // >= buf_len readable bytes, and offset + BLOCK <= buf_len was just
    // checked above.
    unsafe { core::ptr::copy_nonoverlapping(buf.add(off), header.as_mut_ptr(), BLOCK) };

    if is_zero_block(&header, BLOCK) {
        return MTAR_END;
    }
    if !header_checksum_ok(&header, BLOCK) {
        return MTAR_ERR_CHECKSUM;
    }

    /* explicit scan: the subset carries no slice lengths, and position()
     * over a slice field would need one — walk the fixed NAME_LEN window */
    let mut name_len: usize = NAME_LEN;
    for k in 0..NAME_LEN {
        if header[NAME_OFF + k] == 0 {
            name_len = k;
            break;
        }
    }

    let size = match parse_octal(&header[SIZE_OFF..SIZE_OFF + SIZE_LEN], SIZE_LEN) {
        Some(v) => v,
        None => return MTAR_ERR_SIZE,
    };
    let data_off = off + BLOCK;
    if data_off as u64 + size > blen as u64 {
        return MTAR_ERR_TRUNCATED;
    }

    let typeflag = header[TYPEFLAG_OFF];
    let is_dir = typeflag == b'5';

    // SAFETY: caller guarantees `out` points to one writable MtarEntry;
    // `buf.add(..)` stays within the buffer per the bounds checks above.
    unsafe {
        (*out).name_ptr = buf.add(off + NAME_OFF);
        (*out).name_len = name_len as u32;
        (*out).data_ptr = buf.add(data_off);
        (*out).data_len = size as u32;
        (*out).is_dir = is_dir as i32;
        (*out).header_off = off as u32;
    }

    MTAR_OK
}

/// First entry in the archive. Returns MTAR_OK / MTAR_END / MTAR_ERR_*.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_mtar_first(
    buf: *const u8,
    buf_len: u32,
    out: *mut MtarEntry,
) -> i32 {
    // SAFETY: forwarding the caller's own unsafe contract to read_entry_at.
    unsafe { read_entry_at(buf, buf_len, 0, out) }
}

/// Entry after `prev` (which must have come from `pm_util_mtar_first`/`pm_util_mtar_next`
/// on this same buffer). Returns MTAR_OK / MTAR_END / MTAR_ERR_*.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_mtar_next(
    buf: *const u8,
    buf_len: u32,
    prev: *const MtarEntry,
    out: *mut MtarEntry,
) -> i32 {
    // SAFETY: caller guarantees `prev` points to a valid, previously
    // populated MtarEntry; read_entry_at's contract is forwarded as-is.
    unsafe {
        let data_blocks = ((*prev).data_len as usize).div_ceil(BLOCK);
        let next_off = (*prev).header_off as usize + BLOCK + data_blocks * BLOCK;
        read_entry_at(buf, buf_len, next_off as u32, out)
    }
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.mtar",
    pm_util_mtar_first,
    "int32_t(const uint8_t *, uint32_t, MtarEntry *)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.mtar",
    pm_util_mtar_next,
    "int32_t(const uint8_t *, uint32_t, const MtarEntry *, MtarEntry *)"
);

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
