/*
 * MPWP mount bind + export funobjs for the mpwm µPy host.
 */

#include "ports/micropython/packbind.h"

#include <string.h>

#include "py/compile.h"
#include "py/emitglue.h"
#include "py/objmodule.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "py/smallint.h"

#include "ports/micropython/finder.h"
#include "ports/micropython/nativecall.h"
#include "pymergetic/wasmmod/api/__exports__.h"
#include "pymergetic/wasmmod/pack/__types__.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/registry/__exports__.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM_ELF
#include "pymergetic/wasmmod/pack/format/elf/load.h"
#endif

/* ---- export funobj: calls registry by pack FQN + export name ---- */

typedef struct _mp_obj_wasm_export_t {
    mp_obj_base_t base;
    qstr pack_fqn;
    qstr export_name;
    uint8_t sig; /* 0..8 = N i32→i32; 255 = auto via nargs */
} mp_obj_wasm_export_t;

static mp_obj_t wasm_export_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)n_kw;
    mp_obj_wasm_export_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_wasm_native_call(qstr_str(self->pack_fqn), qstr_str(self->export_name), n_args, args);
}

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_wasm_export,
    MP_QSTR_function,
    MP_TYPE_FLAG_BINDS_SELF,
    call, wasm_export_call
    );

static mp_obj_t wasm_export_new(qstr pack_fqn, qstr export_name, uint8_t sig) {
    mp_obj_wasm_export_t *o = mp_obj_malloc(mp_obj_wasm_export_t, &mp_type_wasm_export);
    o->pack_fqn = pack_fqn;
    o->export_name = export_name;
    o->sig = sig;
    return MP_OBJ_FROM_PTR(o);
}

/* ---- path helpers (from historical packload) ---- */

static size_t pack_logical_path_len(const char *path, size_t path_len) {
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (path[i] != '.') {
            continue;
        }
        if (memcmp(path + i, ".upy.", 5) == 0 || memcmp(path + i, ".cpy.", 5) == 0) {
            return i;
        }
    }
    if (path_len >= 3 && memcmp(path + path_len - 3, ".py", 3) == 0) {
        return path_len - 3;
    }
    if (path_len >= 4 && memcmp(path + path_len - 4, ".mpy", 4) == 0) {
        return path_len - 4;
    }
    if (path_len >= 4 && memcmp(path + path_len - 4, ".pyc", 4) == 0) {
        return path_len - 4;
    }
    return path_len;
}

static bool pack_logical_eq(const char *a, size_t a_len, const char *b, size_t b_len) {
    size_t la = pack_logical_path_len(a, a_len);
    size_t lb = pack_logical_path_len(b, b_len);
    return la == lb && memcmp(a, b, la) == 0;
}

static void path_to_dotted(const char *root, size_t root_len, const char *path, size_t path_len,
    vstr_t *out) {
    vstr_init(out, root_len + path_len + 4);
    vstr_add_strn(out, root, root_len);
    if (path_len == 0) {
        return;
    }
    size_t n = pack_logical_path_len(path, path_len);
    if (n == 8 && memcmp(path, "__init__", 8) == 0) {
        return;
    }
    if (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0) {
        n -= 9;
    }
    vstr_add_char(out, '.');
    for (size_t i = 0; i < n; ++i) {
        char c = path[i];
        vstr_add_char(out, c == '/' ? '.' : c);
    }
}

static bool path_is_package_init(const char *path, size_t path_len) {
    size_t n = pack_logical_path_len(path, path_len);
    return (n == 8 && memcmp(path, "__init__", 8) == 0)
        || (n > 9 && memcmp(path + n - 9, "/__init__", 9) == 0);
}

