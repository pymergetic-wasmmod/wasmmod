#include "ports/micropython/hostready.h"

#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "pymergetic/wasmmod/pyexport.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#include "ports/micropython/nativecall.h"

typedef struct _mp_wasm_ready_fun_t {
    mp_obj_base_t base;
    char fqn[96];
    char export_name[96];
} mp_wasm_ready_fun_t;

static mp_obj_t ready_fun_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)n_kw;
    mp_wasm_ready_fun_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_wasm_native_call(self->fqn, self->export_name, n_args, args);
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_ready_fun,
    MP_QSTR_function,
    MP_TYPE_FLAG_BINDS_SELF,
    call, ready_fun_call);

static mp_obj_t make_ready_fun(const char *fqn, const char *export_name) {
    mp_wasm_ready_fun_t *o = m_new_obj(mp_wasm_ready_fun_t);
    o->base.type = &mp_type_wasm_ready_fun;
    strncpy(o->fqn, fqn, sizeof(o->fqn) - 1);
    o->fqn[sizeof(o->fqn) - 1] = '\0';
    strncpy(o->export_name, export_name, sizeof(o->export_name) - 1);
    o->export_name[sizeof(o->export_name) - 1] = '\0';
    return MP_OBJ_FROM_PTR(o);
}

/* pm_<path_underscores>_<leaf> → leaf; fqn "a.b.c" → prefix "pm_b_c_". */
static void attr_from_export(const char *fqn, const char *cname, char *out, size_t out_sz) {
    char prefix[96];
    size_t pi = 0;
    prefix[pi++] = 'p';
    prefix[pi++] = 'm';
    prefix[pi++] = '_';
    const char *p = strchr(fqn, '.');
    if (p != NULL) {
        p++;
        while (*p != '\0' && pi + 1 < sizeof(prefix)) {
            prefix[pi++] = (*p == '.') ? '_' : *p;
            p++;
        }
    }
    if (pi + 1 < sizeof(prefix)) {
        prefix[pi++] = '_';
    }
    prefix[pi] = '\0';

    if (strncmp(cname, prefix, pi) == 0 && cname[pi] != '\0') {
        size_t n = strlen(cname + pi);
        if (n >= out_sz) {
            n = out_sz - 1;
        }
        memcpy(out, cname + pi, n);
        out[n] = '\0';
        return;
    }
    const char *us = strrchr(cname, '_');
    const char *src = (us != NULL && us[1] != '\0') ? us + 1 : cname;
    size_t n = strlen(src);
    if (n >= out_sz) {
        n = out_sz - 1;
    }
    memcpy(out, src, n);
    out[n] = '\0';
}

static int module_has_attr(mp_obj_t module, qstr attr) {
    mp_obj_t dest[2];
    mp_load_method_maybe(module, attr, dest);
    return dest[0] != MP_OBJ_NULL;
}

static void attach_registry_exports(const char *fqn, mp_obj_t module) {
    size_t flen = strlen(fqn);
    uint32_t n = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)flen);
    for (uint32_t i = 0; i < n; ++i) {
        uint8_t name[128];
        uint32_t name_len = sizeof(name);
        uint8_t sig[160];
        uint32_t sig_len = sizeof(sig);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (!pm_wasmmod_registry_export_at((const uint8_t *)fqn, (uint32_t)flen, i,
                name, &name_len, &kind, sig, &sig_len)) {
            continue;
        }
        if (kind != PM_WASMMOD_REGISTRY_EXPORT_FN
            && kind != PM_WASMMOD_REGISTRY_EXPORT_BUFPTR
            && kind != PM_WASMMOD_REGISTRY_EXPORT_MEM
            && kind != PM_WASMMOD_REGISTRY_EXPORT_OBJ
            && kind != PM_WASMMOD_REGISTRY_EXPORT_I64
            && kind != PM_WASMMOD_REGISTRY_EXPORT_F32
            && kind != PM_WASMMOD_REGISTRY_EXPORT_F64) {
            continue;
        }
        name[name_len < sizeof(name) ? name_len : sizeof(name) - 1] = '\0';
        char attr[96];
        attr_from_export(fqn, (const char *)name, attr, sizeof(attr));
        qstr qattr = qstr_from_str(attr);
        /* Keep existing Python defs (impl=py); fill gaps for C/RS namespace dirs. */
        if (module_has_attr(module, qattr)) {
            continue;
        }
        mp_obj_t fun = make_ready_fun(fqn, (const char *)name);
        mp_store_attr(module, qattr, fun);
    }
}

int mp_wasm_host_ready(const char *fqn, mp_obj_t module) {
    if (fqn == NULL || module == MP_OBJ_NULL) {
        return -1;
    }
    if (!mp_obj_is_type(module, &mp_type_module)) {
        return -1;
    }

    int n = pm_wasmmod_pyexport_bind_module(fqn, module);
    attach_registry_exports(fqn, module);
    return n;
}
