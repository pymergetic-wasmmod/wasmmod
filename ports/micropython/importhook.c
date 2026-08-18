/*
 * µPy __import__ wrap: pack-on-path before/after ImportError; presence → registry.
 */

#include <string.h>

#include "py/builtin.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/qstr.h"
#include "py/runtime.h"

#include "ports/common/boot.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/hostready.h"
#include "ports/micropython/importhook.h"
#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/registry/__exports__.h"

#ifndef MICROPY_WASM_VERSION
#define MICROPY_WASM_VERSION PYMERGETIC_WASMMOD_VERSION
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_prev_import);
MP_REGISTER_ROOT_POINTER(mp_obj_t mp_wasm_catalog_cache);

static int mp_wasm_hook_depth;
static int mp_wasm_inited;

void mp_wasm_presence_publish(const char *name) {
    pm_wasmmod_host_presence_publish(name);
}

/* A ROM module's globals cannot take new attrs; binding into one raises. */
static bool mp_wasm_module_is_writable(mp_obj_t module) {
    if (!mp_obj_is_type(module, &mp_type_module)) {
        return false;
    }
    mp_obj_module_t *m = MP_OBJ_TO_PTR(module);
    return !m->globals->map.is_fixed;
}

/* Presence + auto-ready (bind_py + attach typed funobjs). Membership comes
 * from the registry, not from a baked name prefix. */
static void mp_wasm_after_import(const char *name) {
    mp_wasm_presence_publish(name);
    if (name == NULL
        || !pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)strlen(name))) {
        return;
    }
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL && mp_wasm_module_is_writable(el->value)) {
        mp_wasm_host_ready(name, el->value);
    }
}

static void pm_wasm_sync_sys_modules_to_registry(void) {
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    for (size_t i = 0; i < map->alloc; ++i) {
        if (!mp_map_slot_is_filled(map, i) || !mp_obj_is_qstr(map->table[i].key)) {
            continue;
        }
        mp_wasm_presence_publish(qstr_str(MP_OBJ_QSTR_VALUE(map->table[i].key)));
    }
}

typedef struct _mp_wasm_walk_t {
    mp_obj_base_t base;
    mp_obj_t child;
    uint16_t nlen;
    char name[];
} mp_wasm_walk_t;

static void mp_wasm_walk_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_wasm_walk_t *w = MP_OBJ_TO_PTR(self_in);
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }
    size_t alen;
    const byte *an = qstr_data(attr, &alen);
    if (alen == (size_t)w->nlen && memcmp(an, w->name, alen) == 0) {
        dest[0] = w->child;
    }
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_walk,
    MP_QSTR_module,
    MP_TYPE_FLAG_NONE,
    attr, mp_wasm_walk_attr
);

/* Guest pack lives under a ROM host package (pymergetic.*). Vanilla
 * __import__ walks that ROM dict and misses the namespace shell. Finish
 * here: sys.modules under the IMPORT_NAME qstr, return top or leaf like
 * CPython (`import a.b.c` → a, `from a.b.c import x` → leaf). */