static void ensure_parent_packages_local(const char *full_name) {
    size_t len = strlen(full_name);
    for (size_t i = 0; i < len; ++i) {
        if (full_name[i] != '.') {
            continue;
        }
        qstr parent = qstr_from_strn(full_name, i);
        const char *pname = qstr_str(parent);
        if (mp_wasm_is_host_face(pname)) {
            continue;
        }
        if (mp_module_get_builtin(parent, false) != MP_OBJ_NULL) {
            continue;
        }
#if MICROPY_HAVE_REGISTERED_EXTENSIBLE_MODULES
        if (mp_module_get_builtin(parent, true) != MP_OBJ_NULL) {
            continue;
        }
#endif
        mp_obj_t pmod = mp_obj_new_module(parent);
        mp_obj_module_t *mod = MP_OBJ_TO_PTR(pmod);
        /* Never write into ROM/fixed globals (would corrupt builtins). */
        if (mod->globals->map.is_fixed) {
            continue;
        }
        mp_obj_t g = MP_OBJ_FROM_PTR(mod->globals);
        mp_map_elem_t *el = mp_map_lookup(&mod->globals->map,
            MP_OBJ_NEW_QSTR(MP_QSTR___path__), MP_MAP_LOOKUP);
        if (el == NULL) {
            mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                mp_obj_new_str(pname, i));
        }
    }
}

static void link_module_to_parent_local(const char *dotted_name) {
    const char *dot = strrchr(dotted_name, '.');
    if (dot == NULL) {
        return;
    }
    qstr qparent = qstr_from_strn(dotted_name, (size_t)(dot - dotted_name));
    qstr qleaf = qstr_from_str(dot + 1);
    mp_map_elem_t *pel = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qparent), MP_MAP_LOOKUP);
    mp_map_elem_t *cel = mp_map_lookup(&MP_STATE_VM(mp_loaded_modules_dict).map,
        MP_OBJ_NEW_QSTR(qstr_from_str(dotted_name)), MP_MAP_LOOKUP);
    if (pel == NULL || cel == NULL || pel->value == MP_OBJ_NULL || cel->value == MP_OBJ_NULL) {
        return;
    }
    mp_obj_module_t *pmod = MP_OBJ_TO_PTR(pel->value);
    if (pmod->globals->map.is_fixed) {
        return;
    }
    mp_obj_dict_store(MP_OBJ_FROM_PTR(pmod->globals), MP_OBJ_NEW_QSTR(qleaf), cel->value);
}

static int score_pack_file_for_upy_host(const mp_pack_manifest_file_t *f) {
    const char *path = f->path;
    size_t path_len = f->path_len;

    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".cpy.", 5) == 0) {
            return -1;
        }
    }
    if (f->kind == MP_PACK_KIND_PYC) {
        return -1;
    }
    if (f->kind == MP_PACK_KIND_PY) {
        return 1;
    }
    if (f->kind != MP_PACK_KIND_MPY) {
        return -1;
    }

#if MICROPY_PERSISTENT_CODE_LOAD
    const uint8_t *data;
    uint32_t data_len;
    uint8_t *to_free = NULL;
    if (!mp_pack_manifest_file_bytes(f, &data, &data_len, &to_free)) {
        return -1;
    }
    if (data_len < 4 || data[0] != 'M' || data[1] != MPY_VERSION) {
        MICROPY_WASM_FREE(to_free);
        return -1;
    }
    if (MPY_FEATURE_DECODE_ARCH(data[2]) != MP_NATIVE_ARCH_NONE) {
        MICROPY_WASM_FREE(to_free);
        return -1;
    }
    if (data[3] > MP_SMALL_INT_BITS) {
        MICROPY_WASM_FREE(to_free);
        return -1;
    }
    int sib_byte = (int)data[3];
    MICROPY_WASM_FREE(to_free);

    const char *tag = NULL;
    size_t tag_len = 0;
    for (size_t i = 0; i + 5 <= path_len; ++i) {
        if (memcmp(path + i, ".upy.", 5) == 0) {
            tag = path + i + 5;
            tag_len = path_len - (i + 5);
            break;
        }
    }
    if (tag != NULL && tag_len >= 8 && memcmp(tag, "mpy", 3) == 0) {
        unsigned mpy_ver = 0;
        size_t p = 3;
        while (p < tag_len && tag[p] >= '0' && tag[p] <= '9') {
            mpy_ver = mpy_ver * 10u + (unsigned)(tag[p] - '0');
            ++p;
        }
        const char *sibp = NULL;
        for (size_t j = 0; j + 4 < tag_len; ++j) {
            if (tag[j] == '.' && memcmp(tag + j, ".sib", 4) == 0) {
                sibp = tag + j + 4;
                break;
            }
        }
        unsigned sib = 0;
        if (sibp != NULL) {
            while (*sibp >= '0' && *sibp <= '9') {
                sib = sib * 10u + (unsigned)(*sibp - '0');
                ++sibp;
            }
        }
        if (mpy_ver != MPY_VERSION || sib == 0 || sib > (unsigned)MP_SMALL_INT_BITS) {
            return -1;
        }
        return 100 + (int)sib;
    }
    return 50 + sib_byte;
