//! pymergetic.util.lz4 — module tests (`__tests__.rs`).
//!
//! Same ptr+len discipline as the impl: buffers are stack arrays, no
//! allocator — decompression output is sized up front exactly the way
//! pack.py frames it. Slice params are kept for engine-seat ergonomics,
//! but every length that flows into the ABI is a named `usize` the
//! kernel's rsx subset can see.

use super::*;
use crate::util::mod_test::case;

const RND_X0: u32 = 12345;
const COMP_CAP: usize = 8192;
const DECOMP_CAP: usize = 8192;

/// LCG bytes for the long-input case: deterministic, no Vec — fill a
/// stack buffer through its raw pointer, the loop discipline the impl
/// itself uses.
fn fill_lcg(buf: &mut [u8]) {
    let n: usize = buf.len();
    let p = buf.as_mut_ptr();
    let mut x: u32 = RND_X0;
    let mut i = 0usize;
    while i < n {
        x = x.wrapping_mul(1103515245).wrapping_add(12345);
        unsafe { *p.add(i) = (x >> 16) as u8 };
        i += 1;
    }
}

fn roundtrip(data: &[u8]) {
    let data_len: usize = data.len();
    let mut compressed = [0u8; COMP_CAP];
    let n: i32 = lz4_compress_block(data.as_ptr(), data_len, compressed.as_mut_ptr(), COMP_CAP);
    assert!(n >= 0);
    let clen: usize = n as usize;

    let mut decompressed = [0u8; DECOMP_CAP];
    let rc = lz4_decompress_block(compressed.as_ptr(), clen, decompressed.as_mut_ptr(), data_len);
    assert_eq!(rc, LZ4_OK, "decode failed for len={}", data_len);
    assert_eq!(&decompressed[..data_len], data);
}

fn literals_only_short_input() {
    roundtrip(b"hi");
}

fn repetitive_input_uses_real_matches() {
    let data: &[u8] = b"abcdabcdabcdabcdabcdabcdabcdabcd";
    let data_len: usize = data.len();
    let mut compressed = [0u8; 256];
    let n: i32 = lz4_compress_block(data.as_ptr(), data_len, compressed.as_mut_ptr(), 256);
    assert!(n > 0);
    assert!((n as usize) < data_len, "should actually compress repetitive input");
    roundtrip(data);
}

fn long_random_ish_input() {
    let mut data = [0u8; 5000];
    fill_lcg(&mut data);
    // Splice in a repeated run so the match path is exercised too:
    // copy data[100..200] to data[4000..4100] byte-wise (ptr+len, no tools).
    let src = data.as_ptr();
    let dst = data.as_mut_ptr();
    let mut i = 0usize;
    while i < 100 {
        unsafe { *dst.add(4000 + i) = *src.add(100 + i) };
        i += 1;
    }
    roundtrip(&data);
}

fn empty_input() {
    roundtrip(b"");
}

fn nospace_is_reported() {
    let data: &[u8] = b"0123456789abcdefghij"; // incompressible-ish, 20 bytes
    let mut tiny = [0u8; 4];
    let n: i32 = lz4_compress_block(data.as_ptr(), 20, tiny.as_mut_ptr(), 4);
    assert_eq!(n, LZ4_ERR_NOSPACE);
}

fn corrupt_input_is_refused() {
    let data: &[u8] = b"abcdabcdabcdabcdabcdabcdabcdabcd";
    let data_len: usize = data.len();
    let mut compressed = [0u8; 256];
    let n: i32 = lz4_compress_block(data.as_ptr(), data_len, compressed.as_mut_ptr(), 256);
    assert!(n > 0);
    let clen: usize = n as usize;

    // Truncated stream (half the bytes) must refuse, not loop or panic.
    let mut decompressed = [0u8; DECOMP_CAP];
    let rc = lz4_decompress_block(compressed.as_ptr(), clen / 2, decompressed.as_mut_ptr(), data_len);
    assert_eq!(rc, LZ4_ERR_DATA);
}

fn c_abi_roundtrip() {
    let data: &[u8] = b"the quick brown fox the quick brown fox the quick brown fox";
    let data_len: usize = data.len();
    let mut compressed = [0u8; 256];
    let clen: i32 = unsafe { pm_util_lz4_compress(data.as_ptr(), data_len as u32, compressed.as_mut_ptr(), 256) };
    assert!(clen > 0);
    let mut decompressed = [0u8; DECOMP_CAP];
    let rc = unsafe {
        pm_util_lz4_decompress(compressed.as_ptr(), clen as u32, decompressed.as_mut_ptr(), data_len as u32)
    };
    assert_eq!(rc, LZ4_OK);
    assert_eq!(&decompressed[..data_len], data);
}

unsafe extern "C" fn case_literals_only_short_input() -> i32 {
    case(|| literals_only_short_input())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "literals_only_short_input", case_literals_only_short_input);
#[test]
fn test_literals_only_short_input() {
    assert_eq!(unsafe { case_literals_only_short_input() }, 0);
}

unsafe extern "C" fn case_repetitive_input_uses_real_matches() -> i32 {
    case(|| repetitive_input_uses_real_matches())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "repetitive_input_uses_real_matches", case_repetitive_input_uses_real_matches);
#[test]
fn test_repetitive_input_uses_real_matches() {
    assert_eq!(unsafe { case_repetitive_input_uses_real_matches() }, 0);
}

unsafe extern "C" fn case_long_random_ish_input() -> i32 {
    case(|| long_random_ish_input())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "long_random_ish_input", case_long_random_ish_input);
#[test]
fn test_long_random_ish_input() {
    assert_eq!(unsafe { case_long_random_ish_input() }, 0);
}

unsafe extern "C" fn case_empty_input() -> i32 {
    case(|| empty_input())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "empty_input", case_empty_input);
#[test]
fn test_empty_input() {
    assert_eq!(unsafe { case_empty_input() }, 0);
}

unsafe extern "C" fn case_nospace_is_reported() -> i32 {
    case(|| nospace_is_reported())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "nospace_is_reported", case_nospace_is_reported);
#[test]
fn test_nospace_is_reported() {
    assert_eq!(unsafe { case_nospace_is_reported() }, 0);
}

unsafe extern "C" fn case_corrupt_input_is_refused() -> i32 {
    case(|| corrupt_input_is_refused())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "corrupt_input_is_refused", case_corrupt_input_is_refused);
#[test]
fn test_corrupt_input_is_refused() {
    assert_eq!(unsafe { case_corrupt_input_is_refused() }, 0);
}

unsafe extern "C" fn case_c_abi_roundtrip() -> i32 {
    case(|| c_abi_roundtrip())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "c_abi_roundtrip", case_c_abi_roundtrip);
#[test]
fn test_c_abi_roundtrip() {
    assert_eq!(unsafe { case_c_abi_roundtrip() }, 0);
}
