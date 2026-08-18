#include "ports/micropython/nativecall.h"

#include <string.h>

#include "py/mperrno.h"
#include "py/runtime.h"

#include "ports/common/memcookie.h"
#include "ports/micropython/objhandle.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

/* Wasm/AOT exports are registry_fn_t trampolines (args/results), not a
 * C ABI symbol. Casting them to int32_t(void) returns the trampoline
 * status (-1) instead of the guest i32. ELF/resident stay a real C fn. */
static int wasm_container(const char *fqn) {
    int32_t k = pm_wasmmod_registry_container((const uint8_t *)fqn, (uint32_t)strlen(fqn));
    return k == (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_WASM
        || k == (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_AOT;
}

static int32_t call_registry_i32(const char *fqn, const char *export_name,
    const pm_wasmmod_registry_value_t *args, uint32_t nargs) {
    pm_wasmmod_registry_value_t res;
    int32_t st;
    res.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
    res.of.i32 = 0;
    st = pm_wasmmod_registry_call((const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)export_name, (uint32_t)strlen(export_name), args, nargs, &res, 1u);
    if (st < 0) {
        mp_raise_OSError(MP_EINVAL);
    }
    return res.of.i32;
}

static int fetch_sig(const char *fqn, const char *export_name, char *sig, size_t sig_sz) {
    size_t flen = strlen(fqn);
    uint32_t n = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)flen);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t name[128];
        uint32_t name_len = sizeof(name);
        uint8_t sbuf[160];
        uint32_t slen = sizeof(sbuf);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (!pm_wasmmod_registry_export_at((const uint8_t *)fqn, (uint32_t)flen, i,
                name, &name_len, &kind, sbuf, &slen)) {
            continue;
        }
        name[name_len < sizeof(name) ? name_len : sizeof(name) - 1] = '\0';
        if (strcmp((const char *)name, export_name) != 0) {
            continue;
        }
        size_t copy = slen < sig_sz - 1 ? slen : sig_sz - 1;
        memcpy(sig, sbuf, copy);
        sig[copy] = '\0';
        return 0;
    }
    sig[0] = '\0';
    return -1;
}

