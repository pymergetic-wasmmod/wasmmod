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

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM

#include <string.h>

#include "extmod/wasmmod/alloc.h"

#include "py/obj.h"
#include "py/runtime.h"

#include "extmod/wasmmod/forward.h"
#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/runtime.h"
#include "wasm_export.h"

// Growable GC-rooted list of callables (index = slot).
MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_host_slots);
// Growable GC-rooted list of Python objects (index + 1 = handle).
MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_handles);

static int host_registered;

// ---- Callable slots ----

static void host_slots_ensure(void) {
    if (MP_STATE_VM(mp_wasm_host_slots) != MP_OBJ_NULL
        && mp_obj_is_type(MP_STATE_VM(mp_wasm_host_slots), &mp_type_list)) {
        return;
    }
    mp_obj_list_t *list = m_new_obj(mp_obj_list_t);
    mp_obj_list_init(list, 0);
    MP_STATE_VM(mp_wasm_host_slots) = MP_OBJ_FROM_PTR(list);
}

static bool host_slots_grow_to(size_t need_len) {
    host_slots_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots));
    while (list->len < need_len) {
        mp_obj_list_append(MP_STATE_VM(mp_wasm_host_slots), mp_const_none);
        list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots));
    }
    return true;
}

void mp_wasm_host_clear_all(void) {
    if (MP_STATE_VM(mp_wasm_host_slots) == MP_OBJ_NULL
        || !mp_obj_is_type(MP_STATE_VM(mp_wasm_host_slots), &mp_type_list)) {
        return;
    }
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots));
    for (size_t i = 0; i < list->len; ++i) {
        list->items[i] = mp_const_none;
    }
}

size_t mp_wasm_host_slot_count(void) {
    if (MP_STATE_VM(mp_wasm_host_slots) == MP_OBJ_NULL
        || !mp_obj_is_type(MP_STATE_VM(mp_wasm_host_slots), &mp_type_list)) {
        return 0;
    }
    return ((mp_obj_list_t *)MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots)))->len;
}

static mp_obj_t slot_callable(int32_t slot) {
    if (slot < 0) {
        return MP_OBJ_NULL;
    }
    host_slots_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots));
    if ((size_t)slot >= list->len) {
        return MP_OBJ_NULL;
    }
    mp_obj_t cb = list->items[slot];
    if (cb == mp_const_none || !mp_obj_is_callable(cb)) {
        return MP_OBJ_NULL;
    }
    return cb;
}

bool mp_wasm_host_set_slot(int32_t slot, mp_obj_t callable) {
    if (slot < 0) {
        return false;
    }
    if (callable != mp_const_none && !mp_obj_is_callable(callable)) {
        return false;
    }
    if (!host_slots_grow_to((size_t)slot + 1)) {
        return false;
    }
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots));
    list->items[slot] = callable;
    return true;
}

mp_obj_t mp_wasm_host_get_slot(int32_t slot) {
    if (slot < 0) {
        return mp_const_none;
    }
    host_slots_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_host_slots));
    if ((size_t)slot >= list->len) {
        return mp_const_none;
    }
    return list->items[slot];
}

// ---- Mem cookies (host heap; not GC) ----

typedef struct {
    uint8_t *data;
    uint32_t len;
    bool used;
} mp_wasm_cookie_t;

static mp_wasm_cookie_t *cookies;
static size_t cookies_cap;

static bool cookie_grow(size_t need) {
    if (need <= cookies_cap) {
        return true;
    }
    size_t ncap = cookies_cap ? cookies_cap * 2 : 8;
    while (ncap < need) {
        ncap *= 2;
    }
    mp_wasm_cookie_t *n = MICROPY_WASM_REALLOC(cookies, ncap * sizeof(*n));
    if (n == NULL) {
        return false;
    }
    memset(n + cookies_cap, 0, (ncap - cookies_cap) * sizeof(*n));
    cookies = n;
    cookies_cap = ncap;
    return true;
}

static mp_wasm_cookie_t *cookie_get(int32_t cookie) {
    if (cookie <= 0) {
        return NULL;
    }
    size_t idx = (size_t)cookie - 1;
    if (idx >= cookies_cap || !cookies[idx].used) {
        return NULL;
    }
    return &cookies[idx];
}