static mp_obj_t mp_wasm_finish_pack_import(size_t n_args, const mp_obj_t *args, mp_obj_t leaf) {
    size_t n;
    const char *s = mp_obj_str_get_data(args[0], &n);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)), args[0], leaf);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
        MP_OBJ_NEW_QSTR(qstr_from_strn(s, n)), leaf);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] != '.') {
            continue;
        }
        qstr pq = qstr_from_strn(s, i);
        if (mp_wasm_is_host_face(qstr_str(pq))) {
            continue;
        }
        mp_obj_t pmod = mp_obj_new_module(pq);
        mp_obj_module_t *mod = MP_OBJ_TO_PTR(pmod);
        if (mod->globals->map.is_fixed) {
            continue;
        }
        mp_obj_t g = MP_OBJ_FROM_PTR(mod->globals);
        if (mp_map_lookup(&mod->globals->map, MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP)
            == NULL) {
            mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___path__), mp_obj_new_str(s, i));
        }
    }
    /* Leaf on its parent namespace (skip ROM host). */
    {
        const char *dot = NULL;
        for (size_t i = 0; i < n; ++i) {
            if (s[i] == '.') {
                dot = s + i;
            }
        }
        if (dot != NULL) {
            qstr pq = qstr_from_strn(s, (size_t)(dot - s));
            if (!mp_wasm_is_host_face(qstr_str(pq))) {
                mp_obj_t pmod = mp_obj_new_module(pq);
                mp_obj_module_t *mod = MP_OBJ_TO_PTR(pmod);
                if (!mod->globals->map.is_fixed) {
                    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod->globals),
                        MP_OBJ_NEW_QSTR(qstr_from_strn(dot + 1,
                            (size_t)((s + n) - (dot + 1)))), leaf);
                }
            }
        }
    }
    mp_obj_t fromlist = n_args >= 4 ? args[3] : mp_const_none;
    if (fromlist != mp_const_none && fromlist != mp_const_false) {
        return leaf;
    }
    /* `import a.b.c as d` LOAD_ATTRs remaining names. Do not return ROM
     * `a` (fixed dict) and do not key a heap module by interned qstr —
     * UEFI qstr ids for a slice of the import name can miss the compiler
     * intern. Walk objects strcmp the component bytes. */
    const char *parts[8];
    size_t lens[8];
    size_t nparts = 0;
    size_t start = 0;
    for (size_t k = 0; k <= n; ++k) {
        if (k < n && s[k] != '.') {
            continue;
        }
        if (nparts < 8) {
            parts[nparts] = s + start;
            lens[nparts] = k - start;
            nparts++;
        }
        start = k + 1;
    }
    if (nparts < 2) {
        return leaf;
    }
    mp_obj_t cur = leaf;
    for (size_t p = nparts; p > 1;) {
        --p;
        mp_wasm_walk_t *w = m_new_obj_var(mp_wasm_walk_t, name, char, lens[p] + 1);
        w->base.type = &mp_type_wasm_walk;
        w->child = cur;
        w->nlen = (uint16_t)lens[p];
        memcpy(w->name, parts[p], lens[p]);
        w->name[lens[p]] = '\0';
        cur = MP_OBJ_FROM_PTR(w);
    }
    return cur;
}

static mp_obj_t mp_wasm_registry_module(const char *name);
static void mp_wasm_link_registry_parents(const char *name, mp_obj_t leaf);

/* C/RS muscle is already in the image. impl=py starts with empty native
 * slots and must run the .py via default import. Do not js.fetch / CDN a
 * resident card — that OOBs the emcc asyncify path after inspect. */
static int mp_wasm_resident_muscle(const char *name) {
    size_t nlen;
    uint32_t n;
    uint32_t i;
    if (name == NULL || !mp_wasm_is_host_face(name)) {
        return 0;
    }
    nlen = strlen(name);
    n = pm_wasmmod_registry_export_count((const uint8_t *)name, (uint32_t)nlen);
    for (i = 0; i < n; i++) {
        uint8_t ename[128];
        uint32_t elen = sizeof(ename);
        uint8_t sig[160];
        uint32_t slen = sizeof(sig);
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (!pm_wasmmod_registry_export_at((const uint8_t *)name, (uint32_t)nlen, i,
                ename, &elen, &kind, sig, &slen)) {
            continue;
        }
        if (pm_wasmmod_registry_resolve_native((const uint8_t *)name, (uint32_t)nlen,
                ename, elen) != NULL) {
            return 1;
        }
    }
    return 0;
}

static mp_obj_t mp_wasm_import_resident(size_t n_args, const mp_obj_t *args, const char *name) {
    mp_obj_t native = mp_wasm_registry_module(name);
    if (native == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    }
    mp_wasm_link_registry_parents(name, native);
    mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, native);
    mp_wasm_after_import(name);
    return res;
}

/* Direct children of `name` in the registry. `into` may be NULL to just test
 * for existence; the walk stops at the first hit in that case. */