#else
    (void)f;
    return -1;
#endif
}

static void exec_py_into_module(mp_obj_t module_obj, const char *src_name, const uint8_t *data,
    uint32_t len) {
    mp_obj_dict_t *globals = mp_obj_module_get_globals(module_obj);
    mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(src_name), (const char *)data, len, 0);
    mp_parse_compile_execute(lex, MP_PARSE_FILE_INPUT, globals, globals);
}

#if MICROPY_PERSISTENT_CODE_LOAD
static void exec_mpy_into_module(mp_obj_t module_obj, const char *src_name, const uint8_t *data,
    uint32_t len) {
    mp_module_context_t *context = (mp_module_context_t *)MP_OBJ_TO_PTR(module_obj);
    mp_compiled_module_t cm;
    cm.context = context;
    mp_raw_code_load_mem(data, len, &cm);

#if MICROPY_MODULE___FILE__
    mp_store_attr(module_obj, MP_QSTR___file__, MP_OBJ_NEW_QSTR(qstr_from_str(src_name)));
#else
    (void)src_name;
#endif

    mp_obj_dict_t *mod_globals = context->module.globals;
    nlr_jump_callback_node_globals_locals_t ctx;
    ctx.globals = mp_globals_get();
    ctx.locals = mp_locals_get();
    mp_globals_set(mod_globals);
    mp_locals_set(mod_globals);
    nlr_push_jump_callback(&ctx.callback, mp_globals_locals_set_from_nlr_jump_callback);
    mp_obj_t module_fun = mp_make_function_from_proto_fun(cm.rc, context, NULL);
    mp_call_function_0(module_fun);
    nlr_pop_jump_callback(true);
}
#endif

static void exec_pack_file_into_module(mp_obj_t module_obj, const char *src_name,
    const mp_pack_manifest_file_t *f) {
    const uint8_t *data;
    uint32_t data_len;
    uint8_t *to_free = NULL;
    if (!mp_pack_manifest_file_bytes(f, &data, &data_len, &to_free)) {
        mp_raise_ValueError(MP_ERROR_TEXT("wasm pack file inflate failed"));
    }
    if (f->kind == MP_PACK_KIND_PY) {
        exec_py_into_module(module_obj, src_name, data, data_len);
        MICROPY_WASM_FREE(to_free);
        return;
    }
#if MICROPY_PERSISTENT_CODE_LOAD
    if (f->kind == MP_PACK_KIND_MPY) {
        exec_mpy_into_module(module_obj, src_name, data, data_len);
        MICROPY_WASM_FREE(to_free);
        return;
    }
#endif
    MICROPY_WASM_FREE(to_free);
    mp_raise_msg_varg(&mp_type_ValueError,
        MP_ERROR_TEXT("wasm pack file kind %d not supported"), (int)f->kind);
}

