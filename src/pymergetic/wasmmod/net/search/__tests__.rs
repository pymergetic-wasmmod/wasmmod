// pymergetic.wasmmod.net.search — unit tests. These prove the JSON parsing /
// matching semantics without a live CDN: parse_and_query is pure input→rows,
// so a hardcoded index exercises catalog/search/filter exactly as a fetch
// would. The result-set ABI (count/name_at/meta_at) is tested against a query
// injected by calling parse directly — no network.

use super::{
    parse_and_query, reset_ok, Query, STATE,
};

const INDEX: &str = r#"{"schema":1,"channel":"lead","generated":"2026-08-04T00:00:00Z","packages":{
  "zlib":{"version":"1.2.12","artifacts":[{"kind":"wasm","arch":"x86_64","encoding":"zlib"},{"kind":"elf","arch":"x86","encoding":"zlib"}]},
  "pymergetic.metal.async":{"version":"0.1.0","artifacts":[{"kind":"elf","arch":"aarch64","encoding":"zlib"}]},
  "hello":{"version":"0.2.1","description":"sample","artifacts":[{"kind":"aot","arch":"x86_64","encoding":"zlib"}]},
  "WasmKit":{"version":"1.0.0","artifacts":[{"kind":"wasm","arch":"wasm32","encoding":"direct"}]}
}}"#;

fn names(q: &Query) -> Vec<String> {
    let rows = parse_and_query(INDEX.as_bytes(), q).expect("parse ok");
    rows.iter()
        .map(|(n, _)| core::str::from_utf8(n).unwrap().to_string())
        .collect()
}

#[test]
fn catalog_lists_every_pack_name_sorted() {
    assert_eq!(
        names(&Query::default()),
        vec![
            "WasmKit".to_string(),
            "hello".to_string(),
            "pymergetic.metal.async".to_string(),
            "zlib".to_string(),
        ]
    );
}

#[test]
fn search_matches_name_substring_case_insensitive() {
    let q = Query {
        q: Some("wasm".into()),
        ..Default::default()
    };
    assert_eq!(names(&q), vec!["WasmKit".to_string()]);
    let q = Query {
        q: Some("metal".into()),
        ..Default::default()
    };
    assert_eq!(names(&q), vec!["pymergetic.metal.async".to_string()]);
}

#[test]
fn filter_by_prefix() {
    let q = Query {
        prefix: Some("pymergetic.".into()),
        ..Default::default()
    };
    assert_eq!(names(&q), vec!["pymergetic.metal.async".to_string()]);
}

#[test]
fn filter_by_kind_and_arch_across_artifacts() {
    // x86_64 wasm (zlib) and aot (hello) both match arch; kind narrows.
    let q = Query {
        kind: Some("wasm".into()),
        arch: Some("x86_64".into()),
        ..Default::default()
    };
    assert_eq!(names(&q), vec!["zlib".to_string()]);
    // aarch64 elf only.
    let q = Query {
        kind: Some("elf".into()),
        arch: Some("aarch64".into()),
        ..Default::default()
    };
    assert_eq!(names(&q), vec!["pymergetic.metal.async".to_string()]);
    // Kind alone: anything with a wasm artifact.
    let q = Query {
        kind: Some("wasm".into()),
        ..Default::default()
    };
    assert_eq!(names(&q), vec!["WasmKit".to_string(), "zlib".to_string()]);
}

/// Interpose the stateful ABI by seeding STATE directly (no network).
fn reset_ok_private(rows: Vec<(alloc::vec::Vec<u8>, alloc::vec::Vec<u8>)>) -> i32 {
    let mut st = STATE.lock();
    reset_ok(&mut st, rows)
}

#[test]
fn result_set_abi_and_error_state_are_serial() {
    // These mutate the shared STATE, so they run as one serial test (the
    // registry's parallel-test hang solved the same global-race the same way).
    let q = Query {
        kind: Some("wasm".into()),
        ..Default::default()
    };
    let rows = parse_and_query(INDEX.as_bytes(), &q).expect("parse ok");
    let _ = reset_ok_private(rows);
    assert_eq!(super::pm_wasmmod_net_search_count(), 2);

    let mut buf = [0u8; 512];
    let mut len = buf.len() as u32;
    assert_eq!(super::pm_wasmmod_net_search_name_at(0, buf.as_mut_ptr(), &mut len), 1);
    assert_eq!(core::str::from_utf8(&buf[..len as usize]).unwrap(), "WasmKit");

    let mut mlen = buf.len() as u32;
    assert_eq!(super::pm_wasmmod_net_search_meta_at(0, buf.as_mut_ptr(), &mut mlen), 1);
    let meta = core::str::from_utf8(&buf[..mlen as usize]).unwrap();
    assert!(meta.contains("\"kind\":\"wasm\""));

    // Name 1 is zlib (sorted: WasmKit, zlib).
    let mut len2 = buf.len() as u32;
    assert_eq!(super::pm_wasmmod_net_search_name_at(1, buf.as_mut_ptr(), &mut len2), 1);
    assert_eq!(core::str::from_utf8(&buf[..len2 as usize]).unwrap(), "zlib");

    // Out of range → 0.
    let mut len3 = 0u32;
    assert_eq!(super::pm_wasmmod_net_search_name_at(99, buf.as_mut_ptr(), &mut len3), 0);

    // No error after a clean query.
    let mut err = [0u8; 64];
    super::pm_wasmmod_net_search_last_error(err.as_mut_ptr(), err.len());
    assert!(err[0] == 0 || core::str::from_utf8(&err[..]).unwrap().trim_matches('\0').is_empty());
}