static bool mp_wasm_registry_children(const char *name, size_t nlen, mp_obj_t into) {
    uint32_t count = pm_wasmmod_registry_module_count();
    bool any = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t fqn[MP_WASM_FQN_MAX];
        uint32_t len = sizeof(fqn);
        if (!pm_wasmmod_registry_module_at(i, fqn, &len) || len <= nlen + 1) {
            continue;
        }
        if (fqn[nlen] != '.' || memcmp(fqn, name, nlen) != 0) {
            continue;
        }
        any = true;
        if (into == MP_OBJ_NULL) {
            return true;
        }
        /* Only the next component: a.b.c and a.b.c.d both contribute `c`. */
        size_t leaf = nlen + 1;
        size_t end = leaf;
        while (end < len && fqn[end] != '.') {
            ++end;
        }
        char child[MP_WASM_FQN_MAX];
        if (end >= MP_WASM_FQN_MAX) {
            continue;
        }
        memcpy(child, fqn, end);
        child[end] = '\0';
        mp_obj_t cmod = mp_wasm_registry_module(child);
        if (cmod != MP_OBJ_NULL) {
            mp_obj_dict_store(into,
                MP_OBJ_NEW_QSTR(qstr_from_strn(child + leaf, end - leaf)), cmod);
        }
    }
    return any;
}

/* A namespace node has no code of its own, only registered descendants. It
 * resolves children on attribute access so touching a package never binds the
 * whole subtree, and it works at any depth without a per-name registration. */
typedef struct _mp_wasm_ns_t {
    mp_obj_base_t base;
    uint16_t nlen;
    char name[];
} mp_wasm_ns_t;

static void mp_wasm_ns_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    mp_wasm_ns_t *ns = MP_OBJ_TO_PTR(self_in);
    if (dest[0] != MP_OBJ_NULL) {
        return;
    }
    if (attr == MP_QSTR___name__) {
        dest[0] = mp_obj_new_str(ns->name, ns->nlen);
        return;
    }
    if (attr == MP_QSTR___dict__) {
        mp_obj_t d = mp_obj_new_dict(0);
        mp_wasm_registry_children(ns->name, ns->nlen, d);
        dest[0] = d;
        return;
    }
    size_t alen;
    const char *an = (const char *)qstr_data(attr, &alen);
    if ((size_t)ns->nlen + 1 + alen >= MP_WASM_FQN_MAX) {
        return;
    }
    char child[MP_WASM_FQN_MAX];
    memcpy(child, ns->name, ns->nlen);
    child[ns->nlen] = '.';
    memcpy(child + ns->nlen + 1, an, alen);
    child[ns->nlen + 1 + alen] = '\0';
    mp_obj_t cmod = mp_wasm_registry_module(child);
    if (cmod != MP_OBJ_NULL) {
        dest[0] = cmod;
    }
}

static void mp_wasm_ns_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    mp_wasm_ns_t *ns = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<module '%s'>", ns->name);
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_ns,
    MP_QSTR_module,
    MP_TYPE_FLAG_NONE,
    print, mp_wasm_ns_print,
    attr, mp_wasm_ns_attr
);

/* Registry → module. An exact FQN with exports becomes a bound module; a node
 * with registered descendants becomes a namespace. Anything else stays
 * MP_OBJ_NULL so an unknown name fails as ImportError rather than importing
 * as a silent empty shell. */
static mp_obj_t mp_wasm_registry_module(const char *name) {
    if (name == NULL) {
        return MP_OBJ_NULL;
    }
    size_t nlen = strlen(name);
    if (nlen == 0 || nlen >= MP_WASM_FQN_MAX) {
        return MP_OBJ_NULL;
    }
    qstr q = qstr_from_strn(name, nlen);
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(q), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        return el->value;
    }
    /* A ROM/builtin module owns this name — hand it back rather than shadow
     * it with a heap object of the same qstr. */
    mp_obj_t builtin = mp_module_get_builtin(q, false);
    if (builtin == MP_OBJ_NULL) {
        builtin = mp_module_get_builtin(q, true);
    }
    if (builtin != MP_OBJ_NULL) {
        return builtin;
    }
    if (pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)nlen)
        && pm_wasmmod_registry_export_count((const uint8_t *)name, (uint32_t)nlen) > 0) {
        mp_obj_t mod = mp_obj_new_module(q);
        if (mp_wasm_module_is_writable(mod)) {
            mp_wasm_host_ready(name, mod);
        }
        return mod;
    }
    if (!mp_wasm_registry_children(name, nlen, MP_OBJ_NULL)) {
        return MP_OBJ_NULL;
    }
    mp_wasm_ns_t *ns = m_new_obj_var(mp_wasm_ns_t, name, char, nlen + 1);
    ns->base.type = &mp_type_wasm_ns;
    ns->nlen = (uint16_t)nlen;
    memcpy(ns->name, name, nlen);
    ns->name[nlen] = '\0';
    mp_obj_t obj = MP_OBJ_FROM_PTR(ns);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
        MP_OBJ_NEW_QSTR(q), obj);
    return obj;
}