int32_t mp_wasm_mem_alloc(uint32_t size) {
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < cookies_cap; ++i) {
        if (!cookies[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == SIZE_MAX) {
        slot = cookies_cap;
        if (!cookie_grow(slot + 1)) {
            return 0;
        }
    }
    uint8_t *buf = NULL;
    if (size > 0) {
        buf = MICROPY_WASM_MALLOC(size);
        if (buf == NULL) {
            return 0;
        }
        memset(buf, 0, size);
    }
    cookies[slot].data = buf;
    cookies[slot].len = size;
    cookies[slot].used = true;
    return (int32_t)(slot + 1);
}

int32_t mp_wasm_mem_alloc_copy(const uint8_t *data, uint32_t len) {
    int32_t c = mp_wasm_mem_alloc(len);
    if (c == 0) {
        return 0;
    }
    if (len > 0 && data != NULL) {
        mp_wasm_cookie_t *slot = cookie_get(c);
        memcpy(slot->data, data, len);
    }
    return c;
}

bool mp_wasm_mem_free(int32_t cookie) {
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        return false;
    }
    MICROPY_WASM_FREE(slot->data);
    slot->data = NULL;
    slot->len = 0;
    slot->used = false;
    return true;
}

void mp_wasm_mem_clear_all(void) {
    for (size_t i = 0; i < cookies_cap; ++i) {
        if (cookies[i].used) {
            MICROPY_WASM_FREE(cookies[i].data);
            cookies[i].data = NULL;
            cookies[i].len = 0;
            cookies[i].used = false;
        }
    }
}

bool mp_wasm_mem_valid(int32_t cookie) {
    return cookie_get(cookie) != NULL;
}

uint32_t mp_wasm_mem_len(int32_t cookie) {
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    return slot != NULL ? slot->len : 0;
}

const uint8_t *mp_wasm_mem_data(int32_t cookie, uint32_t *len_out) {
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        if (len_out) {
            *len_out = 0;
        }
        return NULL;
    }
    if (len_out) {
        *len_out = slot->len;
    }
    return slot->data;
}

bool mp_wasm_mem_set(int32_t cookie, const uint8_t *data, uint32_t len) {
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        return false;
    }
    uint8_t *buf = NULL;
    if (len > 0) {
        buf = MICROPY_WASM_MALLOC(len);
        if (buf == NULL) {
            return false;
        }
        if (data != NULL) {
            memcpy(buf, data, len);
        } else {
            memset(buf, 0, len);
        }
    }
    MICROPY_WASM_FREE(slot->data);
    slot->data = buf;
    slot->len = len;
    return true;
}

// ---- Object handles (GC-rooted) ----

static void handles_ensure(void) {
    if (MP_STATE_VM(mp_wasm_handles) != MP_OBJ_NULL
        && mp_obj_is_type(MP_STATE_VM(mp_wasm_handles), &mp_type_list)) {
        return;
    }
    mp_obj_list_t *list = m_new_obj(mp_obj_list_t);
    mp_obj_list_init(list, 0);
    MP_STATE_VM(mp_wasm_handles) = MP_OBJ_FROM_PTR(list);
}

int32_t mp_wasm_handle_register(mp_obj_t obj) {
    // None marks a free slot — do not register it as a value.
    if (obj == mp_const_none) {
        return 0;
    }
    handles_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    for (size_t i = 0; i < list->len; ++i) {
        if (list->items[i] == mp_const_none) {
            list->items[i] = obj;
            return (int32_t)(i + 1);
        }
    }
    mp_obj_list_append(MP_STATE_VM(mp_wasm_handles), obj);
    list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    return (int32_t)list->len; // just-appended index + 1
}

mp_obj_t mp_wasm_handle_resolve(int32_t handle) {
    if (handle <= 0) {
        return mp_const_none;
    }
    handles_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    size_t idx = (size_t)handle - 1;
    if (idx >= list->len) {
        return mp_const_none;
    }
    return list->items[idx];
}