mp_obj_t mp_wasm_native_call(const char *fqn, const char *export_name,
    size_t n_args, const mp_obj_t *args) {
    void *p = pm_wasmmod_registry_resolve_native(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)export_name, (uint32_t)strlen(export_name));
    if (p == NULL) {
        mp_raise_msg(&mp_type_LookupError, MP_ERROR_TEXT("resolve miss"));
    }

    char sig[160];
    (void)fetch_sig(fqn, export_name, sig, sizeof(sig));

    if (sig[0] == '\0' || strcmp(sig, "int32_t(void)") == 0
        || strcmp(sig, "uint32_t(void)") == 0) {
        if (n_args != 0 && sig[0] != '\0') {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        if (n_args == 0) {
            if (wasm_container(fqn)) {
                return mp_obj_new_int(call_registry_i32(fqn, export_name, NULL, 0));
            }
            return mp_obj_new_int(((int32_t (*)(void))p)());
        }
    }
    if (strcmp(sig, "void(void)") == 0) {
        if (n_args != 0) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        ((void (*)(void))p)();
        return mp_const_none;
    }
    if (strcmp(sig, "int32_t(int32_t)") == 0 || (sig[0] == '\0' && n_args == 1)) {
        pm_wasmmod_registry_value_t a;
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        if (wasm_container(fqn)) {
            a.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            a.of.i32 = (int32_t)mp_obj_get_int(args[0]);
            return mp_obj_new_int(call_registry_i32(fqn, export_name, &a, 1));
        }
        return mp_obj_new_int(((int32_t (*)(int32_t))p)((int32_t)mp_obj_get_int(args[0])));
    }
    if (strcmp(sig, "int32_t(int32_t, int32_t)") == 0 || (sig[0] == '\0' && n_args == 2)) {
        pm_wasmmod_registry_value_t a[2];
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        if (wasm_container(fqn)) {
            a[0].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            a[0].of.i32 = (int32_t)mp_obj_get_int(args[0]);
            a[1].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            a[1].of.i32 = (int32_t)mp_obj_get_int(args[1]);
            return mp_obj_new_int(call_registry_i32(fqn, export_name, a, 2));
        }
        return mp_obj_new_int(((int32_t (*)(int32_t, int32_t))p)(
            (int32_t)mp_obj_get_int(args[0]), (int32_t)mp_obj_get_int(args[1])));
    }
    if (strcmp(sig, "int32_t(uint32_t, uint16_t)") == 0) {
        /* Host listen/connect faces (net.ssh.listen, net.http.asgi.listen):
         * addr + port. Both are passed as ints on the resident-C ABI. */
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        return mp_obj_new_int(((int32_t (*)(uint32_t, uint16_t))p)(
            (uint32_t)mp_obj_get_int(args[0]), (uint16_t)mp_obj_get_int(args[1])));
    }
    if (strcmp(sig, "int32_t(int32_t, int32_t, int32_t)") == 0 || (sig[0] == '\0' && n_args == 3)) {
        pm_wasmmod_registry_value_t a[3];
        if (n_args != 3) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        if (wasm_container(fqn)) {
            a[0].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            a[0].of.i32 = (int32_t)mp_obj_get_int(args[0]);
            a[1].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            a[1].of.i32 = (int32_t)mp_obj_get_int(args[1]);
            a[2].kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
            a[2].of.i32 = (int32_t)mp_obj_get_int(args[2]);
            return mp_obj_new_int(call_registry_i32(fqn, export_name, a, 3));
        }
        return mp_obj_new_int(((int32_t (*)(int32_t, int32_t, int32_t))p)(
            (int32_t)mp_obj_get_int(args[0]), (int32_t)mp_obj_get_int(args[1]),
            (int32_t)mp_obj_get_int(args[2])));
    }
    if (strcmp(sig, "int32_t(const uint8_t *, uint32_t)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("bufptr needs one bytes-like"));
        }
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
        return mp_obj_new_int(((int32_t (*)(const uint8_t *, uint32_t))p)(
            (const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len));
    }
    if (strcmp(sig, "int32_t(pm_wasmmod_mem_cookie_t)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("mem needs one bytes-like"));
        }
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
        pm_wasmmod_mem_cookie_t c = pm_wasmmod_mem_cookie_put(
            (const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len);
        if (c == 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("mem cookie table full"));
        }
        int32_t out = ((int32_t (*)(pm_wasmmod_mem_cookie_t))p)(c);
        pm_wasmmod_mem_cookie_release(c);
        return mp_obj_new_int(out);
    }
    if (strcmp(sig, "int32_t(pm_wasmmod_obj_handle_t)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("obj needs one argument"));
        }
        pm_wasmmod_obj_handle_t h = pm_wasmmod_obj_handle_put(args[0]);
        if (h == 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("obj handle table full"));
        }
        int32_t out = ((int32_t (*)(pm_wasmmod_obj_handle_t))p)(h);
        pm_wasmmod_obj_handle_release(h);
        return mp_obj_new_int(out);
    }
    if (strcmp(sig, "int64_t(int64_t)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
#if MICROPY_LONGINT_IMPL != MICROPY_LONGINT_IMPL_NONE
        return mp_obj_new_int_from_ll(((int64_t (*)(int64_t))p)((int64_t)mp_obj_get_ll(args[0])));
#else
        return mp_obj_new_int((mp_int_t)((int64_t (*)(int64_t))p)((int64_t)mp_obj_get_int(args[0])));
#endif
    }
#if MICROPY_FLOAT_IMPL != MICROPY_FLOAT_IMPL_NONE
    if (strcmp(sig, "float(float)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        return mp_obj_new_float((mp_float_t)((float (*)(float))p)((float)mp_obj_get_float(args[0])));
    }
    if (strcmp(sig, "double(double)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        return mp_obj_new_float((mp_float_t)((double (*)(double))p)((double)mp_obj_get_float(args[0])));
    }
#endif
    if (strcmp(sig, "int32_t(const char *, const char *)") == 0) {
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        return mp_obj_new_int(((int32_t (*)(const char *, const char *))p)(
            mp_obj_str_get_str(args[0]), mp_obj_str_get_str(args[1])));
    }
    if (strcmp(sig, "const char *(void)") == 0) {
        const char *s;
        if (n_args != 0) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        s = ((const char *(*)(void))p)();
        if (s == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_str(s, strlen(s));
    }

    mp_raise_ValueError(MP_ERROR_TEXT("unsupported native sig (use CONNECT from C/RS)"));
}