/* Walk the dotted ancestors of `name`, materialise each as a namespace module
 * and hang the child off it, so an attribute walk reaches the leaf. Ancestors
 * with a fixed ROM dict take nothing; the delegation hook resolves those. */
static void mp_wasm_link_registry_parents(const char *name, mp_obj_t leaf) {
    mp_obj_t child = leaf;
    size_t end = strlen(name);
    while (end > 0) {
        size_t dot = end;
        while (dot > 0 && name[dot - 1] != '.') {
            --dot;
        }
        if (dot == 0) {
            return;
        }
        char parent[MP_WASM_FQN_MAX];
        memcpy(parent, name, dot - 1);
        parent[dot - 1] = '\0';
        mp_obj_t pmod = mp_wasm_registry_module(parent);
        if (pmod == MP_OBJ_NULL) {
            return;
        }
        if (mp_wasm_module_is_writable(pmod)) {
            mp_obj_module_t *pm = MP_OBJ_TO_PTR(pmod);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(pm->globals),
                MP_OBJ_NEW_QSTR(qstr_from_strn(name + dot, end - dot)), child);
        }
        child = pmod;
        end = dot - 1;
    }
}

#if MICROPY_MODULE_ATTR_DELEGATION
/* `pkg.<attr>` for a ROM package whose fixed dict cannot hold new children.
 * The parent name comes from the package's own __name__, so this works at any
 * depth; misses fall through to the registry. */
void mp_wasm_pymergetic_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    if (dest[0] != MP_OBJ_NULL || !mp_obj_is_type(self_in, &mp_type_module)) {
        return;
    }
    mp_obj_module_t *pkg = MP_OBJ_TO_PTR(self_in);
    mp_map_elem_t *nel = mp_map_lookup(&pkg->globals->map,
        MP_OBJ_NEW_QSTR(MP_QSTR___name__), MP_MAP_LOOKUP);
    if (nel == NULL || !mp_obj_is_qstr(nel->value)) {
        return;
    }
    size_t plen;
    const char *pkg_name = (const char *)qstr_data(MP_OBJ_QSTR_VALUE(nel->value), &plen);
    size_t llen;
    const char *leaf = (const char *)qstr_data(attr, &llen);
    if (plen + 1 + llen >= MP_WASM_FQN_MAX) {
        return;
    }
    char child[MP_WASM_FQN_MAX];
    memcpy(child, pkg_name, plen);
    child[plen] = '.';
    memcpy(child + plen + 1, leaf, llen);
    child[plen + 1 + llen] = '\0';

    qstr child_q = qstr_from_str(child);
    mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(child_q), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        dest[0] = el->value;
        return;
    }
    /* A statically registered child (MP_REGISTER_MODULE of the dotted name),
     * still unimported. Reaching it here is what lets the package dict hold no
     * child at all — a downstream tree's modules arrive by registration, and
     * every seat resolves them the same way whether or not it links the TU that
     * used to spell them out. */
    mp_obj_t mod = mp_module_get_builtin(child_q, false);
    if (mod != MP_OBJ_NULL) {
        dest[0] = mod;
        return;
    }
    mod = mp_wasm_registry_module(child);
    if (mod != MP_OBJ_NULL) {
        dest[0] = mod;
    }
}
#endif

