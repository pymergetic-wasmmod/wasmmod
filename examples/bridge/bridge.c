/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Call-matrix bridge pack:
 *   Py → C/RS exports
 *   C → RS (same .wasm)
 *   guest → guest (hello, mixed)
 *   guest → host → Py (micropython.host)
 *
 * Rust mirrors some of these imports in lib.rs (rs_via_*).
 */


#include "../guest.h"

/* Guest→guest (loader forwarders; peers must already be loaded). */
MP_WASM_IMPORT("hello", int, hello, void);
MP_WASM_IMPORT("mixed", int, mixed_answer, void);
MP_WASM_IMPORT("mixed", long long, mixed_i64, long long x);

/* Guest→host (registered by the loader as micropython.host.*). */
MP_WASM_IMPORT("micropython.host", int, call_i32, int slot, int arg);
MP_WASM_IMPORT("micropython.host", int, call0_i32, int slot);
MP_WASM_IMPORT("micropython.host", long long, call_i64, int slot, long long arg);
MP_WASM_IMPORT("micropython.host", float, call_f32, int slot, float arg);
MP_WASM_IMPORT("micropython.host", double, call_f64, int slot, double arg);
MP_WASM_IMPORT("micropython.host", int, call_buf, int slot, int off, int len);
MP_WASM_IMPORT("micropython.host", int, call_mem, int slot, int cookie);
MP_WASM_IMPORT("micropython.host", int, call_obj, int slot, int handle);
MP_WASM_IMPORT("micropython.host", int, call0_py, int mod_off, int mod_len, int attr_off, int attr_len);
MP_WASM_IMPORT("micropython.host", int, call_py, int mod_off, int mod_len, int attr_off, int attr_len, int arg);
MP_WASM_IMPORT("micropython.host", int, mem_alloc, int size);
MP_WASM_IMPORT("micropython.host", void, mem_free, int cookie);
MP_WASM_IMPORT("micropython.host", int, mem_copy_in, int cookie, int src_off, int n);
MP_WASM_IMPORT("micropython.host", int, mem_copy_out, int cookie, int dest_off, int n);

/* Same-pack Rust (linked into this module). */
int rs_square(int x);
int rs_add3(int a, int b, int c);

/* Richer numeric exports (i64 / f32 / f64 + multi-arg). */
long long via_i64(long long x) {
    return x + 1;
}

float via_f32(float x) {
    return x * 2.0f;
}

double via_f64(double x) {
    return x + 0.5;
}

int add3(int a, int b, int c) {
    return a + b + c;
}

double scale_add_f64(double x, double s, double b) {
    return x * s + b;
}

long long via_host_i64(long long x) {
    return call_i64(2, x);
}

float via_host_f32(float x) {
    return call_f32(3, x);
}

double via_host_f64(double x) {
    return call_f64(4, x);
}

/* Guest→guest with i64 (forwarder introspects peer Wasm types). */
long long via_mixed_i64(long long x) {
    return mixed_i64(x);
}

int via_rs(int x) {
    return rs_square(x);
}

/* Same-pack C → C */
int via_c_self(int x) {
    return add3(x, x, x); /* 3x */
}

int via_hello(void) {
    return hello();
}

int via_mixed(void) {
    return mixed_answer();
}

int via_host(int x) {
    return call_i32(0, x);
}

int via_host0(void) {
    return call0_i32(1);
}

/* Guest→host → host C / host RS fun-objs (slots set by matrix). */
int via_host_c(int x) {
    return call_i32(7, x);
}

int via_host_rs(int x) {
    return call_i32(8, x);
}

/* Same-pack C → pack Python via call0_py. */
int via_pack_py(void) {
    static const char m[] = "bridge";
    static const char a[] = "ping_code";
    return call0_py(MP_WASM_PTR(m), (int)(sizeof(m) - 1), MP_WASM_PTR(a), (int)(sizeof(a) - 1));
}

/* Guest→guest C → peer pack Python. */
int via_peer_py(void) {
    static const char m[] = "hello.util";
    static const char a[] = "ping_code";
    return call0_py(MP_WASM_PTR(m), (int)(sizeof(m) - 1), MP_WASM_PTR(a), (int)(sizeof(a) - 1));
}

/* Linear buffer → host as Python bytes (call_buf). */
int via_buf(void) {
    static const char msg[] = "ping";
    return call_buf(5, MP_WASM_PTR(msg), 4);
}

/* Host cookie: copy linear → cookie → call_mem(slot, cookie). */
int via_mem(void) {
    static const char msg[] = "pong";
    int cookie = mem_alloc(4);
    if (cookie == 0) {
        return -1;
    }
    if (mem_copy_in(cookie, MP_WASM_PTR(msg), 4) != 0) {
        mem_free(cookie);
        return -1;
    }
    int r = call_mem(5, cookie);
    mem_free(cookie);
    return r;
}

/* Opaque Python object handle (registered on host, passed as i32). */
int via_handle(int handle) {
    return call_obj(6, handle);
}

/* Round-trip: host cookie written by Py, guest copies to linear and checksums. */
int via_mem_out(int cookie) {
    char buf[8];
    if (mem_copy_out(cookie, MP_WASM_PTR(buf), 4) != 0) {
        return -1;
    }
    return (unsigned char)buf[0] + (unsigned char)buf[1]
        + (unsigned char)buf[2] + (unsigned char)buf[3];
}

/* One-shot combo: C→RS + guest→guest×2 + guest→host. */
int matrix(int x) {
    int a = rs_square(x);     /* C→RS */
    int b = hello();          /* guest→guest → C */
    int c = mixed_answer();   /* guest→guest → C→RS */
    int d = call_i32(0, x);   /* guest→host→Py */
    return a + b + c + d;
}

/* Richer combo: multi-arg + i64 host + f64 scale + peer i64. */
long long matrix_rich(long long x) {
    int a = rs_add3(1, 2, 3);                 /* 6 */
    long long b = call_i64(2, x);             /* x+100 */
    double c = scale_add_f64(2.0, 3.0, 0.5);  /* 6.5 */
    long long d = mixed_i64(x);               /* x+7 */
    return a + b + (long long)c + d;          /* 6+(x+100)+6+(x+7) = 2x+119 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
