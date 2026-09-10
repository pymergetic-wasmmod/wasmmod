//! pymergetic.util.lz4 — minimal LZ4 block format (not the framed format):
//! sequences of [token][literal-length ext][literals][offset u16 LE]
//! [match-length ext], last sequence in a block is literals-only. No
//! dependency on the `lz4`/`lz4_flex` crates — small enough to own outright.
//!
//! Encoder is a plain greedy match finder (correctness over ratio); good
//! enough to self-test decode against real back-references without any
//! external tool. Faces (types + cbindgen mirror) are hand-written for now,
//! see lz4.export.h and lz4.pmm.toml.
//!
//! Kernel-discipline: ptr+len pairs everywhere (the kernel's rsx subset
//! carries no slice lengths), no allocator — compression writes straight
//! into the caller's dst buffer.

#![allow(clippy::missing_safety_doc)]

const MIN_MATCH: usize = 4;

pub const LZ4_OK: i32 = 0;
pub const LZ4_ERR_NOSPACE: i32 = -1;
pub const LZ4_ERR_DATA: i32 = -2;

/// Greedy LZ77 match find: longest match of `src[pos..]` against any
/// earlier position in `src[..pos]`, capped at u16 offset (LZ4 block
/// format's window limit) and MIN_MATCH minimum length to be worth coding.
/// Returns (offset, length), or None when no match of MIN_MATCH exists.
fn find_match(src: *const u8, src_len: usize, pos: usize) -> Option<(usize, usize)> {
    let max_off = 0xFFFF;
    let start: usize = if pos > max_off { pos - max_off } else { 0 };
    let mut best_len = 0usize;
    let mut best_off = 0usize;
    let mut cand = start;
    while cand < pos {
        let mut len = 0usize;
        while len < src_len - pos && len < src_len - cand {
            unsafe {
                if *src.add(cand + len) != *src.add(pos + len) {
                    break;
                }
            }
            len += 1;
        }
        if len > best_len {
            best_len = len;
            best_off = pos - cand;
        }
        cand += 1;
    }
    if best_len >= MIN_MATCH {
        Some((best_off, best_len))
    } else {
        None
    }
}

/// Extension bytes for a length field already >= 15 (the token nibble's
/// max): 255 per byte until the remainder fits in one final byte. Returns
/// the total, or None when the read runs off the end of src.
fn read_ext_len(src: *const u8, src_len: usize, ip: &mut usize) -> Option<usize> {
    let mut total = 0usize;
    loop {
        if *ip >= src_len {
            return None;
        }
        let b = unsafe { *src.add(*ip) } as usize;
        *ip += 1;
        total += b;
        if b != 255 {
            break;
        }
    }
    Some(total)
}

/// Compresses one block (no frame header/trailer, no checksum) straight
/// into `dst[0..dst_cap]`. Returns bytes written, LZ4_ERR_NOSPACE when the
/// output does not fit.
pub fn lz4_compress_block(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_cap: usize,
) -> i32 {
    let mut pos = 0usize;
    let mut literal_start = 0usize;
    let mut op = 0usize;

    while pos < src_len {
        // Last MIN_MATCH-1 bytes of a block can never start a match long
        // enough to be worth coding (nothing left to confirm 4+ bytes
        // against) — real LZ4 encoders also stop matching near block end.
        if pos + MIN_MATCH > src_len {
            break;
        }
        let m = find_match(src, src_len, pos);
        if let Some((off, len)) = m {
            let lit_len = pos - literal_start;
            let token_lit: usize = if lit_len < 15 { lit_len } else { 15 };
            let match_len_field = len - MIN_MATCH;
            let token_match: usize = if match_len_field < 15 { match_len_field } else { 15 };
            if op >= dst_cap {
                return LZ4_ERR_NOSPACE;
            }
            unsafe {
                *dst.add(op) = ((token_lit << 4) | token_match) as u8;
            }
            op += 1;
            if lit_len >= 15 {
                let mut rest = lit_len - 15;
                while rest >= 255 {
                    if op >= dst_cap {
                        return LZ4_ERR_NOSPACE;
                    }
                    unsafe {
                        *dst.add(op) = 255;
                    }
                    op += 1;
                    rest -= 255;
                }
                if op >= dst_cap {
                    return LZ4_ERR_NOSPACE;
                }
                unsafe {
                    *dst.add(op) = rest as u8;
                }
                op += 1;
            }
            let mut i = literal_start;
            while i < pos {
                if op >= dst_cap {
                    return LZ4_ERR_NOSPACE;
                }
                unsafe {
                    *dst.add(op) = *src.add(i);
                }
                op += 1;
                i += 1;
            }
            if op >= dst_cap {
                return LZ4_ERR_NOSPACE;
            }
            unsafe {
                *dst.add(op) = (off & 0xFF) as u8;
            }
            op += 1;
            if op >= dst_cap {
                return LZ4_ERR_NOSPACE;
            }
            unsafe {
                *dst.add(op) = ((off >> 8) & 0xFF) as u8;
            }
            op += 1;
            if match_len_field >= 15 {
                let mut rest = match_len_field - 15;
                while rest >= 255 {
                    if op >= dst_cap {
                        return LZ4_ERR_NOSPACE;
                    }
                    unsafe {
                        *dst.add(op) = 255;
                    }
                    op += 1;
                    rest -= 255;
                }
                if op >= dst_cap {
                    return LZ4_ERR_NOSPACE;
                }
                unsafe {
                    *dst.add(op) = rest as u8;
                }
                op += 1;
            }
            pos += len;
            literal_start = pos;
        } else {
            pos += 1;
        }
    }

    // Final sequence: whatever literals remain, no match (block-format
    // requirement — the last sequence is always literals-only).
    let lit_len = src_len - literal_start;
    let token_lit: usize = if lit_len < 15 { lit_len } else { 15 };
    if op >= dst_cap {
        return LZ4_ERR_NOSPACE;
    }
    unsafe {
        *dst.add(op) = (token_lit << 4) as u8;
    }
    op += 1;
    if lit_len >= 15 {
        let mut rest = lit_len - 15;
        while rest >= 255 {
            if op >= dst_cap {
                return LZ4_ERR_NOSPACE;
            }
            unsafe {
                *dst.add(op) = 255;
            }
            op += 1;
            rest -= 255;
        }
        if op >= dst_cap {
            return LZ4_ERR_NOSPACE;
        }
        unsafe {
            *dst.add(op) = rest as u8;
        }
        op += 1;
    }
    let mut i = literal_start;
    while i < src_len {
        if op >= dst_cap {
            return LZ4_ERR_NOSPACE;
        }
        unsafe {
            *dst.add(op) = *src.add(i);
        }
        op += 1;
        i += 1;
    }

    op as i32
}

