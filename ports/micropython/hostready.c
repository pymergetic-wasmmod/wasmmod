#include "ports/micropython/hostready.h"

#include <string.h>

#include "py/obj.h"
#include "py/runtime.h"

#include "pymergetic/util/limits.h"
#include "pymergetic/wasmmod/pyexport.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"

#include "ports/micropython/finder.h"
#include "ports/micropython/nativecall.h"

typedef struct _mp_wasm_ready_fun_t {
    mp_obj_base_t base;
    char fqn[96];
    char export_name[96];
} mp_wasm_ready_fun_t;

static mp_obj_t ready_fun_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_wasm_ready_fun_t *self = MP_OBJ_TO_PTR(self_in);
    /* Keyword args ride along: the card faces are declared as plain
     * positional C prototypes, so kw args fold onto the tail as
     * positionals (kwargs-from-dict order is the caller's declaration
     * order — the .pyi faces spell them positionally for exactly this
     * reason). */
    if (n_kw == 0) {
        return mp_wasm_native_call(self->fqn, self->export_name, n_args, args);
    }
    {
        /* args layout: n_args positionals, then n_kw key/value pairs */
        mp_obj_t flat[n_args + n_kw];
        size_t i;
        if (n_args > 0) {
            memcpy(flat, args, n_args * sizeof(mp_obj_t));
        }
        for (i = 0; i < n_kw; i++) {
            flat[n_args + i] = args[n_args + 2u * i + 1u];
        }
        return mp_wasm_native_call(self->fqn, self->export_name, n_args + n_kw, flat);
    }
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

/* ---- a card's knobs, under the card -----------------------------------------
 *
 * A knob belongs to a module the way an export does, so it reads that way from
 * Python: `m.net.ip.limits.socket` is the socket knob of pymergetic.metal.net.ip
 * and `= 4096` moves it. The same object comes back from
 * `limits.of("pymergetic.metal.net.ip")` for code that has the name rather than
 * the module, and from a namespace, where it covers everything below —
 * `m.drivers.limits.net.sim.device`.
 *
 * It holds a module name, not a knob: the values live in the cards, so nothing
 * here goes stale when a seat moves one.
 */
typedef struct _mp_wasm_limits_t {
    mp_obj_base_t base;
    uint16_t nlen;
    char name[];
} mp_wasm_limits_t;

extern const mp_obj_type_t mp_type_wasm_limits;

mp_obj_t mp_wasm_limits_view(const char *module) {
    size_t nlen;
    if (module == NULL) {
        return MP_OBJ_NULL;
    }
    nlen = strlen(module);
    if (nlen == 0 || nlen >= MP_WASM_FQN_MAX) {
        return MP_OBJ_NULL;
    }
    /* Nothing under it is nothing to show: the attribute is simply not there,
     * which is what a card with no capacity of its own should look like. */
    if (pm_util_limits_count_under(module) == 0u) {
        return MP_OBJ_NULL;
    }
    mp_wasm_limits_t *v = m_new_obj_var(mp_wasm_limits_t, name, char, nlen + 1);
    v->base.type = &mp_type_wasm_limits;
    v->nlen = (uint16_t)nlen;
    memcpy(v->name, module, nlen);
    v->name[nlen] = '\0';
    return MP_OBJ_FROM_PTR(v);
}

