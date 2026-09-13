//! pymergetic.util.zlib tests — compress/decompress round-trip.

#[test]
fn roundtrip_small() {
    let input = b"hello wasmmod zlib";
    let mut comp = vec![0u8; 256];
    let comp_len = pm_util_zlib_compress(&input[..], &mut comp);
    assert!(comp_len > 0);
    let mut decomp = vec![0u8; 256];
    let decomp_len = pm_util_zlib_decompress(&comp[..comp_len as usize], &mut decomp);
    assert_eq!(decomp_len, input.len() as i32);
    assert_eq!(&decomp[..decomp_len as usize], &input[..]);
}

#[test]
fn roundtrip_empty() {
    let input: &[u8] = &[];
    let mut comp = vec![0u8; 64];
    let comp_len = pm_util_zlib_compress(input, &mut comp);
    assert!(comp_len >= 0);
}