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

//! Call-matrix-only host Rust helpers (not part of the default wasm loader).
//! Built when `MICROPY_PY_WASM_MATRIX=1` (see examples/Makefile).

#![no_std]

unsafe extern "C" {
    fn mp_wasm_host_call_export_i32(
        pack: *const u8,
        pack_len: usize,
        func: *const u8,
        func_len: usize,
        nargs: u32,
        args: *const i32,
        out: *mut i32,
    ) -> i32;

    fn mp_wasm_host_call_attr(
        module: *const u8,
        module_len: usize,
        attr: *const u8,
        attr_len: usize,
        has_arg: i32,
        arg: i32,
        out: *mut usize,
    ) -> i32;
}

/// Host RS callee: guest→host → RS (also exposed as a MicroPython fun-obj).
#[no_mangle]
pub extern "C" fn mp_wasm_host_rs_triple(x: i32) -> i32 {
    x * 3
}

/// Host RS → guest Wasm export (i32 args).
#[no_mangle]
pub unsafe extern "C" fn mp_wasm_host_rs_call_export_i32(
    pack: *const u8,
    pack_len: usize,
    func: *const u8,
    func_len: usize,
    nargs: u32,
    args: *const i32,
    out: *mut i32,
) -> i32 {
    mp_wasm_host_call_export_i32(pack, pack_len, func, func_len, nargs, args, out)
}

/// Host RS → guest / pack Python attr.
#[no_mangle]
pub unsafe extern "C" fn mp_wasm_host_rs_call_attr(
    module: *const u8,
    module_len: usize,
    attr: *const u8,
    attr_len: usize,
    has_arg: i32,
    arg: i32,
    out: *mut usize,
) -> i32 {
    mp_wasm_host_call_attr(module, module_len, attr, attr_len, has_arg, arg, out)
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