bool mp_wasm_handle_free(int32_t handle) {
    if (handle <= 0) {
        return false;
    }
    handles_ensure();
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    size_t idx = (size_t)handle - 1;
    if (idx >= list->len || list->items[idx] == mp_const_none) {
        return false;
    }
    list->items[idx] = mp_const_none;
    return true;
}

void mp_wasm_handle_clear_all(void) {
    if (MP_STATE_VM(mp_wasm_handles) == MP_OBJ_NULL
        || !mp_obj_is_type(MP_STATE_VM(mp_wasm_handles), &mp_type_list)) {
        return;
    }
    mp_obj_list_t *list = MP_OBJ_TO_PTR(MP_STATE_VM(mp_wasm_handles));
    for (size_t i = 0; i < list->len; ++i) {
        list->items[i] = mp_const_none;
    }
}

// ---- Host-language helpers (also used by host_rs.rs) ----

#define MP_WASM_HOST_NAME_MAX 64

static bool copy_name(char *dst, size_t dst_sz, const char *src, size_t src_len) {
    if (src == NULL || src_len == 0 || src_len >= dst_sz) {
        return false;
    }
    memcpy(dst, src, src_len);
    dst[src_len] = '\0';
    return true;
}

int mp_wasm_host_call_export_i32(const char *pack, size_t pack_len,
    const char *func, size_t func_len,
    uint32_t nargs, const int32_t *args, int32_t *out) {
    char pname[MP_WASM_HOST_NAME_MAX];
    char fname[MP_WASM_HOST_NAME_MAX];
    if (!copy_name(pname, sizeof(pname), pack, pack_len)
        || !copy_name(fname, sizeof(fname), func, func_len)
        || out == NULL) {
        return -1;
    }
    if (nargs > 0 && args == NULL) {
        return -1;
    }
    mp_pack_t *mod = mp_wasm_registry_find(pname);
    if (mod == NULL) {
        return -1;
    }
    char err[64];
    if (nargs == 0) {
        if (!mp_pack_call0(mod, fname, out, err, sizeof(err))) {
            return -1;
        }
        return 0;
    }
    if (!mp_pack_call_i32(mod, fname, args, nargs, out, err, sizeof(err))) {
        return -1;
    }
    return 0;
}

static mp_obj_t loaded_module(const char *name) {
    qstr q = qstr_from_str(name);
    mp_map_elem_t *el = mp_map_lookup(
        &MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(q),
        MP_MAP_LOOKUP);
    if (el == NULL) {
        return MP_OBJ_NULL;
    }
    return el->value;
}

int mp_wasm_host_call_attr(const char *mod, size_t mod_len,
    const char *attr, size_t attr_len,
    int has_arg, int32_t arg, mp_obj_t *out) {
    char mname[MP_WASM_HOST_NAME_MAX];
    char aname[MP_WASM_HOST_NAME_MAX];
    if (!copy_name(mname, sizeof(mname), mod, mod_len)
        || !copy_name(aname, sizeof(aname), attr, attr_len)
        || out == NULL) {
        return -1;
    }
    mp_obj_t module = loaded_module(mname);
    if (module == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr(module, qstr_from_str(aname));
        mp_obj_t res;
        if (has_arg) {
            res = mp_call_function_1(fn, mp_obj_new_int(arg));
        } else {
            res = mp_call_function_0(fn);
        }
        nlr_pop();
        *out = res;
        return 0;
    }
    return -1;
}

#if MICROPY_PY_WASM_MATRIX
static mp_obj_t host_c_triple(mp_obj_t x_in) {
    return mp_obj_new_int(mp_obj_get_int(x_in) * 3);
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_wasm_host_c_triple_obj, host_c_triple);

// Defined in examples/wasmmod/host_matrix.rs
int32_t mp_wasm_host_rs_triple(int32_t x);

static mp_obj_t host_rs_triple(mp_obj_t x_in) {
    return mp_obj_new_int(mp_wasm_host_rs_triple((int32_t)mp_obj_get_int(x_in)));
}
MP_DEFINE_CONST_FUN_OBJ_1(mp_wasm_host_rs_triple_obj, host_rs_triple);
#endif