static void limits_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_wasm_limits_t *v = MP_OBJ_TO_PTR(self_in);
    size_t alen;
    const char *an = (const char *)qstr_data(attr, &alen);
    int32_t k;

    if (dest[0] == MP_OBJ_NULL) {
        if (attr == MP_QSTR___name__) {
            dest[0] = mp_obj_new_str(v->name, v->nlen);
            return;
        }
        k = pm_util_limits_find_of(v->name, an);
        if (k >= 0) {
            dest[0] = mp_obj_new_int_from_uint(pm_util_limits_soft(k));
            return;
        }
        /* Not a knob of this card — a card below it, then. */
        if ((size_t)v->nlen + 1 + alen < MP_WASM_FQN_MAX) {
            char child[MP_WASM_FQN_MAX];
            memcpy(child, v->name, v->nlen);
            child[v->nlen] = '.';
            memcpy(child + v->nlen + 1, an, alen);
            child[v->nlen + 1 + alen] = '\0';
            mp_obj_t under = mp_wasm_limits_view(child);
            if (under != MP_OBJ_NULL) {
                dest[0] = under;
            }
        }
        return;
    }
    if (dest[0] != MP_OBJ_SENTINEL) {
        return;
    }
    /* A store. Only a knob of this card can take one — a branch is not a
     * number, and deleting a capacity means nothing. */
    k = pm_util_limits_find_of(v->name, an);
    if (k < 0 || dest[1] == MP_OBJ_NULL) {
        return;
    }
    {
        mp_uint_t want = mp_obj_get_int_truncated(dest[1]);
        int32_t rc = pm_util_limits_set_at(k, (pm_mod_value_t)want);
        if (rc == -2) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("%s: %u is past the hard limit %u"),
                pm_util_limits_name(k), (unsigned)want,
                (unsigned)pm_util_limits_hard(k));
        }
        if (rc == -3) {
            mp_raise_msg_varg(&mp_type_ValueError,
                MP_ERROR_TEXT("%s: the card could not take %u"),
                pm_util_limits_name(k), (unsigned)want);
        }
        if (rc != 0) {
            mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s: refused"),
                pm_util_limits_name(k));
        }
    }
    dest[0] = MP_OBJ_NULL; /* stored */
}

/* Printing is the listing: what this card counts, and where each knob stands. */
static void limits_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_wasm_limits_t *v = MP_OBJ_TO_PTR(self_in);
    uint32_t own = pm_util_limits_count_of(v->name);
    uint32_t i;
    mp_printf(print, "<limits '%s'", v->name);
    for (i = 0; i < own; i++) {
        int32_t k = pm_util_limits_nth_of(v->name, (int32_t)i);
        if (k < 0) {
            break;
        }
        mp_printf(print, "%s %s=%u", i == 0 ? ":" : ",", pm_util_limits_leaf(k),
            (unsigned)pm_util_limits_soft(k));
    }
    if (own != pm_util_limits_count_under(v->name)) {
        mp_printf(print, "%s %u under", own == 0 ? ":" : ",",
            (unsigned)(pm_util_limits_count_under(v->name) - own));
    }
    mp_printf(print, ">");
}

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_limits,
    MP_QSTR_module,
    MP_TYPE_FLAG_NONE,
    print, limits_print,
    attr, limits_attr
);

/* pymergetic.util.limits.of(module) — the same view, for a caller holding the
 * name. The card's own faces (count_of, nth_of, find_of, module, leaf) answer
 * by index; this is the one that reads like the module it is about. */
static mp_obj_t limits_of(mp_obj_t module_in) {
    const char *module;
    mp_obj_t view;
    if (mp_obj_is_str(module_in)) {
        module = mp_obj_str_get_str(module_in);
    } else {
        /* A module object, so `limits.of(m.net.ip)` works as well as its name. */
        mp_obj_t nm = mp_load_attr(module_in, MP_QSTR___name__);
        module = mp_obj_str_get_str(nm);
    }
    view = mp_wasm_limits_view(module);
    if (view == MP_OBJ_NULL) {
        mp_raise_msg_varg(&mp_type_ValueError,
            MP_ERROR_TEXT("%s has no knobs"), module);
    }
    return view;
}
static MP_DEFINE_CONST_FUN_OBJ_1(limits_of_obj, limits_of);

/* Every card that counts something gets its knobs under it, by name. */
static void attach_limits(const char *fqn, mp_obj_t module) {
    mp_obj_t view = mp_wasm_limits_view(fqn);
    if (view != MP_OBJ_NULL) {
        mp_store_attr(module, qstr_from_str("limits"), view);
    }
    if (strcmp(fqn, "pymergetic.util.limits") == 0) {
        mp_store_attr(module, qstr_from_str("of"), MP_OBJ_FROM_PTR(&limits_of_obj));
    }
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
        uint8_t sig[256];
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
    attach_limits(fqn, module);
    return n;
}