static mp_obj_t module_for_export_suffix(const char *pack_name, const char *suffix,
    uint16_t suffix_len) {
    if (suffix_len == 0 || (suffix_len == 1 && suffix[0] == '.')) {
        return mp_obj_new_module(qstr_from_str(pack_name));
    }
    /* PM_MOD_EXPORT_C(module, …) discovery writes the C short name into the
     * MPWP module field. When that equals the pack leaf, bind on the pack
     * root (not pack.leaf.leaf). Real nested faces use a different suffix. */
    const char *dot = strrchr(pack_name, '.');
    const char *leaf = dot ? dot + 1 : pack_name;
    if (strlen(leaf) == suffix_len && memcmp(leaf, suffix, suffix_len) == 0) {
        return mp_obj_new_module(qstr_from_str(pack_name));
    }
    vstr_t dotted;
    vstr_init(&dotted, strlen(pack_name) + suffix_len + 2);
    vstr_add_str(&dotted, pack_name);
    vstr_add_char(&dotted, '.');
    vstr_add_strn(&dotted, suffix, suffix_len);
    const char *name = vstr_null_terminated_str(&dotted);
    ensure_parent_packages_local(name);
    mp_obj_t mod = mp_obj_new_module(qstr_from_str(name));
    vstr_clear(&dotted);
    return mod;
}

static void bind_pack_exports(mp_obj_t root, const char *pack_name, const mp_pack_manifest_t *info) {
    qstr qpack = qstr_from_str(pack_name);
    if (info != NULL && info->n_exports > 0) {
        for (uint32_t i = 0; i < info->n_exports; ++i) {
            const mp_pack_manifest_export_t *ex = &info->exports[i];
            if (ex->func_len == 0 || ex->export_len == 0) {
                continue;
            }
            /* Skip if registry has no such export (e.g. ELF-only names). */
            if (pm_wasmmod_registry_resolve_native((const uint8_t *)pack_name,
                    (uint32_t)strlen(pack_name), (const uint8_t *)ex->export_name,
                    ex->export_len) == NULL) {
                continue;
            }
            qstr qexport = qstr_from_strn(ex->export_name, ex->export_len);
            mp_obj_t target = module_for_export_suffix(pack_name, ex->module, ex->module_len);
            mp_obj_t f = wasm_export_new(qpack, qexport, ex->sig);
            mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(target)),
                MP_OBJ_NEW_QSTR(qstr_from_strn(ex->func, ex->func_len)), f);
        }
        return;
    }
    (void)root;
}