// Guest linear names → sys.modules lookup / call (native → pack Python).
static bool guest_name(wasm_exec_env_t exec_env, int32_t off, int32_t len,
    char *buf, size_t buf_sz) {
    if (len < 0 || (size_t)len >= buf_sz) {
        return false;
    }
    void *p = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)off, (uint32_t)len, &p)) {
        return false;
    }
    memcpy(buf, p, (size_t)len);
    buf[len] = '\0';
    return true;
}

static int32_t host_call0_py(wasm_exec_env_t exec_env,
    int32_t mod_off, int32_t mod_len, int32_t attr_off, int32_t attr_len) {
    char mname[MP_WASM_HOST_NAME_MAX];
    char aname[MP_WASM_HOST_NAME_MAX];
    if (!guest_name(exec_env, mod_off, mod_len, mname, sizeof(mname))
        || !guest_name(exec_env, attr_off, attr_len, aname, sizeof(aname))) {
        return -1;
    }
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_call_attr(mname, strlen(mname), aname, strlen(aname), 0, 0, &out) != 0) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        int32_t v = (int32_t)mp_obj_get_int(out);
        nlr_pop();
        return v;
    }
    return -1;
}

static int32_t host_call_py(wasm_exec_env_t exec_env,
    int32_t mod_off, int32_t mod_len, int32_t attr_off, int32_t attr_len, int32_t arg) {
    char mname[MP_WASM_HOST_NAME_MAX];
    char aname[MP_WASM_HOST_NAME_MAX];
    if (!guest_name(exec_env, mod_off, mod_len, mname, sizeof(mname))
        || !guest_name(exec_env, attr_off, attr_len, aname, sizeof(aname))) {
        return -1;
    }
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_call_attr(mname, strlen(mname), aname, strlen(aname), 1, arg, &out) != 0) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        int32_t v = (int32_t)mp_obj_get_int(out);
        nlr_pop();
        return v;
    }
    return -1;
}

// ---- WAMR natives ----

static int32_t host_call_i32(wasm_exec_env_t exec_env, int32_t slot, int32_t arg) {
    (void)exec_env;
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_1(cb, mp_obj_new_int(arg));
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

static int32_t host_call0_i32(wasm_exec_env_t exec_env, int32_t slot) {
    (void)exec_env;
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_0(cb);
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

static int64_t host_call_i64(wasm_exec_env_t exec_env, int32_t slot, int64_t arg) {
    (void)exec_env;
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_1(cb, mp_obj_new_int_from_ll(arg));
        int64_t out = mp_obj_get_ll(res);
        nlr_pop();
        return out;
    }
    return -1;
}

static float host_call_f32(wasm_exec_env_t exec_env, int32_t slot, float arg) {
    (void)exec_env;
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1.0f;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_1(cb, mp_obj_new_float((mp_float_t)arg));
        float out = mp_obj_get_float_to_f(res);
        nlr_pop();
        return out;
    }
    return -1.0f;
}

static double host_call_f64(wasm_exec_env_t exec_env, int32_t slot, double arg) {
    (void)exec_env;
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1.0;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_1(cb, mp_obj_new_float((mp_float_t)arg));
        double out = mp_obj_get_float_to_d(res);
        nlr_pop();
        return out;
    }
    return -1.0;
}

// Guest linear [off,len] → Python bytes → callable(bytes) → i32.
static int32_t host_call_buf(wasm_exec_env_t exec_env, int32_t slot, int32_t off, int32_t len) {
    if (len < 0) {
        return -1;
    }
    void *p = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)off, (uint32_t)len, &p)) {
        return -1;
    }
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t buf = mp_obj_new_bytes(p, (size_t)len);
        mp_obj_t res = mp_call_function_1(cb, buf);
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