/// Decompresses one block into `dst[0..dst_len]` (sized to the exact
/// expected output length, matching pack.py's "size is known up front"
/// framing — no growth, no streaming). Returns LZ4_OK or LZ4_ERR_DATA.
pub fn lz4_decompress_block(
    src: *const u8,
    src_len: usize,
    dst: *mut u8,
    dst_len: usize,
) -> i32 {
    let mut ip = 0usize; // read cursor into src
    let mut op = 0usize; // write cursor into dst

    while ip < src_len {
        let token = unsafe { *src.add(ip) } as usize;
        ip += 1;
        let mut lit_len = token >> 4;
        if lit_len == 15 {
            match read_ext_len(src, src_len, &mut ip) {
                Some(v) => lit_len += v,
                None => return LZ4_ERR_DATA,
            }
        }
        if ip + lit_len > src_len || op + lit_len > dst_len {
            return LZ4_ERR_DATA;
        }
        unsafe {
            core::ptr::copy_nonoverlapping(src.add(ip), dst.add(op), lit_len);
        }
        ip += lit_len;
        op += lit_len;

        if ip >= src_len {
            // Final sequence: literals only, no offset/match-length follow.
            break;
        }
        if ip + 2 > src_len {
            return LZ4_ERR_DATA;
        }
        let off = unsafe { *src.add(ip) } as usize | ((unsafe { *src.add(ip + 1) } as usize) << 8);
        ip += 2;
        if off == 0 || off > op {
            return LZ4_ERR_DATA;
        }
        let base = token & 0x0F;
        let mut match_len = base + MIN_MATCH;
        if base == 15 {
            match read_ext_len(src, src_len, &mut ip) {
                Some(v) => match_len += v,
                None => return LZ4_ERR_DATA,
            }
        }
        if op + match_len > dst_len {
            return LZ4_ERR_DATA;
        }
        // Byte-at-a-time: overlapping matches (off < match_len) are the
        // LZ4 norm — memcpy would read bytes not yet written.
        let match_src = op - off;
        let mut i = 0usize;
        while i < match_len {
            unsafe {
                *dst.add(op + i) = *dst.add(match_src + i);
            }
            i += 1;
        }
        op += match_len;
    }

    if op == dst_len {
        LZ4_OK
    } else {
        LZ4_ERR_DATA
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_lz4_compress(
    src: *const u8,
    src_len: u32,
    dst: *mut u8,
    dst_cap: u32,
) -> i32 {
    // SAFETY: caller guarantees `src` points to >= src_len readable bytes
    // and `dst` to >= dst_cap writable bytes.
    lz4_compress_block(src, src_len as usize, dst, dst_cap as usize)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn pm_util_lz4_decompress(
    src: *const u8,
    src_len: u32,
    dst: *mut u8,
    dst_len: u32,
) -> i32 {
    // SAFETY: caller guarantees `src` points to >= src_len readable bytes
    // and `dst` to >= dst_len writable bytes.
    lz4_decompress_block(src, src_len as usize, dst, dst_len as usize)
}

/* Same table as PM_MOD_EXPORT_C — not a second registration system. */
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.lz4",
    pm_util_lz4_compress,
    "int32_t(const uint8_t *, uint32_t, uint8_t *, uint32_t)"
);
crate::PM_MOD_EXPORT_RS!(
    "pymergetic.util.lz4",
    pm_util_lz4_decompress,
    "int32_t(const uint8_t *, uint32_t, uint8_t *, uint32_t)"
);

#[cfg(test)]
#[path = "__tests__.rs"]
mod __tests__;