/* µPy-native CDN catalog namespace check, mirroring the `packages()` fold in
 * modmetal.c: every µPy seat has the `json` module and `wasmmod.net.cdn`.
 * `fetch_index` returns the channel index bytes; `json.loads` parses it into
 * the `packages` dict — no serde / Rust card is dragged in. The parsed dict is
 * fetched once and cached for the seat lifetime (like `packages_catalog()`),
 * so a failed import doesn't re-hit the network each time. A fetch/parse
 * failure (offline seat, CDN without an index, transient) caches the failure
 * sentinel and reports no namespaces: a namespace import must never couple to
 * a reachable index, or a plain unknown name would raise OSError mid-import
 * instead of a normal ImportError.
 *
 * The cdn + json modules are resolved from sys.modules / builtins, not via a
 * re-entrant `mp_import_name`: this can run while the hook is mid-import, and
 * importing the bare `pymergetic` umbrella would publish it as a RESIDENT
 * host-face card, poisoning every guest pack in its subtree. */
static mp_obj_t mp_wasm_sys_module(const char *name) {
    mp_map_t *map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_elem_t *el = mp_map_lookup(map, MP_OBJ_NEW_QSTR(qstr_from_str(name)),
        MP_MAP_LOOKUP);
    if (el == NULL || el->value == MP_OBJ_NULL) {
        return MP_OBJ_NULL;
    }
    return el->value;
}

static mp_obj_t mp_wasm_catalog_packages(void) {
    if (MP_STATE_VM(mp_wasm_catalog_cache) == MP_OBJ_NULL) {
        nlr_buf_t nlr;
        mp_obj_t packages = mp_const_none;
        if (nlr_push(&nlr) == 0) {
            mp_obj_t cdnmod = mp_wasm_sys_module("pymergetic.wasmmod.net.cdn");
            mp_obj_t json_mod = mp_wasm_sys_module("json");
            if (json_mod == MP_OBJ_NULL) {
                json_mod = mp_module_get_builtin(qstr_from_str("json"), false);
                if (json_mod == MP_OBJ_NULL) {
                    json_mod = mp_module_get_builtin(qstr_from_str("json"), true);
                }
            }
            if (cdnmod != MP_OBJ_NULL && json_mod != MP_OBJ_NULL) {
                mp_obj_t fetch = mp_load_attr(cdnmod, qstr_from_str("fetch_index"));
                mp_obj_t bytes = mp_call_function_1(fetch, mp_obj_new_str("lead", 4));
                mp_obj_t loads = mp_load_attr(json_mod, qstr_from_str("loads"));
                mp_obj_t doc = mp_call_function_1(loads, bytes);
                packages = mp_obj_dict_get(doc, MP_OBJ_NEW_QSTR(qstr_from_str("packages")));
            }
            nlr_pop();
        } else {
            packages = mp_const_none;
        }
        MP_STATE_VM(mp_wasm_catalog_cache) = packages;
    }
    return MP_STATE_VM(mp_wasm_catalog_cache);
}

/* Does any catalog pack live at `name.X`? i.e. is `name` a namespace root.
 * A missing/unreachable index reports no namespaces (see above). */