mp_obj_t mp_wasm_pack_bind(const char *pack_name, pm_wasmmod_registry_handle_t h,
    const uint8_t *meta, uint32_t meta_len) {
    /* Best-effort lifecycle export (may be absent). */
    int32_t lc = 0;
    (void)pm_wasmmod_api_call0_i32((const uint8_t *)pack_name, (uint32_t)strlen(pack_name),
        (const uint8_t *)"mp_pack_load", 12, &lc);

    ensure_parent_packages_local(pack_name);
    qstr qpack = qstr_from_str(pack_name);
    mp_obj_t root = mp_obj_new_module(qpack);
    mp_wasm_store_handle_on_module(root, h);

    mp_obj_t g = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(root));
    mp_obj_dict_store(g, MP_OBJ_NEW_QSTR(MP_QSTR___path__),
        mp_obj_new_str(pack_name, strlen(pack_name)));

    mp_pack_manifest_t info;
    memset(&info, 0, sizeof(info));
    bool have_pack = false;
    {
        const uint8_t *payload = NULL;
        uint32_t payload_len = 0;
        have_pack = mp_pack_manifest_find_section(meta, meta_len, &payload, &payload_len)
            && mp_pack_manifest_parse(payload, payload_len, &info);
    }
    bind_pack_exports(root, pack_name, have_pack ? &info : NULL);

    if (have_pack) {
        uint32_t *best_idx = m_new(uint32_t, info.n_files ? info.n_files : 1);
        int *best_score = m_new(int, info.n_files ? info.n_files : 1);
        uint32_t n_best = 0;
        for (uint32_t i = 0; i < info.n_files; ++i) {
            const mp_pack_manifest_file_t *f = &info.files[i];
            if (f->kind != MP_PACK_KIND_PY && f->kind != MP_PACK_KIND_MPY
                && f->kind != MP_PACK_KIND_PYC) {
                continue;
            }
            int score = score_pack_file_for_upy_host(f);
            if (score < 0) {
                continue;
            }
            uint32_t slot = n_best;
            bool found = false;
            for (uint32_t j = 0; j < n_best; ++j) {
                if (pack_logical_eq(f->path, f->path_len, info.files[best_idx[j]].path,
                        info.files[best_idx[j]].path_len)) {
                    slot = j;
                    found = true;
                    break;
                }
            }
            if (found) {
                if (score > best_score[slot]) {
                    best_idx[slot] = i;
                    best_score[slot] = score;
                }
            } else {
                best_idx[n_best] = i;
                best_score[n_best] = score;
                n_best++;
            }
        }
        for (uint32_t j = 0; j < n_best; ++j) {
            const mp_pack_manifest_file_t *f = &info.files[best_idx[j]];
            vstr_t dotted;
            path_to_dotted(pack_name, strlen(pack_name), f->path, f->path_len, &dotted);
            const char *dotted_name = vstr_null_terminated_str(&dotted);
            ensure_parent_packages_local(dotted_name);
            qstr qmod = qstr_from_str(dotted_name);
            mp_obj_t mod = mp_obj_new_module(qmod);
            mp_wasm_store_handle_on_module(mod, h);
            if (path_is_package_init(f->path, f->path_len)
                || strcmp(dotted_name, pack_name) == 0) {
                mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
                    MP_OBJ_NEW_QSTR(MP_QSTR___path__),
                    mp_obj_new_str(dotted_name, dotted.len));
            }
            vstr_t src_name;
            vstr_init(&src_name, f->path_len + 16);
            vstr_add_str(&src_name, pack_name);
            vstr_add_char(&src_name, ':');
            vstr_add_strn(&src_name, f->path, f->path_len);
            exec_pack_file_into_module(mod, vstr_null_terminated_str(&src_name), f);
            /* Re-bind exports onto root after __init__ executes (answer() needs hello). */
            if (strcmp(dotted_name, pack_name) == 0) {
                bind_pack_exports(root, pack_name, &info);
            }
            link_module_to_parent_local(dotted_name);
            vstr_clear(&src_name);
            vstr_clear(&dotted);
        }
        m_del(uint32_t, best_idx, info.n_files ? info.n_files : 1);
        m_del(int, best_score, info.n_files ? info.n_files : 1);
    }
    mp_pack_manifest_free(&info);
    link_module_to_parent_local(pack_name);
    return root;
}

/* ---- ELF publish (optional) ---- */

#include <stdio.h>

#if MICROPY_PY_WASM_ELF

#ifndef MP_WASM_ELF_ADAPTER_SLOTS
#define MP_WASM_ELF_ADAPTER_SLOTS (32)
#endif

typedef struct {
    bool used;
    void *native;
    uint8_t sig;
    mp_wasm_elf_image_t *img;
} elf_adapter_slot_t;

static elf_adapter_slot_t elf_adapters[MP_WASM_ELF_ADAPTER_SLOTS];

static int32_t elf_adapter_invoke(unsigned idx, const pm_wasmmod_registry_value_t *args,
    uint32_t nargs, pm_wasmmod_registry_value_t *results, uint32_t nresults) {
    if (idx >= MP_WASM_ELF_ADAPTER_SLOTS || !elf_adapters[idx].used || elf_adapters[idx].native == NULL) {
        return -1;
    }
    uint8_t sig = elf_adapters[idx].sig;
    if (sig == MP_PACK_SIG_AUTO) {
        sig = (uint8_t)nargs;
    }
    if (nargs < sig || nresults < 1) {
        return -1;
    }
    int32_t r = 0;
    void *fn = elf_adapters[idx].native;
    switch (sig) {
        case 0:
            r = ((int32_t (*)(void))fn)();
            break;
        case 1:
            r = ((int32_t (*)(int32_t))fn)(args[0].of.i32);
            break;
        case 2:
            r = ((int32_t (*)(int32_t, int32_t))fn)(args[0].of.i32, args[1].of.i32);
            break;
        case 3:
            r = ((int32_t (*)(int32_t, int32_t, int32_t))fn)(args[0].of.i32, args[1].of.i32,
                args[2].of.i32);
            break;
        case 4:
            r = ((int32_t (*)(int32_t, int32_t, int32_t, int32_t))fn)(args[0].of.i32, args[1].of.i32,
                args[2].of.i32, args[3].of.i32);
            break;
        default:
            return -1;
    }
    results[0] = pm_wasmmod_registry_value_i32(r);
    return 0;
}