// Cookie → Python bytes → callable(bytes) → i32.
static int32_t host_call_mem(wasm_exec_env_t exec_env, int32_t slot, int32_t cookie) {
    (void)exec_env;
    mp_wasm_cookie_t *cslot = cookie_get(cookie);
    if (cslot == NULL) {
        return -1;
    }
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t buf = mp_obj_new_bytes(cslot->data, (size_t)cslot->len);
        mp_obj_t res = mp_call_function_1(cb, buf);
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

// Handle → resolved Python object → callable(obj) → i32.
static int32_t host_call_obj(wasm_exec_env_t exec_env, int32_t slot, int32_t handle) {
    (void)exec_env;
    if (handle <= 0) {
        return -1;
    }
    mp_obj_t obj = mp_wasm_handle_resolve(handle);
    if (obj == mp_const_none) {
        return -1; // free / invalid (None is not a registerable value)
    }
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t res = mp_call_function_1(cb, obj);
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

static int32_t host_mem_alloc(wasm_exec_env_t exec_env, int32_t size) {
    (void)exec_env;
    if (size < 0) {
        return 0;
    }
    return mp_wasm_mem_alloc((uint32_t)size);
}

static void host_mem_free(wasm_exec_env_t exec_env, int32_t cookie) {
    (void)exec_env;
    (void)mp_wasm_mem_free(cookie);
}

static int32_t host_mem_len(wasm_exec_env_t exec_env, int32_t cookie) {
    (void)exec_env;
    return (int32_t)mp_wasm_mem_len(cookie);
}

static int32_t host_mem_copy_in(wasm_exec_env_t exec_env, int32_t cookie, int32_t src_off, int32_t n) {
    if (n < 0) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL || (uint32_t)n > slot->len) {
        return -1;
    }
    void *linear = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)src_off, (uint32_t)n, &linear)) {
        return -1;
    }
    if (n > 0) {
        memcpy(slot->data, linear, (size_t)n);
    }
    return 0;
}

static int32_t host_mem_copy_out(wasm_exec_env_t exec_env, int32_t cookie, int32_t dest_off, int32_t n) {
    if (n < 0) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL || (uint32_t)n > slot->len) {
        return -1;
    }
    void *linear = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)dest_off, (uint32_t)n, &linear)) {
        return -1;
    }
    if (n > 0) {
        memcpy(linear, slot->data, (size_t)n);
    }
    return 0;
}

static int32_t host_mem_copy_in_at(wasm_exec_env_t exec_env, int32_t cookie, int32_t cookie_off, int32_t src_off, int32_t n) {
    if (n < 0 || cookie_off < 0) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        return -1;
    }
    if ((uint32_t)cookie_off > slot->len || (uint32_t)n > (slot->len - (uint32_t)cookie_off)) {
        return -1;
    }
    void *linear = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)src_off, (uint32_t)n, &linear)) {
        return -1;
    }
    if (n > 0) {
        memcpy(slot->data + cookie_off, linear, (size_t)n);
    }
    return 0;
}

static int32_t host_mem_copy_out_at(wasm_exec_env_t exec_env, int32_t cookie, int32_t cookie_off, int32_t dest_off, int32_t n) {
    if (n < 0 || cookie_off < 0) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        return -1;
    }
    if ((uint32_t)cookie_off > slot->len || (uint32_t)n > (slot->len - (uint32_t)cookie_off)) {
        return -1;
    }
    void *linear = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)dest_off, (uint32_t)n, &linear)) {
        return -1;
    }
    if (n > 0) {
        memcpy(linear, slot->data + cookie_off, (size_t)n);
    }
    return 0;
}

static NativeSymbol host_symbols[] = {
    { "call_i32", (void *)host_call_i32, "(ii)i", NULL },
    { "call0_i32", (void *)host_call0_i32, "(i)i", NULL },
    { "call_i64", (void *)host_call_i64, "(iI)I", NULL },
    { "call_f32", (void *)host_call_f32, "(if)f", NULL },
    { "call_f64", (void *)host_call_f64, "(iF)F", NULL },
    { "call_buf", (void *)host_call_buf, "(iii)i", NULL },
    { "call_mem", (void *)host_call_mem, "(ii)i", NULL },
    { "call_obj", (void *)host_call_obj, "(ii)i", NULL },
    { "call0_py", (void *)host_call0_py, "(iiii)i", NULL },
    { "call_py", (void *)host_call_py, "(iiiii)i", NULL },
    { "mem_alloc", (void *)host_mem_alloc, "(i)i", NULL },
    { "mem_free", (void *)host_mem_free, "(i)", NULL },
    { "mem_len", (void *)host_mem_len, "(i)i", NULL },
    { "mem_copy_in", (void *)host_mem_copy_in, "(iii)i", NULL },
    { "mem_copy_out", (void *)host_mem_copy_out, "(iii)i", NULL },
    { "mem_copy_in_at", (void *)host_mem_copy_in_at, "(iiii)i", NULL },
    { "mem_copy_out_at", (void *)host_mem_copy_out_at, "(iiii)i", NULL },
};

