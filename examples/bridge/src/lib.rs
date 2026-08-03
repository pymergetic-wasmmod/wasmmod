// This file is part of the MicroPython project, http://micropython.org/
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
#[link(wasm_import_module = "hello")]
unsafe extern "C" {
    fn hello() -> i32;
}

#[link(wasm_import_module = "mixed")]
unsafe extern "C" {
    fn mixed_i64(x: i64) -> i64;
}

// Guest→host
#[link(wasm_import_module = "wasmmod.host")]
unsafe extern "C" {
    fn call_i32(slot: i32, arg: i32) -> i32;
    fn call_i64(slot: i32, arg: i64) -> i64;
    fn call_f32(slot: i32, arg: f32) -> f32;
    fn call_f64(slot: i32, arg: f64) -> f64;
    fn call_buf(slot: i32, off: i32, len: i32) -> i32;
    fn call_mem(slot: i32, cookie: i32) -> i32;
    fn call_obj(slot: i32, handle: i32) -> i32;
    fn call0_py(mod_off: i32, mod_len: i32, attr_off: i32, attr_len: i32) -> i32;
    fn mem_alloc(size: i32) -> i32;
    fn mem_free(cookie: i32);
    fn mem_copy_in(cookie: i32, src_off: i32, n: i32) -> i32;
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_hello() -> i32 {
    hello()
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host(x: i32) -> i32 {
    call_i32(0, x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_i64(x: i64) -> i64 {
    call_i64(2, x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_f32(x: f32) -> f32 {
    call_f32(3, x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_f64(x: f64) -> f64 {
    call_f64(4, x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_mixed_i64(x: i64) -> i64 {
    mixed_i64(x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_buf() -> i32 {
    static MSG: [u8; 4] = *b"ping";
    call_buf(5, MSG.as_ptr() as i32, 4)
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
    let r = call_mem(5, cookie);
    mem_free(cookie);
    r
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_handle(handle: i32) -> i32 {
    call_obj(6, handle)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_c(x: i32) -> i32 {
    call_i32(7, x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_host_rs(x: i32) -> i32 {
    call_i32(8, x)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_pack_py() -> i32 {
    static M: [u8; 6] = *b"bridge";
    static A: [u8; 9] = *b"ping_code";
    call0_py(M.as_ptr() as i32, 6, A.as_ptr() as i32, 9)
}

#[no_mangle]
pub unsafe extern "C" fn rs_via_peer_py() -> i32 {
    static M: [u8; 10] = *b"hello.util";
    static A: [u8; 9] = *b"ping_code";
    call0_py(M.as_ptr() as i32, 10, A.as_ptr() as i32, 9)
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
