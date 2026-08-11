//! pymergetic.util.lz4 — minimal LZ4 block format (not the framed format):
//! sequences of [token][literal-length ext][literals][offset u16 LE]
//! [match-length ext], last sequence in a block is literals-only. No
//! dependency on the `lz4`/`lz4_flex` crates — small enough to own outright.
//!
//! Encoder is a plain greedy match finder (correctness over ratio); good
//! enough to self-test decode against real back-references without any
//! external tool. Faces (types + cbindgen mirror) are hand-written for now,
//! see lz4.export.h and lz4.pmm.toml.

#![allow(clippy::missing_safety_doc)]

use alloc::vec::Vec;

const MIN_MATCH: usize = 4;

pub const LZ4_OK: i32 = 0;
pub const LZ4_ERR_NOSPACE: i32 = -1;
pub const LZ4_ERR_DATA: i32 = -2;

/// Greedy LZ77 match find: longest match of `src[pos..]` against any
/// earlier position in `src[..pos]`, capped at u16 offset (LZ4 block
/// format's window limit) and MIN_MATCH minimum length to be worth coding.
fn find_match(src: &[u8], pos: usize) -> Option<(usize, usize)> {
    let max_off = 0xFFFF;
    let start = pos.saturating_sub(max_off);
    let mut best_len = 0usize;
    let mut best_off = 0usize;
    let mut cand = start;
    while cand < pos {
        let max_len = (src.len() - pos).min(src.len() - cand);
        let mut len = 0;
        while len < max_len && src[cand + len] == src[pos + len] {
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

fn push_length(out: &mut Vec<u8>, mut len: usize) {
    // Extension bytes for a length field already >= 15 (the token nibble's
    // max): 255 per byte until the remainder fits in one final byte.
    while len >= 255 {
        out.push(255);
        len -= 255;
    }
    out.push(len as u8);
}

/// Compresses one block (no frame header/trailer, no checksum). Returns
/// bytes written into `dst`, or LZ4_ERR_NOSPACE if it doesn't fit.
pub fn lz4_compress_block(src: &[u8], dst: &mut Vec<u8>) -> i32 {
    dst.clear();
    let mut pos = 0usize;
    let mut literal_start = 0usize;

    while pos < src.len() {
        // Last MIN_MATCH-1 bytes of a block can never start a match long
        // enough to be worth coding (nothing left to confirm 4+ bytes
        // against) — real LZ4 encoders also stop matching near block end.
        if pos + MIN_MATCH > src.len() {
            break;
        }
        if let Some((off, len)) = find_match(src, pos) {
            let lit_len = pos - literal_start;
            let token_lit = lit_len.min(15) as u8;
            let match_len_field = len - MIN_MATCH;
            let token_match = match_len_field.min(15) as u8;
            dst.push((token_lit << 4) | token_match);
            if lit_len >= 15 {
                push_length(dst, lit_len - 15);
            }
            dst.extend_from_slice(&src[literal_start..pos]);
            dst.push((off & 0xFF) as u8);
            dst.push(((off >> 8) & 0xFF) as u8);
            if match_len_field >= 15 {
                push_length(dst, match_len_field - 15);
            }
            pos += len;
            literal_start = pos;
        } else {
            pos += 1;
        }
    }

    // Final sequence: whatever literals remain, no match (block-format
    // requirement — the last sequence is always literals-only).
    let lit_len = src.len() - literal_start;
    let token_lit = lit_len.min(15) as u8;
    dst.push(token_lit << 4);
    if lit_len >= 15 {
        push_length(dst, lit_len - 15);
    }
    dst.extend_from_slice(&src[literal_start..]);

    dst.len() as i32
}

/// Decompresses one block into `dst` (must already be sized to the exact
/// expected output length, matching pack.py's "size is known up front"
/// framing — no growth, no streaming).
pub fn lz4_decompress_block(src: &[u8], dst: &mut [u8]) -> i32 {
    let mut ip = 0usize; // read cursor into src
    let mut op = 0usize; // write cursor into dst

    let read_ext_len = |ip: &mut usize| -> Option<usize> {
        let mut total = 0usize;
        loop {
            let b = *src.get(*ip)?;
            *ip += 1;
            total += b as usize;
            if b != 255 {
                break;
            }
        }
        Some(total)
    };

    while ip < src.len() {
        let token = src[ip];
        ip += 1;
        let mut lit_len = (token >> 4) as usize;
        if lit_len == 15 {
            lit_len += read_ext_len(&mut ip).unwrap_or(usize::MAX);
        }
        if ip + lit_len > src.len() || op + lit_len > dst.len() {
            return LZ4_ERR_DATA;
        }
        dst[op..op + lit_len].copy_from_slice(&src[ip..ip + lit_len]);
        ip += lit_len;
        op += lit_len;

        if ip >= src.len() {
            // Final sequence: literals only, no offset/match-length follow.
            break;
        }
        if ip + 2 > src.len() {
            return LZ4_ERR_DATA;
        }
        let off = src[ip] as usize | ((src[ip + 1] as usize) << 8);
        ip += 2;
        if off == 0 || off > op {
            return LZ4_ERR_DATA;
        }
        let base = (token & 0x0F) as usize;
        let mut match_len = base + MIN_MATCH;
        if base == 15 {
            match_len += read_ext_len(&mut ip).unwrap_or(usize::MAX);
        }
        if op + match_len > dst.len() {
            return LZ4_ERR_DATA;
        }
        let match_src = op - off;
        for i in 0..match_len {
            dst[op + i] = dst[match_src + i];
        }
        op += match_len;
    }

    if op == dst.len() {
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
    unsafe {
        let src = core::slice::from_raw_parts(src, src_len as usize);
        let mut out = Vec::new();
        let n = lz4_compress_block(src, &mut out);
        if n < 0 || n as u32 > dst_cap {
            return LZ4_ERR_NOSPACE;
        }
        core::ptr::copy_nonoverlapping(out.as_ptr(), dst, n as usize);
        n
    }
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
    unsafe {
        let src = core::slice::from_raw_parts(src, src_len as usize);
        let dst = core::slice::from_raw_parts_mut(dst, dst_len as usize);
        lz4_decompress_block(src, dst)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn roundtrip(data: &[u8]) {
        let mut compressed = Vec::new();
        let n = lz4_compress_block(data, &mut compressed);
        assert!(n >= 0);
        compressed.truncate(n as usize);

        let mut decompressed = vec![0u8; data.len()];
        let rc = lz4_decompress_block(&compressed, &mut decompressed);
        assert_eq!(rc, LZ4_OK, "decode failed for len={}", data.len());
        assert_eq!(decompressed, data);
    }

    #[test]
    fn literals_only_short_input() {
        roundtrip(b"hi");
    }

    #[test]
    fn repetitive_input_uses_real_matches() {
        let data = b"abcdabcdabcdabcdabcdabcdabcdabcd".to_vec();
        let mut compressed = Vec::new();
        let n = lz4_compress_block(&data, &mut compressed);
        assert!(n > 0);
        assert!((n as usize) < data.len(), "should actually compress repetitive input");
        roundtrip(&data);
    }

    #[test]
    fn long_random_ish_input() {
        let mut data = Vec::new();
        let mut x: u32 = 12345;
        for _ in 0..5000 {
            x = x.wrapping_mul(1103515245).wrapping_add(12345);
            data.push((x >> 16) as u8);
        }
        // Splice in a repeated run so the match path is exercised too.
        let chunk: Vec<u8> = data[100..200].to_vec();
        data.extend_from_slice(&chunk);
        roundtrip(&data);
    }

    #[test]
    fn empty_input() {
        roundtrip(b"");
    }

    #[test]
    fn c_abi_roundtrip() {
        let data = b"the quick brown fox the quick brown fox the quick brown fox".to_vec();
        let mut compressed = vec![0u8; 256];
        let clen = unsafe { pm_util_lz4_compress(data.as_ptr(), data.len() as u32, compressed.as_mut_ptr(), compressed.len() as u32) };
        assert!(clen > 0);
        let mut decompressed = vec![0u8; data.len()];
        let rc = unsafe {
            pm_util_lz4_decompress(compressed.as_ptr(), clen as u32, decompressed.as_mut_ptr(), decompressed.len() as u32)
        };
        assert_eq!(rc, LZ4_OK);
        assert_eq!(decompressed, data);
    }
}
