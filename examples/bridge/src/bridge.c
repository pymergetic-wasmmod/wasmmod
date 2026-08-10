/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
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
 *   guest → host → Py (wasmmod.host)
 *
 * Rust mirrors some of these imports in lib.rs (rs_via_*).
 */


#include "pm_guest.h"

/* Guest→guest (loader forwarders; peers must already be loaded). */
MP_WASM_IMPORT("pymergetic.wasmmod_examples.hello", int, hello, void);
MP_WASM_IMPORT("pymergetic.wasmmod_examples.mixed", int, mixed_answer, void);
MP_WASM_IMPORT("pymergetic.wasmmod_examples.mixed", long long, mixed_i64, long long x);

/*
 * Guest→host: one plain named import per host callback (matrix.host.* is a
 * test-only module name — a host test registers each with wasm.export_py*
 * before this pack is loaded; a real product host would use whatever module
 * name a pack's pack.toml deps on). Resolved once at load/connect time via
 * the same __pm_modules path as any other import — no slot number, no
 * runtime name string.
 */
MP_WASM_IMPORT("matrix.host", int, host_double, int x);
MP_WASM_IMPORT("matrix.host", int, host_const, void);
MP_WASM_IMPORT("matrix.host", long long, host_i64, long long x);
MP_WASM_IMPORT("matrix.host", float, host_f32, float x);
MP_WASM_IMPORT("matrix.host", double, host_f64, double x);
MP_WASM_IMPORT("matrix.host", int, host_bytes, int cookie);
MP_WASM_IMPORT("matrix.host", int, host_obj, int handle);
MP_WASM_IMPORT("matrix.host", int, host_c_triple, int x);
MP_WASM_IMPORT("matrix.host", int, host_rs_triple, int x);

/* Same-pack self-import + peer-pack import of embedded Python (see
 * src/__init__.py and hello/src/util/__init__.py's wasm.export_py calls).
 * Both are named "ping_code" on the wasm side; MP_WASM_IMPORT_AS keeps the
 * two distinct as local C symbols. */
MP_WASM_IMPORT("pymergetic.wasmmod_examples.bridge", int, ping_code, void);
MP_WASM_IMPORT_AS("pymergetic.wasmmod_examples.hello.util", "ping_code", int, peer_ping_code, void);

/* wasmmod's own durable memory-cookie bridge (unrelated to host_slots —
 * stays regardless of this migration). */
MP_WASM_IMPORT("wasmmod.host", int, mem_alloc, int size);
MP_WASM_IMPORT("wasmmod.host", void, mem_free, int cookie);
MP_WASM_IMPORT("wasmmod.host", int, mem_copy_in, int cookie, int src_off, int n);
MP_WASM_IMPORT("wasmmod.host", int, mem_copy_out, int cookie, int dest_off, int n);

/* Guest loader API (wasm.* on the host → wasmmod.* imports here). */
MP_WASM_IMPORT_AS("wasmmod", "version", int, wasmmod_version, int off, int maxlen);
MP_WASM_IMPORT_AS("wasmmod", "mode", int, wasmmod_mode, void);
MP_WASM_IMPORT_AS("wasmmod", "verify", int, wasmmod_verify, void);
MP_WASM_IMPORT_AS("wasmmod", "trust_count", int, wasmmod_trust_count, void);
MP_WASM_IMPORT_AS("wasmmod", "call_i32", int, wasmmod_call_i32,
    int pack_off, int pack_len, int func_off, int func_len, int nargs, int args_off);

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
    return host_i64(x);
}

float via_host_f32(float x) {
    return host_f32(x);
}

double via_host_f64(double x) {
    return host_f64(x);
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

/* Dynamic peer call via wasmmod.call_i32 (no static hello import required). */
int via_loader_hello(void) {
    static const char pack[] = "pymergetic.wasmmod_examples.hello";
    static const char func[] = "hello";
    return wasmmod_call_i32(MP_WASM_PTR(pack), 33, MP_WASM_PTR(func), 5, 0, 0);
}

int via_loader_version_len(void) {
    char buf[32];
    return wasmmod_version(MP_WASM_PTR(buf), (int)sizeof(buf));
}

int via_loader_mode(void) {
    return wasmmod_mode();
}

int via_loader_verify(void) {
    return wasmmod_verify();
}

int via_loader_trust_count(void) {
    return wasmmod_trust_count();
}

int via_mixed(void) {
    return mixed_answer();
}

int via_host(int x) {
    return host_double(x);
}

int via_host0(void) {
    return host_const();
}

/* Guest→host → host C / host RS fun-objs (host_c_triple/host_rs_triple are
 * wasm.host_c_triple/host_rs_triple exported under matrix.host by name). */
int via_host_c(int x) {
    return host_c_triple(x);
}

int via_host_rs(int x) {
    return host_rs_triple(x);
}

/* Same-pack C → pack Python: static import, resolved once at connect time
 * (see src/__init__.py's wasm.export_py self-export of ping_code). */
int via_pack_py(void) {
    return ping_code();
}

/* Guest→guest C → peer pack Python: static import of hello.util's
 * self-exported ping_code (see hello/src/util/__init__.py). */
int via_peer_py(void) {
    return peer_ping_code();
}

/* Linear buffer → host cookie → host_bytes(cookie) (mem-cookie shape; a
 * WAMR guest has no host-callable "raw offset" shape since that needs the
 * calling instance's exec_env — route through the durable cookie table like
 * via_mem does, just with different message content). */
int via_buf(void) {
    static const char msg[] = "ping";
    int cookie = mem_alloc(4);
    if (cookie == 0) {
        return -1;
    }
    if (mem_copy_in(cookie, MP_WASM_PTR(msg), 4) != 0) {
        mem_free(cookie);
        return -1;
    }
    int r = host_bytes(cookie);
    mem_free(cookie);
    return r;
}

/* Host cookie: copy linear → cookie → host_bytes(cookie). */
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
    int r = host_bytes(cookie);
    mem_free(cookie);
    return r;
}

/* Opaque Python object handle (registered on host, passed as i32). */
int via_handle(int handle) {
    return host_obj(handle);
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
    int d = host_double(x);   /* guest→host→Py */
    return a + b + c + d;
}

/* Richer combo: multi-arg + i64 host + f64 scale + peer i64. */
long long matrix_rich(long long x) {
    int a = rs_add3(1, 2, 3);                 /* 6 */
    long long b = host_i64(x);                /* x+100 */
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