#define ELF_AD_FN(N) \
    static int32_t elf_ad_##N(const pm_wasmmod_registry_value_t *a, uint32_t n, \
        pm_wasmmod_registry_value_t *r, uint32_t nr) { \
        return elf_adapter_invoke(N, a, n, r, nr); \
    }
ELF_AD_FN(0) ELF_AD_FN(1) ELF_AD_FN(2) ELF_AD_FN(3)
ELF_AD_FN(4) ELF_AD_FN(5) ELF_AD_FN(6) ELF_AD_FN(7)
ELF_AD_FN(8) ELF_AD_FN(9) ELF_AD_FN(10) ELF_AD_FN(11)
ELF_AD_FN(12) ELF_AD_FN(13) ELF_AD_FN(14) ELF_AD_FN(15)
ELF_AD_FN(16) ELF_AD_FN(17) ELF_AD_FN(18) ELF_AD_FN(19)
ELF_AD_FN(20) ELF_AD_FN(21) ELF_AD_FN(22) ELF_AD_FN(23)
ELF_AD_FN(24) ELF_AD_FN(25) ELF_AD_FN(26) ELF_AD_FN(27)
ELF_AD_FN(28) ELF_AD_FN(29) ELF_AD_FN(30) ELF_AD_FN(31)

static pm_wasmmod_registry_fn_t const elf_ad_fns[MP_WASM_ELF_ADAPTER_SLOTS] = {
    elf_ad_0, elf_ad_1, elf_ad_2, elf_ad_3, elf_ad_4, elf_ad_5, elf_ad_6, elf_ad_7,
    elf_ad_8, elf_ad_9, elf_ad_10, elf_ad_11, elf_ad_12, elf_ad_13, elf_ad_14, elf_ad_15,
    elf_ad_16, elf_ad_17, elf_ad_18, elf_ad_19, elf_ad_20, elf_ad_21, elf_ad_22, elf_ad_23,
    elf_ad_24, elf_ad_25, elf_ad_26, elf_ad_27, elf_ad_28, elf_ad_29, elf_ad_30, elf_ad_31,
};

static int claim_elf_adapter(void *native, uint8_t sig, mp_wasm_elf_image_t *img) {
    for (int i = 0; i < MP_WASM_ELF_ADAPTER_SLOTS; ++i) {
        if (!elf_adapters[i].used) {
            elf_adapters[i].used = true;
            elf_adapters[i].native = native;
            elf_adapters[i].sig = sig;
            elf_adapters[i].img = img;
            return i;
        }
    }
    return -1;
}

static void *elf_resolve_import(const char *name, void *ctx) {
    (void)ctx;
    (void)name;
    return NULL;
}

typedef struct {
    pm_wasmmod_registry_handle_t h;
    mp_wasm_elf_image_t *img;
    const mp_pack_manifest_t *info;
} elf_pub_ctx_t;

static uint8_t sig_for_export_name(const mp_pack_manifest_t *info, const char *name) {
    if (info == NULL) {
        return MP_PACK_SIG_AUTO;
    }
    size_t nlen = strlen(name);
    for (uint32_t i = 0; i < info->n_exports; ++i) {
        const mp_pack_manifest_export_t *ex = &info->exports[i];
        if (ex->export_len == nlen && memcmp(ex->export_name, name, nlen) == 0) {
            return ex->sig;
        }
    }
    return MP_PACK_SIG_AUTO;
}

