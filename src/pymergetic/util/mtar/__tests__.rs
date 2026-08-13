//! pymergetic.util.mtar — module tests (`__tests__.rs`).

use super::*;
use crate::util::mod_test::case;

/// Test-only: builds one valid ustar-ish header block. Not part of the
/// public module surface — mtar is a reader, this just gives the
/// reader something real to chew on without needing an external `tar`.
fn write_header(name: &str, size: usize, typeflag: u8) -> [u8; BLOCK] {
    let mut h = [0u8; BLOCK];
    h[NAME_OFF..NAME_OFF + name.len()].copy_from_slice(name.as_bytes());
    let size_oct = format!("{:011o}\0", size);
    h[SIZE_OFF..SIZE_OFF + size_oct.len()].copy_from_slice(size_oct.as_bytes());
    h[TYPEFLAG_OFF] = typeflag;
    for b in h.iter_mut().skip(CHKSUM_OFF).take(CHKSUM_LEN) {
        *b = b' ';
    }
    let mut sum: u64 = 0;
    for &b in h.iter() {
        sum += b as u64;
    }
    let chk = format!("{:06o}\0 ", sum);
    h[CHKSUM_OFF..CHKSUM_OFF + chk.len()].copy_from_slice(chk.as_bytes());
    h
}

fn pad_to_block(buf: &mut Vec<u8>) {
    let rem = buf.len() % BLOCK;
    if rem != 0 {
        buf.extend(std::iter::repeat_n(0u8, BLOCK - rem));
    }
}

fn build_archive(entries: &[(&str, &[u8])]) -> Vec<u8> {
    let mut buf = Vec::new();
    for (name, data) in entries {
        buf.extend_from_slice(&write_header(name, data.len(), b'0'));
        buf.extend_from_slice(data);
        pad_to_block(&mut buf);
    }
    buf.extend(std::iter::repeat_n(0u8, BLOCK * 2)); // end-of-archive marker
    buf
}

fn iterates_two_entries() {
    let archive = build_archive(&[
        ("hello.txt", b"hello world"),
        ("dir/nested.bin", &[0xAAu8; 700]), // spans two data blocks
    ]);

    unsafe {
        let mut e = MtarEntry {
            name_ptr: core::ptr::null(),
            name_len: 0,
            data_ptr: core::ptr::null(),
            data_len: 0,
            is_dir: 0,
            header_off: 0,
        };

        let rc = pm_util_mtar_first(archive.as_ptr(), archive.len() as u32, &mut e);
        assert_eq!(rc, MTAR_OK);
        let name = core::slice::from_raw_parts(e.name_ptr, e.name_len as usize);
        assert_eq!(name, b"hello.txt");
        let data = core::slice::from_raw_parts(e.data_ptr, e.data_len as usize);
        assert_eq!(data, b"hello world");
        assert_eq!(e.is_dir, 0);

        let rc = pm_util_mtar_next(archive.as_ptr(), archive.len() as u32, &e, &mut e);
        assert_eq!(rc, MTAR_OK);
        let name = core::slice::from_raw_parts(e.name_ptr, e.name_len as usize);
        assert_eq!(name, b"dir/nested.bin");
        assert_eq!(e.data_len, 700);
        let data = core::slice::from_raw_parts(e.data_ptr, e.data_len as usize);
        assert!(data.iter().all(|&b| b == 0xAA));

        let rc = pm_util_mtar_next(archive.as_ptr(), archive.len() as u32, &e, &mut e);
        assert_eq!(rc, MTAR_END);
    }
}

fn rejects_bad_checksum() {
    let mut archive = build_archive(&[("x", b"y")]);
    archive[CHKSUM_OFF] = b'9'; // corrupt the checksum field
    unsafe {
        let mut e = core::mem::zeroed();
        let rc = pm_util_mtar_first(archive.as_ptr(), archive.len() as u32, &mut e);
        assert_eq!(rc, MTAR_ERR_CHECKSUM);
    }
}

fn empty_archive_ends_immediately() {
    let archive = [0u8; BLOCK * 2];
    unsafe {
        let mut e = core::mem::zeroed();
        let rc = pm_util_mtar_first(archive.as_ptr(), archive.len() as u32, &mut e);
        assert_eq!(rc, MTAR_END);
    }
}

unsafe extern "C" fn case_iterates_two_entries() -> i32 {
    case(|| iterates_two_entries())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.mtar",
    "iterates_two_entries",
    case_iterates_two_entries
);
#[test]
fn test_iterates_two_entries() {
    assert_eq!(unsafe { case_iterates_two_entries() }, 0);
}

unsafe extern "C" fn case_rejects_bad_checksum() -> i32 {
    case(|| rejects_bad_checksum())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.mtar",
    "rejects_bad_checksum",
    case_rejects_bad_checksum
);
#[test]
fn test_rejects_bad_checksum() {
    assert_eq!(unsafe { case_rejects_bad_checksum() }, 0);
}

unsafe extern "C" fn case_empty_archive_ends_immediately() -> i32 {
    case(|| empty_archive_ends_immediately())
}
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.mtar",
    "empty_archive_ends_immediately",
    case_empty_archive_ends_immediately
);
#[test]
fn test_empty_archive_ends_immediately() {
    assert_eq!(unsafe { case_empty_archive_ends_immediately() }, 0);
}