static bool mp_wasm_catalog_ns_has(const char *name) {
    size_t nlen = strlen(name);
    mp_obj_t packages = mp_wasm_catalog_packages();
    if (packages == mp_const_none) {
        return false;
    }
    /* Dict keys from json.loads are str objects (not necessarily interned
     * qstrs); iterate with a bytes extraction that accepts either. */
    mp_map_t *map = mp_obj_dict_get_map(packages);
    for (size_t i = 0; i < map->alloc; ++i) {
        if (!mp_map_slot_is_filled(map, i)) {
            continue;
        }
        mp_obj_t kobj = map->table[i].key;
        if (mp_obj_is_qstr(kobj)) {
            size_t klen;
            const char *key = (const char *)qstr_data(MP_OBJ_QSTR_VALUE(kobj), &klen);
            if (klen > nlen && key[nlen] == '.' && memcmp(key, name, nlen) == 0) {
                return true;
            }
        } else if (mp_obj_is_str(kobj)) {
            GET_STR_DATA_LEN(kobj, key, klen);
            if (klen > nlen && key[nlen] == '.' && memcmp(key, name, nlen) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* PEP 420 namespace package. A dotted name that owns no code of its own but
 * is a real container in the CDN catalog (some pack lives at `name.X`) imports
 * as a writable namespace whose children resolve lazily on attribute access —
 * `mp_wasm_pymergetic_attr` / the registry fall through to a CDN fetch for
 * guest leaves. Deliberately catalog-backed: we never invent a namespace for a
 * name the catalog does not know, so a typo or a `from X import Y` child stays
 * a normal ImportError instead of binding the import name to an empty module. */
static mp_obj_t mp_wasm_build_namespace(const char *name, size_t nlen) {
    qstr q = qstr_from_strn(name, nlen);
    mp_obj_t mod = mp_obj_new_module(q);
    mp_obj_module_t *m = MP_OBJ_TO_PTR(mod);
    mp_obj_t g = MP_OBJ_FROM_PTR(m->globals);
    if (mp_map_lookup(&m->globals->map, MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP)
        == NULL) {
        mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___path__),
            mp_obj_new_str(name, nlen));
    }
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
        MP_OBJ_NEW_QSTR(q), mod);
    return mod;
}

mp_obj_t mp_wasm_builtin_import(size_t n_args, const mp_obj_t *args) {
    if (mp_wasm_hook_depth == 0 && n_args >= 1 && mp_obj_is_str(args[0])) {
        const char *name = mp_obj_str_get_str(args[0]);
        mp_map_elem_t *el = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
            MP_OBJ_NEW_QSTR(qstr_from_str(name)), MP_MAP_LOOKUP);
        if (el == NULL || el->value == MP_OBJ_NULL) {
            if (mp_wasm_resident_muscle(name)) {
                mp_obj_t res = mp_wasm_import_resident(n_args, args, name);
                if (res != MP_OBJ_NULL) {
                    return res;
                }
            }
            vstr_t path;
            if (mp_wasm_find_pack(name, &path)) {
                vstr_clear(&path);
                mp_wasm_hook_depth++;
                nlr_buf_t nlr_pack;
                if (nlr_push(&nlr_pack) == 0) {
                    mp_obj_t pack = mp_wasm_import_pack(name);
                    nlr_pop();
                    mp_wasm_hook_depth--;
                    mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, pack);
                    mp_wasm_after_import(name);
                    return res;
                }
                mp_wasm_hook_depth--;
                nlr_jump(nlr_pack.ret_val);
            }
        }
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        /* Never call mp_builtin___import___obj — with the compile-time
         * wrap that funobj is this function. */
        mp_obj_t res = mp_builtin___import___default(n_args, args);
        nlr_pop();
        if (mp_wasm_hook_depth == 0 && n_args >= 1 && mp_obj_is_str(args[0])) {
            mp_wasm_after_import(mp_obj_str_get_str(args[0]));
        }
        return res;
    }

    mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
    if (!mp_obj_exception_match(exc, MP_OBJ_FROM_PTR(&mp_type_ImportError))
        || mp_wasm_hook_depth > 0 || n_args < 1 || !mp_obj_is_str(args[0])) {
        nlr_jump(nlr.ret_val);
    }

    const char *name = mp_obj_str_get_str(args[0]);
    /* Builtins and the filesystem win first; the registry fills the gap they
     * leave, before we reach for a pack over the network. */
    mp_obj_t native = mp_wasm_registry_module(name);
    if (native != MP_OBJ_NULL) {
        mp_wasm_link_registry_parents(name, native);
        mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, native);
        mp_wasm_after_import(name);
        return res;
    }
    /* PEP 420 container: a dotted name with a real pack-bearing subtree in the
     * CDN catalog, but no code and (on a cold seat) no loaded leaf. Import it
     * as a namespace package; children resolve lazily. A plain unknown name, or
     * a `from X import Y` child, fails the catalog check and stays a normal
     * ImportError. The check is µPy-native over cdn.fetch_index + json (upy
     * has json on board); no serde card is involved. */
    if (strchr(name, '.') != NULL && mp_wasm_catalog_ns_has(name)) {
        mp_obj_t ns = mp_wasm_build_namespace(name, strlen(name));
        mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, ns);
        /* Deliberately no mp_wasm_after_import: publishing this container as a
         * host presence/card would make every child pack a "host face" and
         * block the leaf fetch (`import a.b.leaf`). A namespace owns no own
         * exports and must not shadow its subtree. */
        return res;
    }
    mp_wasm_hook_depth++;
    nlr_buf_t nlr2;
    if (nlr_push(&nlr2) == 0) {
        mp_obj_t pack = mp_wasm_import_pack(name);
        nlr_pop();
        mp_wasm_hook_depth--;
        mp_obj_t res = mp_wasm_finish_pack_import(n_args, args, pack);
        mp_wasm_after_import(name);
        return res;
    }
    mp_wasm_hook_depth--;
    nlr_jump(nlr2.ret_val);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_import_hook_obj, 1, 5, mp_wasm_builtin_import);