static void elf_export_cb(const char *name, void *addr, void *ctx_in) {
    elf_pub_ctx_t *ctx = ctx_in;
    if (name == NULL || addr == NULL || name[0] == '\0') {
        return;
    }
    uint8_t sig = sig_for_export_name(ctx->info, name);
    int slot = claim_elf_adapter(addr, sig, ctx->img);
    if (slot < 0) {
        return;
    }
    (void)pm_wasmmod_registry_export_set(ctx->h, (const uint8_t *)name, (uint32_t)strlen(name),
        PM_WASMMOD_REGISTRY_EXPORT_FN, (void *)elf_ad_fns[slot]);
}

#endif /* MICROPY_PY_WASM_ELF */

pm_wasmmod_registry_handle_t mp_wasm_elf_publish(const char *pack_name, const uint8_t *bytes,
    uint32_t len, void **img_out, char *err, size_t err_len) {
    pm_wasmmod_registry_handle_t bad = { .index = UINT32_MAX, .generation = 0 };
    if (img_out) {
        *img_out = NULL;
    }
#if !MICROPY_PY_WASM_ELF
    (void)pack_name;
    (void)bytes;
    (void)len;
    if (err && err_len) {
        snprintf(err, err_len, "ELF disabled");
    }
    return bad;
#else
    mp_wasm_elf_image_t *img = NULL;
    if (!mp_wasm_elf_image_load(bytes, len, elf_resolve_import, NULL, &img, err, err_len)) {
        return bad;
    }
    pm_wasmmod_registry_handle_t h = pm_wasmmod_registry_publish((const uint8_t *)pack_name,
        (uint32_t)strlen(pack_name), PM_WASMMOD_REGISTRY_CONTAINER_ELF);
    if (h.index == UINT32_MAX) {
        mp_wasm_elf_image_free(img);
        if (err && err_len) {
            snprintf(err, err_len, "registry publish failed");
        }
        return bad;
    }
    (void)pm_wasmmod_loader_bake_pkg_version((const uint8_t *)pack_name, (uint32_t)strlen(pack_name),
        bytes, len);

    mp_pack_manifest_t info;
    memset(&info, 0, sizeof(info));
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    bool have = mp_pack_manifest_find_section(bytes, len, &payload, &payload_len)
        && mp_pack_manifest_parse(payload, payload_len, &info);

    elf_pub_ctx_t ctx = { .h = h, .img = img, .info = have ? &info : NULL };
    mp_wasm_elf_foreach_func(img, elf_export_cb, &ctx);
    mp_pack_manifest_free(&info);

    if (img_out) {
        *img_out = img;
    }
    return h;
#endif
}

void mp_wasm_elf_release_for_module(mp_obj_t mod) {
#if !MICROPY_PY_WASM_ELF
    (void)mod;
#else
    pm_wasmmod_registry_handle_t h;
    if (!mp_wasm_load_handle_from_module(mod, &h)) {
        return;
    }
    mp_obj_t g = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod));
    mp_map_elem_t *el = mp_map_lookup(&((mp_obj_dict_t *)MP_OBJ_TO_PTR(g))->map,
        MP_OBJ_NEW_QSTR(MP_QSTR___wasm_elf__), MP_MAP_LOOKUP);
    if (el != NULL && el->value != MP_OBJ_NULL) {
        mp_wasm_elf_image_t *img = (mp_wasm_elf_image_t *)(uintptr_t)mp_obj_get_int(el->value);
        for (int i = 0; i < MP_WASM_ELF_ADAPTER_SLOTS; ++i) {
            if (elf_adapters[i].used && elf_adapters[i].img == img) {
                elf_adapters[i].used = false;
                elf_adapters[i].native = NULL;
                elf_adapters[i].img = NULL;
            }
        }
        if (img != NULL) {
            mp_wasm_elf_image_free(img);
        }
    }
    (void)pm_wasmmod_registry_unpublish(h);
#endif
}