bool mp_wasm_host_register(void) {
    if (host_registered) {
        return true;
    }
    host_slots_ensure();
    handles_ensure();
    if (!wasm_runtime_register_natives(MP_WASM_HOST_MODULE, host_symbols,
            sizeof(host_symbols) / sizeof(host_symbols[0]))) {
        return false;
    }
    host_registered = 1;
    return true;
}

#if MICROPY_PY_WASM_ELF
// System V entrypoints for in-tree ELF guests (no wasm_exec_env_t).
// Pointer args replace Wasm linear offsets (call_buf / call_py / mem_copy_*).

static int32_t elf_host_call_i32(int32_t slot, int32_t arg) {
    return host_call_i32(NULL, slot, arg);
}
static int32_t elf_host_call0_i32(int32_t slot) {
    return host_call0_i32(NULL, slot);
}
static int64_t elf_host_call_i64(int32_t slot, int64_t arg) {
    return host_call_i64(NULL, slot, arg);
}
static float elf_host_call_f32(int32_t slot, float arg) {
    return host_call_f32(NULL, slot, arg);
}
static double elf_host_call_f64(int32_t slot, double arg) {
    return host_call_f64(NULL, slot, arg);
}
static int32_t elf_host_call_mem(int32_t slot, int32_t cookie) {
    return host_call_mem(NULL, slot, cookie);
}
static int32_t elf_host_call_obj(int32_t slot, int32_t handle) {
    return host_call_obj(NULL, slot, handle);
}
static int32_t elf_host_mem_alloc(int32_t size) {
    return host_mem_alloc(NULL, size);
}
static void elf_host_mem_free(int32_t cookie) {
    host_mem_free(NULL, cookie);
}
static int32_t elf_host_mem_len(int32_t cookie) {
    return host_mem_len(NULL, cookie);
}

static bool elf_guest_name(const void *ptr, int32_t len, char *buf, size_t buf_sz) {
    if (ptr == NULL || len < 0 || (size_t)len >= buf_sz) {
        return false;
    }
    memcpy(buf, ptr, (size_t)len);
    buf[len] = '\0';
    return true;
}

// Guest native [ptr,len] → Python bytes → callable(bytes) → i32.
static int32_t elf_host_call_buf(int32_t slot, const void *ptr, int32_t len) {
    if (len < 0 || (len > 0 && ptr == NULL)) {
        return -1;
    }
    mp_obj_t cb = slot_callable(slot);
    if (cb == MP_OBJ_NULL) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t buf = mp_obj_new_bytes(ptr, (size_t)len);
        mp_obj_t res = mp_call_function_1(cb, buf);
        int32_t out = (int32_t)mp_obj_get_int(res);
        nlr_pop();
        return out;
    }
    return -1;
}

static int32_t elf_host_call0_py(const void *mod, int32_t mod_len,
    const void *attr, int32_t attr_len) {
    char mname[MP_WASM_HOST_NAME_MAX];
    char aname[MP_WASM_HOST_NAME_MAX];
    if (!elf_guest_name(mod, mod_len, mname, sizeof(mname))
        || !elf_guest_name(attr, attr_len, aname, sizeof(aname))) {
        return -1;
    }
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_call_attr(mname, strlen(mname), aname, strlen(aname), 0, 0, &out) != 0) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        int32_t v = (int32_t)mp_obj_get_int(out);
        nlr_pop();
        return v;
    }
    return -1;
}

