//! pymergetic.util.lz4 — module tests (`__tests__.rs`).

use super::*;
use crate::util::mod_test::case;

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

fn literals_only_short_input() {
    roundtrip(b"hi");
}

fn repetitive_input_uses_real_matches() {
    let data = b"abcdabcdabcdabcdabcdabcdabcdabcd".to_vec();
    let mut compressed = Vec::new();
    let n = lz4_compress_block(&data, &mut compressed);
    assert!(n > 0);
    assert!((n as usize) < data.len(), "should actually compress repetitive input");
    roundtrip(&data);
}

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

fn empty_input() {
    roundtrip(b"");
}

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

unsafe extern "C" fn case_c_abi_roundtrip() -> i32 {
    case(|| c_abi_roundtrip())
}
crate::PM_MOD_TEST_RS!("pymergetic.util.lz4", "c_abi_roundtrip", case_c_abi_roundtrip);
#[test]
fn test_c_abi_roundtrip() {
    assert_eq!(unsafe { case_c_abi_roundtrip() }, 0);
}