static mp_obj_t mod_wasm_install_hook(void) {
#if MICROPY_CAN_OVERRIDE_BUILTINS
    if (MP_STATE_VM(mp_wasm_prev_import) != MP_OBJ_NULL) {
        pm_wasm_sync_sys_modules_to_registry();
        return mp_const_none;
    }
    mp_obj_t dest[2];
    mp_load_method_maybe(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__, dest);
    MP_STATE_VM(mp_wasm_prev_import) =
        dest[0] != MP_OBJ_NULL ? dest[0] : MP_OBJ_FROM_PTR(&mp_builtin___import___obj);
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__,
        MP_OBJ_FROM_PTR(&mod_wasm_import_hook_obj));
    pm_wasm_sync_sys_modules_to_registry();
    return mp_const_none;
#else
    mp_raise_NotImplementedError(MP_ERROR_TEXT("install_hook needs MICROPY_CAN_OVERRIDE_BUILTINS"));
#endif
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_install_hook_obj, mod_wasm_install_hook);

/* Port init: put the hook in place before any user code runs, so a cold
 * `import a.b.c` is caught. Deliberately does not boot the host — that stays
 * lazy in mp_wasm_ensure_inited(). */
void mp_wasm_port_init(void) {
#if MICROPY_CAN_OVERRIDE_BUILTINS
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        (void)mod_wasm_install_hook();
    }
#endif
}

void mp_wasm_ensure_inited(void) {
    if (!mp_wasm_inited) {
        /* A host kernel boots from its own package __init__, so its PM_MOD_BOOT
         * cards can queue before this. */
        if (pm_wasmmod_host_boot("pymergetic.wasmmod", MICROPY_WASM_VERSION) != 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("wasmmod loader_init failed"));
        }
        mp_wasm_inited = 1;
        (void)mp_wasm_path_obj();
    }
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        (void)mod_wasm_install_hook();
    }
}

static mp_obj_t mod_wasm_uninstall_hook(void) {
#if MICROPY_CAN_OVERRIDE_BUILTINS
    if (MP_STATE_VM(mp_wasm_prev_import) == MP_OBJ_NULL) {
        return mp_const_none;
    }
    mp_store_attr(MP_OBJ_FROM_PTR(&mp_module_builtins), MP_QSTR___import__,
        MP_STATE_VM(mp_wasm_prev_import));
    MP_STATE_VM(mp_wasm_prev_import) = MP_OBJ_NULL;
#endif
    pm_wasmmod_net_cdn_reset();
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj, mod_wasm_uninstall_hook);

static mp_obj_t mod_wasm_publish_presence(mp_obj_t name_in) {
    mp_wasm_ensure_inited();
    mp_wasm_presence_publish(mp_obj_str_get_str(name_in));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_publish_presence_obj, mod_wasm_publish_presence);

/* Live inventory. Inspect (and anyone else) lists what this seat registered —
 * not a second ledger. On every seat that links this TU. */
static mp_obj_t mod_wasm_modules(void) {
    uint32_t n;
    uint32_t i;
    mp_obj_t lst;
    mp_wasm_ensure_inited();
    n = pm_wasmmod_registry_module_count();
    lst = mp_obj_new_list(0, NULL);
    for (i = 0; i < n; i++) {
        uint8_t buf[MP_WASM_FQN_MAX];
        uint32_t len = (uint32_t)sizeof(buf);
        if (!pm_wasmmod_registry_module_at(i, buf, &len) || len == 0) {
            continue;
        }
        mp_obj_list_append(lst, mp_obj_new_str((const char *)buf, len));
    }
    return lst;
}
MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_modules_obj, mod_wasm_modules);