static int32_t elf_host_call_py(const void *mod, int32_t mod_len,
    const void *attr, int32_t attr_len, int32_t arg) {
    char mname[MP_WASM_HOST_NAME_MAX];
    char aname[MP_WASM_HOST_NAME_MAX];
    if (!elf_guest_name(mod, mod_len, mname, sizeof(mname))
        || !elf_guest_name(attr, attr_len, aname, sizeof(aname))) {
        return -1;
    }
    mp_obj_t out = MP_OBJ_NULL;
    if (mp_wasm_host_call_attr(mname, strlen(mname), aname, strlen(aname), 1, arg, &out) != 0) {
        return -1;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        int32_t v = (int32_t)mp_obj_get_int(out);
        nlr_pop();
        return v;
    }
    return -1;
}

static int32_t elf_host_mem_copy_in(int32_t cookie, const void *src, int32_t n) {
    if (n < 0 || (n > 0 && src == NULL)) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL || (uint32_t)n > slot->len) {
        return -1;
    }
    if (n > 0) {
        memcpy(slot->data, src, (size_t)n);
    }
    return 0;
}

static int32_t elf_host_mem_copy_out(int32_t cookie, void *dest, int32_t n) {
    if (n < 0 || (n > 0 && dest == NULL)) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL || (uint32_t)n > slot->len) {
        return -1;
    }
    if (n > 0) {
        memcpy(dest, slot->data, (size_t)n);
    }
    return 0;
}

static int32_t elf_host_mem_copy_in_at(int32_t cookie, int32_t cookie_off, const void *src, int32_t n) {
    if (n < 0 || cookie_off < 0 || (n > 0 && src == NULL)) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        return -1;
    }
    if ((uint32_t)cookie_off > slot->len || (uint32_t)n > (slot->len - (uint32_t)cookie_off)) {
        return -1;
    }
    if (n > 0) {
        memcpy(slot->data + cookie_off, src, (size_t)n);
    }
    return 0;
}

static int32_t elf_host_mem_copy_out_at(int32_t cookie, int32_t cookie_off, void *dest, int32_t n) {
    if (n < 0 || cookie_off < 0 || (n > 0 && dest == NULL)) {
        return -1;
    }
    mp_wasm_cookie_t *slot = cookie_get(cookie);
    if (slot == NULL) {
        return -1;
    }
    if ((uint32_t)cookie_off > slot->len || (uint32_t)n > (slot->len - (uint32_t)cookie_off)) {
        return -1;
    }
    if (n > 0) {
        memcpy(dest, slot->data + cookie_off, (size_t)n);
    }
    return 0;
}

typedef struct {
    const char *name;
    void *addr;
} mp_wasm_elf_native_t;

static const mp_wasm_elf_native_t elf_host_natives[] = {
    { "call_i32", (void *)elf_host_call_i32 },
    { "call0_i32", (void *)elf_host_call0_i32 },
    { "call_i64", (void *)elf_host_call_i64 },
    { "call_f32", (void *)elf_host_call_f32 },
    { "call_f64", (void *)elf_host_call_f64 },
    { "call_buf", (void *)elf_host_call_buf },
    { "call_mem", (void *)elf_host_call_mem },
    { "call_obj", (void *)elf_host_call_obj },
    { "call0_py", (void *)elf_host_call0_py },
    { "call_py", (void *)elf_host_call_py },
    { "mem_alloc", (void *)elf_host_mem_alloc },
    { "mem_free", (void *)elf_host_mem_free },
    { "mem_len", (void *)elf_host_mem_len },
    { "mem_copy_in", (void *)elf_host_mem_copy_in },
    { "mem_copy_out", (void *)elf_host_mem_copy_out },
    { "mem_copy_in_at", (void *)elf_host_mem_copy_in_at },
    { "mem_copy_out_at", (void *)elf_host_mem_copy_out_at },
};

void *mp_wasm_host_elf_lookup(const char *func) {
    if (func == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(elf_host_natives) / sizeof(elf_host_natives[0]); ++i) {
        if (strcmp(elf_host_natives[i].name, func) == 0) {
            return elf_host_natives[i].addr;
        }
    }
    return NULL;
}
#endif // MICROPY_PY_WASM_ELF

#endif // MICROPY_PY_WASM
