// This file is part of wasmmod, https://github.com/pymergetic/wasmmod
//
// The MIT License (MIT)
//
// Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#![no_std]

//! Rust side of the bridge pack — mirrors C richer-type / host / mem paths.

/// Same-pack Rust export (also called from C via the C ABI).
#[no_mangle]
pub extern "C" fn rs_square(x: i32) -> i32 {
    x * x
}

#[no_mangle]
pub extern "C" fn rs_add3(a: i32, b: i32, c: i32) -> i32 {
    a + b + c
}

#[no_mangle]
pub extern "C" fn rs_via_i64(x: i64) -> i64 {
    x + 1
}

#[no_mangle]
pub extern "C" fn rs_via_f32(x: f32) -> f32 {
    x * 2.0
}

#[no_mangle]
pub extern "C" fn rs_via_f64(x: f64) -> f64 {
    x + 0.5
}

#[no_mangle]
pub extern "C" fn rs_scale_add_f64(x: f64, s: f64, b: f64) -> f64 {
    x * s + b
}

// Same-pack C symbols (linked into this module).
unsafe extern "C" {
    fn add3(a: i32, b: i32, c: i32) -> i32;
}

/// Same-pack RS → RS
#[no_mangle]
pub extern "C" fn rs_via_square(x: i32) -> i32 {
    rs_square(x)
}

/// Same-pack RS → C
#[no_mangle]
pub unsafe extern "C" fn rs_via_add3(x: i32) -> i32 {
    add3(x, 1, 2)
}

// Guest→guest
#[link(wasm_import_module = "pymergetic.wasmmod_examples.hello")]
unsafe extern "C" {
    fn hello() -> i32;
}

#[link(wasm_import_module = "pymergetic.wasmmod_examples.mixed")]
unsafe extern "C" {
    fn mixed_i64(x: i64) -> i64;
}

// Guest→host: one plain named import per host callback, resolved once at
// connect time via __pm_modules (see bridge.c's matching MP_WASM_IMPORT
// block for the full rationale). matrix.host.* is test-only naming; a real
// pack would use whatever module name its own pack.toml deps on.
#[link(wasm_import_module = "matrix.host")]
unsafe extern "C" {
    fn host_double(x: i32) -> i32;
    fn host_i64(x: i64) -> i64;
    fn host_f32(x: f32) -> f32;
    fn host_f64(x: f64) -> f64;
    fn host_bytes(cookie: i32) -> i32;
    fn host_obj(handle: i32) -> i32;
    fn host_c_triple(x: i32) -> i32;
    fn host_rs_triple(x: i32) -> i32;
}

// Same-pack self-import + peer-pack import of embedded Python (see
// bridge/src/__init__.py and hello/src/util/__init__.py's wasm.export_py
// calls).
#[link(wasm_import_module = "pymergetic.wasmmod_examples.bridge")]
unsafe extern "C" {
    fn ping_code() -> i32;
}

#[link(wasm_import_module = "pymergetic.wasmmod_examples.hello.util")]
unsafe extern "C" {
    #[link_name = "ping_code"]
    fn peer_ping_code() -> i32;
}

// wasmmod's own durable memory-cookie bridge (unrelated to host_slots).
#[link(wasm_import_module = "wasmmod.host")]
unsafe extern "C" {
    fn mem_alloc(size: i32) -> i32;
    fn mem_free(cookie: i32);
    fn mem_copy_in(cookie: i32, src_off: i32, n: i32) -> i32;
}

// Guest loader API (host wasm.* mirror)
#[link(wasm_import_module = "wasmmod")]
unsafe extern "C" {
    fn version(off: i32, maxlen: i32) -> i32;
    fn mode() -> i32;
    fn verify() -> i32;
    fn trust_count() -> i32;
    #[link_name = "call_i32"]
    fn wasmmod_call_i32(
        pack_off: i32,
        pack_len: i32,
        func_off: i32,
        func_len: i32,
        nargs: i32,
        args_off: i32,
    ) -> i32;
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_hello() -> i32 {
    hello()
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_loader_hello() -> i32 {
    static PACK: [u8; 33] = *b"pymergetic.wasmmod_examples.hello";
    static FUNC: [u8; 5] = *b"hello";
    wasmmod_call_i32(PACK.as_ptr() as i32, 33, FUNC.as_ptr() as i32, 5, 0, 0)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_loader_version_len() -> i32 {
    let mut buf = [0u8; 32];
    version(buf.as_mut_ptr() as i32, 32)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_loader_mode() -> i32 {
    mode()
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_loader_verify() -> i32 {
    verify()
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_loader_trust_count() -> i32 {
    trust_count()
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host(x: i32) -> i32 {
    host_double(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_i64(x: i64) -> i64 {
    host_i64(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_f32(x: f32) -> f32 {
    host_f32(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_f64(x: f64) -> f64 {
    host_f64(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_mixed_i64(x: i64) -> i64 {
    mixed_i64(x)
}

// Cookie-based (mem-cookie shape; a WAMR guest has no host-callable "raw
// offset" shape since that needs the calling instance's exec_env — route
// through the durable cookie table like rs_via_mem does).
#[no_mangle]
pub unsafe extern "C" fn rs_via_buf() -> i32 {
    static MSG: [u8; 4] = *b"ping";
    let cookie = mem_alloc(4);
    if cookie == 0 {
        return -1;
    }
    if mem_copy_in(cookie, MSG.as_ptr() as i32, 4) != 0 {
        mem_free(cookie);
        return -1;
    }
    let r = host_bytes(cookie);
    mem_free(cookie);
    r
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_mem() -> i32 {
    static MSG: [u8; 4] = *b"pong";
    let cookie = mem_alloc(4);
    if cookie == 0 {
        return -1;
    }
    if mem_copy_in(cookie, MSG.as_ptr() as i32, 4) != 0 {
        mem_free(cookie);
        return -1;
    }
    let r = host_bytes(cookie);
    mem_free(cookie);
    r
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_handle(handle: i32) -> i32 {
    host_obj(handle)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_c(x: i32) -> i32 {
    host_c_triple(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_rs(x: i32) -> i32 {
    host_rs_triple(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_pack_py() -> i32 {
    ping_code()
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_peer_py() -> i32 {
    peer_ping_code()
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
