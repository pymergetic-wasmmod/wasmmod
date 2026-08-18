/*
 * Thin MicroPython face for wasmmod (upywm).
 *
 * Module tables + mp_obj wrappers only. Import hook → importhook.c;
 * util.gen → modgen.c; boot/load prepare → ports/common/.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (1)
#endif

#include <string.h>

#include "py/mperrno.h"
#include "py/mpprint.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include "ports/common/load.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/hostready.h"
#include "ports/micropython/importhook.h"
#include "ports/micropython/mpconfig_wasm.h"
#include "ports/micropython/nativecall.h"
#include "ports/micropython/packbind.h"

extern const mp_obj_module_t mp_module_pymergetic_wasmmod_guest;
extern const mp_obj_module_t mp_module_pymergetic_wasmmod_net;

#include "pymergetic/wasmmod/__version__.h"
#include "pymergetic/wasmmod/api/__exports__.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/net/search.h"
#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/source.h"
#include "pymergetic/wasmmod/pack/zlib_env.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/verify.h"

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM_GEN
MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_gen_obj);
#endif

#ifndef MICROPY_WASM_VERSION
#define MICROPY_WASM_VERSION PYMERGETIC_WASMMOD_VERSION
#endif

#if !MICROPY_PY_WASM
#error "ports/micropython/modwasmmod.c requires MICROPY_PY_WASM=1"
#endif

static mp_obj_t mod_wasm_has(mp_obj_t name_in) {
    mp_wasm_ensure_inited();
    const char *name = mp_obj_str_get_str(name_in);
    return mp_obj_new_bool(pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)strlen(name)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_has_obj, mod_wasm_has);

static mp_obj_t mod_wasm_version(void) {
    mp_wasm_ensure_inited();
    uint8_t buf[64];
    uint32_t n = sizeof(buf);
    if (pm_wasmmod_registry_version((const uint8_t *)"pymergetic.wasmmod",
            (uint32_t)strlen("pymergetic.wasmmod"), buf, &n)
        && n > 0) {
        return mp_obj_new_str((const char *)buf, n);
    }
    return mp_obj_new_str(MICROPY_WASM_VERSION, strlen(MICROPY_WASM_VERSION));
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_version_obj, mod_wasm_version);

static mp_obj_t mod_wasm_test(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(args[0]);
    size_t flen = strlen(fqn);
    if (n_args >= 2) {
        const char *name = mp_obj_str_get_str(args[1]);
        return mp_obj_new_int(pm_wasmmod_registry_test_run(
            (const uint8_t *)fqn, (uint32_t)flen,
            (const uint8_t *)name, (uint32_t)strlen(name)));
    }
    return mp_obj_new_int(pm_wasmmod_registry_test_run_all(
        (const uint8_t *)fqn, (uint32_t)flen));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_test_obj, 1, 2, mod_wasm_test);

static mp_obj_t mod_wasm_test_all(void) {
    mp_wasm_ensure_inited();
    uint32_t n = pm_wasmmod_registry_module_count();
    int32_t fails = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t buf[256];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_module_at(i, buf, &len) || len == 0) {
            continue;
        }
        if (pm_wasmmod_registry_test_count(buf, len) == 0) {
            continue;
        }
        fails += pm_wasmmod_registry_test_run_all(buf, len);
    }
    return mp_obj_new_int(fails);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_test_all_obj, mod_wasm_test_all);

static mp_obj_t mod_wasm_bind_py(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(args[0]);
    mp_obj_t mod;
    if (n_args >= 2) {
        mod = args[1];
    } else {
        mod = mp_import_name(qstr_from_str(fqn), mp_const_empty_tuple, MP_OBJ_NEW_SMALL_INT(0));
    }
    /* Explicit re-ready; import path already calls host_ready. */
    int n = mp_wasm_host_ready(fqn, mod);
    if (n < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("bind_py failed"));
    }
    return mp_obj_new_int(n);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_bind_py_obj, 1, 2, mod_wasm_bind_py);

static mp_obj_t mod_wasm_tests(mp_obj_t fqn_in) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    size_t flen = strlen(fqn);
    uint32_t tc = pm_wasmmod_registry_test_count((const uint8_t *)fqn, (uint32_t)flen);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint32_t i = 0; i < tc; i++) {
        uint8_t buf[128];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_test_at((const uint8_t *)fqn, (uint32_t)flen, i, buf, &len)
            || len == 0) {
            continue;
        }
        mp_obj_list_append(list, mp_obj_new_str((const char *)buf, len));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_tests_obj, mod_wasm_tests);

static mp_obj_t mod_wasm_test_count(mp_obj_t fqn_in) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    return mp_obj_new_int_from_uint(pm_wasmmod_registry_test_count(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_test_count_obj, mod_wasm_test_count);

/* wasm.bench(fqn, name=None, iters=...) → ns/op report (int) for one bench,
 * or the full ns/op report string for every bench of `fqn`. Benches are
 * informational and never gate: without a clock the registry returns a
 * negative BENCH_RC_* instead of a fake number. */
static mp_obj_t mod_wasm_bench(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(args[0]);
    size_t flen = strlen(fqn);
    uint64_t iters = 50000;
    if (n_args >= 3) {
        iters = (uint64_t)mp_obj_get_int(args[2]);
    } else if (n_args >= 2 && !mp_obj_is_str(args[1])
        && args[1] != mp_const_none) {
        /* Positional iters without a name is a common enough REPL slip. */
        iters = (uint64_t)mp_obj_get_int(args[1]);
    }
    if (n_args >= 2 && mp_obj_is_str(args[1])) {
        const char *name = mp_obj_str_get_str(args[1]);
        return mp_obj_new_int(pm_wasmmod_registry_bench_run(
            (const uint8_t *)fqn, (uint32_t)flen,
            (const uint8_t *)name, (uint32_t)strlen(name), iters));
    }
    uint8_t buf[2048];
    uint32_t len = sizeof(buf);
    int32_t bad = pm_wasmmod_registry_bench_run_all(
        (const uint8_t *)fqn, (uint32_t)flen, iters, buf, &len);
    if (len > sizeof(buf)) {
        len = sizeof(buf);
    }
    /* Return a dict {report: str, bad: int} so the REPL can both show the
     * pretty text and count the not-clean benches. */
    mp_obj_t d = mp_obj_new_dict(2);
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(qstr_from_str("report")),
        mp_obj_new_str((const char *)buf, len));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(qstr_from_str("bad")), mp_obj_new_int(bad));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_bench_obj, 1, 3, mod_wasm_bench);

/* wasm.bench_all(iters=...) → full ns/op sweep across every module with
 * benches. Returns the aggregate report string; prints as a bonus. */
static mp_obj_t mod_wasm_bench_all(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    uint64_t iters = 50000;
    if (n_args >= 1 && args[0] != mp_const_none) {
        iters = (uint64_t)mp_obj_get_int(args[0]);
    }
    uint32_t n = pm_wasmmod_registry_module_count();
    const mp_print_t *print = &mp_plat_print;
    int32_t bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t m[256];
        uint32_t mlen = sizeof(m);
        if (!pm_wasmmod_registry_module_at(i, m, &mlen) || mlen == 0) {
            continue;
        }
        if (pm_wasmmod_registry_bench_count(m, mlen) == 0) {
            continue;
        }
        uint8_t buf[2048];
        uint32_t blen = sizeof(buf);
        bad += pm_wasmmod_registry_bench_run_all(m, mlen, iters, buf, &blen);
        if (blen > sizeof(buf)) {
            blen = sizeof(buf);
        }
        mp_print_str(print, (const char *)buf);
    }
    (void)bad;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_bench_all_obj, 0, 1, mod_wasm_bench_all);

/* wasm.benches(fqn) → list of bench names for a module (or [] if none). */
static mp_obj_t mod_wasm_benches(mp_obj_t fqn_in) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    size_t flen = strlen(fqn);
    uint32_t bc = pm_wasmmod_registry_bench_count((const uint8_t *)fqn, (uint32_t)flen);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (uint32_t i = 0; i < bc; i++) {
        uint8_t buf[128];
        uint32_t len = sizeof(buf);
        if (!pm_wasmmod_registry_bench_at((const uint8_t *)fqn, (uint32_t)flen, i, buf, &len)
            || len == 0) {
            continue;
        }
        mp_obj_list_append(list, mp_obj_new_str((const char *)buf, len));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_benches_obj, mod_wasm_benches);

static mp_obj_t mod_wasm_bench_count(mp_obj_t fqn_in) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    return mp_obj_new_int_from_uint(pm_wasmmod_registry_bench_count(
        (const uint8_t *)fqn, (uint32_t)strlen(fqn)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_bench_count_obj, mod_wasm_bench_count);

static mp_obj_t mod_wasm_path(void) {
    mp_wasm_ensure_inited();
    return mp_wasm_path_obj();
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_path_obj_fun, mod_wasm_path);

static mp_obj_t mod_wasm_path_append(mp_obj_t root_in) {
    mp_wasm_ensure_inited();
    mp_wasm_path_append(mp_obj_str_get_str(root_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_path_append_obj, mod_wasm_path_append);

static void wasm_cdn_scrub_path_bases(void) {
    mp_obj_t lst = mp_wasm_path_obj();
    size_t n;
    mp_obj_t *items;
    mp_obj_list_get(lst, &n, &items);
    for (size_t i = n; i > 0; --i) {
        size_t idx = i - 1;
        if (!mp_obj_is_str(items[idx])) {
            continue;
        }
        if (pm_wasmmod_net_cdn_url_is_base(mp_obj_str_get_str(items[idx]))) {
            mp_obj_list_remove(lst, items[idx]);
        }
    }
}

static const char *wasm_cdn_token_arg(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2 || args[1] == mp_const_none) {
        return NULL;
    }
    if (!mp_obj_is_str(args[1])) {
        mp_raise_TypeError(MP_ERROR_TEXT("cdn: token must be str"));
    }
    return mp_obj_str_get_str(args[1]);
}

/* wasm.cdn(url, token=None) — replace bases with one artifact CDN root. */
static mp_obj_t mod_wasm_cdn(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    if (!mp_obj_is_str(args[0])) {
        mp_raise_TypeError(MP_ERROR_TEXT("cdn(url, token=None)"));
    }
    const char *url = mp_obj_str_get_str(args[0]);
    if (!pm_wasmmod_io_uri_is_http(url)) {
        mp_raise_ValueError(MP_ERROR_TEXT("cdn: url must be http(s)"));
    }
    pm_wasmmod_net_cdn_configure(url, wasm_cdn_token_arg(n_args, args));
    wasm_cdn_scrub_path_bases();
    const char *name = pm_wasmmod_net_cdn_driver_name();
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_cdn_obj, 1, 2, mod_wasm_cdn);

/* wasm.cdn_prepend(url, token=None) — add a base, keep existing. */
static mp_obj_t mod_wasm_cdn_prepend(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    if (!mp_obj_is_str(args[0])) {
        mp_raise_TypeError(MP_ERROR_TEXT("cdn_prepend(url, token=None)"));
    }
    const char *url = mp_obj_str_get_str(args[0]);
    if (!pm_wasmmod_io_uri_is_http(url)) {
        mp_raise_ValueError(MP_ERROR_TEXT("cdn_prepend: url must be http(s)"));
    }
    if (pm_wasmmod_net_cdn_base_count() == 0) {
        pm_wasmmod_net_cdn_configure(url, wasm_cdn_token_arg(n_args, args));
    } else {
        (void)pm_wasmmod_net_cdn_prepend(url, wasm_cdn_token_arg(n_args, args));
    }
    wasm_cdn_scrub_path_bases();
    const char *name = pm_wasmmod_net_cdn_driver_name();
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_cdn_prepend_obj, 1, 2, mod_wasm_cdn_prepend);

static mp_obj_t mod_wasm_cdn_reset(void) {
    mp_wasm_ensure_inited();
    pm_wasmmod_net_cdn_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_cdn_reset_obj, mod_wasm_cdn_reset);

/* Snapshot the `channel` kwarg: None → "lead" (the RS card treats NULL/empty
 * as lead too). NUL-terminated for the C ABI. */
static const char *channel_str(const mp_arg_val_t *args, size_t index) {
    mp_obj_t v = args[index].u_obj;
    if (v == mp_const_none) {
        return "lead";
    }
    if (!mp_obj_is_str(v)) {
        mp_raise_TypeError(MP_ERROR_TEXT("channel must be str"));
    }
    return mp_obj_str_get_str(v);
}

/* Snapshot an optional str kwarg: None → NULL (no constraint), otherwise the
 * NUL-terminated string. `label` is the TypeError message when not a str. */
static const char *opt_str_arg(const mp_arg_val_t *args, size_t index, const char *label) {
    mp_obj_t v = args[index].u_obj;
    if (v == mp_const_none) {
        return NULL;
    }
    if (!mp_obj_is_str(v)) {
        mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("%s must be str"), label);
    }
    return mp_obj_str_get_str(v);
}

/* The pack search/filter/catalog face is a single RS card
 * (`pymergetic.wasmmod.net.search`, impl = rs) that parses the fetched CDN
 * index with serde_json. Catalog/search/filter hand the matching logic to the
 * card via a count + name_at/meta_at result set, so host C, Rust, and this µPy
 * face all share the same semantics. Only the `full=True` convenience decodes
 * a pack's raw JSON entry here (Python's job); the matching itself is Rust's.
 *
 * Run a query, returning the matched count after raising on the card's error.
 */
static uint32_t wasm_search_raise_on_error(int32_t count) {
    if (count >= 0) {
        return (uint32_t)count;
    }
    uint8_t err[192];
    pm_wasmmod_net_search_last_error(err, sizeof(err) - 1);
    err[sizeof(err) - 1] = '\0';
    mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("search: %s"), err);
}

/* Build a Python list from the card's current result set. full=True returns
 * (name, meta) tuples where meta is the pack's parsed JSON entry. Meta is
 * fetched into a grown heap buffer (a pack entry can exceed the 512-byte
 * fixed scratch, so retry on the "required" handshake). */
static mp_obj_t wasm_search_results(uint32_t count, int full) {
    mp_obj_t out = mp_obj_new_list(0, NULL);
    uint8_t namebuf[256];
    for (uint32_t i = 0; i < count; i++) {
        uint32_t lname = sizeof(namebuf);
        if (!pm_wasmmod_net_search_name_at(i, namebuf, &lname) || lname == 0) {
            continue;
        }
        mp_obj_t name = mp_obj_new_str((const char *)namebuf, lname);
        if (!full) {
            mp_obj_list_append(out, name);
            continue;
        }
        uint32_t cap = 512;
        uint8_t *meta = m_new(uint8_t, cap);
        uint32_t n = cap;
        while (!pm_wasmmod_net_search_meta_at(i, meta, &n)) {
            if (n == 0) {
                break;
            }
            cap = n + 1;
            meta = m_renew(uint8_t, meta, cap - 1, cap);
            n = cap;
        }
        if (n == 0) {
            m_del(uint8_t, meta, cap);
            mp_obj_list_append(out, name);
            continue;
        }
        mp_obj_t text = mp_obj_new_str((const char *)meta, n);
        m_del(uint8_t, meta, cap);
        mp_obj_t json_mod = mp_import_name(qstr_from_str("json"), mp_const_none,
            MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t loads = mp_load_attr(json_mod, qstr_from_str("loads"));
        mp_obj_t entry = mp_call_function_1(loads, text);
        mp_obj_t pair[2] = { name, entry };
        mp_obj_list_append(out, mp_obj_new_tuple(2, pair));
    }
    return out;
}

/* wasm.catalog(channel="lead") → list of package name strings. */
static mp_obj_t mod_wasm_catalog(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_wasm_ensure_inited();
    enum { ARG_channel };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_channel, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_uint_t count = wasm_search_raise_on_error(
        pm_wasmmod_net_search_catalog(channel_str(args, ARG_channel)));
    return wasm_search_results((uint32_t)count, 0);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_catalog_obj, 0, mod_wasm_catalog);

/* wasm.search(q, *, channel="lead", full=False)
 * Every pack whose name contains q (case-insensitive), sorted. full=True
 * returns (name, meta) pairs. Matching is done by the RS search card. */
static mp_obj_t mod_wasm_search(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_wasm_ensure_inited();
    enum { ARG_q, ARG_channel, ARG_full };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_q, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_channel, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_full, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_FALSE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!mp_obj_is_str(args[ARG_q].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("search: q must be str"));
    }
    const char *q = mp_obj_str_get_str(args[ARG_q].u_obj);
    mp_uint_t count = wasm_search_raise_on_error(
        pm_wasmmod_net_search_search(q, channel_str(args, ARG_channel)));
    return wasm_search_results((uint32_t)count, mp_obj_is_true(args[ARG_full].u_obj));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_search_obj, 0, mod_wasm_search);

/* wasm.filter(*, prefix=None, name_contains=None, kind=None, arch=None,
 *            channel="lead", full=False)
 * Filter the index by name prefix/substring and artifact kind/arch (Rust card
 * looks across each package's artifacts[]). Returns names or (name, meta). */
static mp_obj_t mod_wasm_filter(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_wasm_ensure_inited();
    enum { ARG_prefix, ARG_name_contains, ARG_kind, ARG_arch, ARG_channel, ARG_full };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_prefix, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_name_contains, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_kind, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_arch, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_channel, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_full, MP_ARG_OBJ, { .u_rom_obj = MP_ROM_FALSE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    const char *prefix = opt_str_arg(args, ARG_prefix, "filter: prefix");
    const char *name_contains = opt_str_arg(args, ARG_name_contains, "filter: name_contains");
    const char *kind = opt_str_arg(args, ARG_kind, "filter: kind");
    const char *arch = opt_str_arg(args, ARG_arch, "filter: arch");
    mp_uint_t count = wasm_search_raise_on_error(pm_wasmmod_net_search_filter(
        prefix, name_contains, kind, arch, channel_str(args, ARG_channel)));
    return wasm_search_results((uint32_t)count, mp_obj_is_true(args[ARG_full].u_obj));
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_filter_obj, 0, mod_wasm_filter);

/* wasm.session_id() / wasm.session_id(id) */
static mp_obj_t mod_wasm_session_id(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    if (n_args >= 1) {
        if (args[0] == mp_const_none) {
            pm_wasmmod_net_cdn_set_session_id(NULL);
        } else if (mp_obj_is_str(args[0])) {
            pm_wasmmod_net_cdn_set_session_id(mp_obj_str_get_str(args[0]));
        } else {
            mp_raise_TypeError(MP_ERROR_TEXT("session_id must be str or None"));
        }
    }
    const char *sid = pm_wasmmod_net_cdn_session_id();
    if (sid == NULL || sid[0] == '\0') {
        return mp_const_none;
    }
    return mp_obj_new_str(sid, strlen(sid));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_session_id_obj, 0, 1, mod_wasm_session_id);

static const char *wasm_cdn_kw_token(mp_obj_t token_obj, const char *where) {
    if (token_obj == mp_const_none) {
        return NULL;
    }
    if (!mp_obj_is_str(token_obj)) {
        mp_raise_msg_varg(&mp_type_TypeError, MP_ERROR_TEXT("%s: token must be str"), where);
    }
    return mp_obj_str_get_str(token_obj);
}

/* wasm.publish(name, version, data, *, lead=True, pin=True, token=None) */
static mp_obj_t mod_wasm_publish(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_wasm_ensure_inited();
    enum { ARG_name, ARG_version, ARG_data, ARG_lead, ARG_pin, ARG_token };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_name, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_version, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_data, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_lead, MP_ARG_KW_ONLY | MP_ARG_BOOL, { .u_bool = true } },
        { MP_QSTR_pin, MP_ARG_KW_ONLY | MP_ARG_BOOL, { .u_bool = true } },
        { MP_QSTR_token, MP_ARG_KW_ONLY | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!mp_obj_is_str(args[ARG_name].u_obj) || !mp_obj_is_str(args[ARG_version].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("publish: name and version must be str"));
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_data].u_obj, &bufinfo, MP_BUFFER_READ);
    const char *token = wasm_cdn_kw_token(args[ARG_token].u_obj, "publish");
    char err[160];
    if (pm_wasmmod_net_cdn_publish(mp_obj_str_get_str(args[ARG_name].u_obj),
            mp_obj_str_get_str(args[ARG_version].u_obj), bufinfo.buf, (uint32_t)bufinfo.len,
            args[ARG_lead].u_bool ? 1 : 0, args[ARG_pin].u_bool ? 1 : 0, token, err, sizeof(err))
        != 0) {
        if (strstr(err, "not supported") != NULL || strstr(err, "https requires") != NULL) {
            mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("%s"), err);
        }
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_publish_obj, 3, mod_wasm_publish);

/* wasm.publish_file(path, name, version, *, lead=True, pin=True, token=None) */
static mp_obj_t mod_wasm_publish_file(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_wasm_ensure_inited();
    enum { ARG_path, ARG_name, ARG_version, ARG_lead, ARG_pin, ARG_token };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_path, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_name, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_version, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_lead, MP_ARG_KW_ONLY | MP_ARG_BOOL, { .u_bool = true } },
        { MP_QSTR_pin, MP_ARG_KW_ONLY | MP_ARG_BOOL, { .u_bool = true } },
        { MP_QSTR_token, MP_ARG_KW_ONLY | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (!mp_obj_is_str(args[ARG_path].u_obj) || !mp_obj_is_str(args[ARG_name].u_obj)
        || !mp_obj_is_str(args[ARG_version].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("publish_file: path, name, version must be str"));
    }
    const char *path = mp_obj_str_get_str(args[ARG_path].u_obj);
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (!mp_wasm_read_file(path, &bytes, &blen)) {
        mp_raise_OSError_with_filename(MP_ENOENT, path);
    }
    const char *token = wasm_cdn_kw_token(args[ARG_token].u_obj, "publish_file");
    char err[160];
    int32_t st = pm_wasmmod_net_cdn_publish(mp_obj_str_get_str(args[ARG_name].u_obj),
        mp_obj_str_get_str(args[ARG_version].u_obj), bytes, (uint32_t)blen,
        args[ARG_lead].u_bool ? 1 : 0, args[ARG_pin].u_bool ? 1 : 0, token, err, sizeof(err));
    if (st != 0) {
        if (strstr(err, "not supported") != NULL || strstr(err, "https requires") != NULL) {
            mp_raise_msg_varg(&mp_type_NotImplementedError, MP_ERROR_TEXT("%s"), err);
        }
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_publish_file_obj, 3, mod_wasm_publish_file);

static mp_obj_t mod_wasm_load(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    const char *fqn = n_args > 1 ? mp_obj_str_get_str(args[1]) : "anon";

    pm_wasmmod_host_prepared_t prep;
    char err[160];
    if (pm_wasmmod_host_prepare((const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len, fqn,
            &prep, err, sizeof(err))
        != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("%s"), err);
    }

    pm_wasmmod_registry_handle_t h;
    mp_obj_t mod;
#if MICROPY_PY_WASM_ELF
    if (prep.kind == MP_WASM_KIND_ELF) {
        void *img = NULL;
        h = mp_wasm_elf_publish(fqn, prep.bytes, prep.len, &img, err, sizeof(err));
        if (h.index == UINT32_MAX) {
            MICROPY_WASM_FREE(prep.owned);
            mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("elf load: %s"), err);
        }
        mod = mp_wasm_pack_bind(fqn, h, prep.bytes, prep.len);
        mp_obj_dict_store(MP_OBJ_FROM_PTR(mp_obj_module_get_globals(mod)),
            MP_OBJ_NEW_QSTR(MP_QSTR___wasm_elf__),
            mp_obj_new_int_from_ull((uint64_t)(uintptr_t)img));
        MICROPY_WASM_FREE(prep.owned);
        return mod;
    }
#endif

    h = pm_wasmmod_host_load_wasm(fqn, prep.bytes, prep.len);
    if (h.index == UINT32_MAX) {
        MICROPY_WASM_FREE(prep.owned);
        mp_raise_OSError(MP_EINVAL);
    }
    mod = mp_wasm_pack_bind(fqn, h, prep.bytes, prep.len);
    MICROPY_WASM_FREE(prep.owned);
    return mod;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_load_obj, 1, 2, mod_wasm_load);

static mp_obj_t mod_wasm_unload(mp_obj_t name_in) {
    mp_wasm_ensure_inited();
    mp_wasm_unload_pack(mp_obj_str_get_str(name_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_unload_obj, mod_wasm_unload);

static mp_obj_t mod_wasm_call(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(args[0]);
    const char *exp = mp_obj_str_get_str(args[1]);
    if (n_args <= 2) {
        return mp_wasm_native_call(fqn, exp, 0, NULL);
    }
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(args[2], &len, &items);
    return mp_wasm_native_call(fqn, exp, len, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_call_obj, 2, 3, mod_wasm_call);

static mp_obj_t mod_wasm_connect(mp_obj_t fqn_in, mp_obj_t exp_in) {
    mp_wasm_ensure_inited();
    const char *fqn = mp_obj_str_get_str(fqn_in);
    const char *exp = mp_obj_str_get_str(exp_in);
    pm_wasmmod_registry_fn_t fn = NULL;
    int32_t st = pm_wasmmod_api_connect((const uint8_t *)fqn, (uint32_t)strlen(fqn),
        (const uint8_t *)exp, (uint32_t)strlen(exp), &fn);
    return mp_obj_new_bool(st == 0 && fn != NULL);
}
static MP_DEFINE_CONST_FUN_OBJ_2(mod_wasm_connect_obj, mod_wasm_connect);

static mp_obj_t mod_wasm_verify(size_t n_args, const mp_obj_t *args) {
    mp_wasm_ensure_inited();
    if (n_args == 1 && (args[0] == mp_const_true || args[0] == mp_const_false)) {
        mp_wasm_set_verify_enabled(args[0] == mp_const_true);
        return mp_obj_new_bool(mp_wasm_get_verify_enabled());
    }
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
    char err[160];
    bool ok = mp_wasm_verify_sig((const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len, err,
        sizeof(err));
    if (!ok) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("verify: %s"), err);
    }
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_verify_obj, 1, 1, mod_wasm_verify);

/* wasm.trust_add(cert, *, zlib_len=None) — add a custom trust anchor.
 *
 * cert is either a bytes/bytearray holding a CA cert (DER or PEM) or a leaf
 * SPKI (pinned public key), or a filesystem path to such a file. zlib_len only
 * applies to bytes input and says the payload is zlib-compressed with that
 * uncompressed size (the generated wasm_trust_ca.c framing). Returns the new
 * trust-anchor count.
 */
static mp_obj_t mod_wasm_trust_add(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    mp_wasm_ensure_inited();
    enum { ARG_cert, ARG_zlib_len };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_cert, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_zlib_len, MP_ARG_KW_ONLY | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (mp_obj_is_str(args[ARG_cert].u_obj)) {
        const char *path = mp_obj_str_get_str(args[ARG_cert].u_obj);
        uint8_t *bytes = NULL;
        size_t blen = 0;
        if (!mp_wasm_read_file(path, &bytes, &blen)) {
            mp_raise_OSError_with_filename(MP_ENOENT, path);
        }
        bool ok = mp_wasm_trust_add(bytes, blen);
        m_del(uint8_t, bytes, blen);
        if (!ok) {
            mp_raise_ValueError(MP_ERROR_TEXT("trust_add: anchor not accepted"));
        }
        return mp_obj_new_int_from_uint(mp_wasm_trust_count());
    }

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_cert].u_obj, &bufinfo, MP_BUFFER_READ);

    size_t uncompressed = 0;
    if (args[ARG_zlib_len].u_obj != mp_const_none) {
        uncompressed = mp_obj_get_int(args[ARG_zlib_len].u_obj);
    }
    bool ok;
    if (uncompressed > 0 && uncompressed != bufinfo.len) {
        ok = mp_wasm_trust_add_blob((const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len,
            (uint32_t)uncompressed);
    } else {
        ok = mp_wasm_trust_add((const uint8_t *)bufinfo.buf, bufinfo.len);
    }
    if (!ok) {
        mp_raise_ValueError(MP_ERROR_TEXT("trust_add: anchor not accepted"));
    }
    return mp_obj_new_int_from_uint(mp_wasm_trust_count());
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_trust_add_obj, 1, mod_wasm_trust_add);

/* wasm.trust_apply(payload) — authenticate + install an MPTB revocation bundle.
 *
 * Returns True once the bundle has been applied for the session (see
 * trust_policy()). Raises ValueError with the cryptographic reason otherwise;
 * a failed apply leaves the current policy untouched (fails closed).
 */
static mp_obj_t mod_wasm_trust_apply(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_payload };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_payload, MP_ARG_REQUIRED | MP_ARG_OBJ, { .u_rom_obj = MP_ROM_NONE } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(args[ARG_payload].u_obj, &bufinfo, MP_BUFFER_READ);
    char err[200];
    if (!mp_wasm_trust_apply_bundle((const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len,
            err, sizeof(err))) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("trust_apply: %s"), err);
    }
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(mod_wasm_trust_apply_obj, 1, mod_wasm_trust_apply);

/* wasm.trust_reset() — clear the installed policy for the session. */
static mp_obj_t mod_wasm_trust_reset(void) {
    mp_wasm_ensure_inited();
    mp_wasm_trust_policy_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_reset_obj, mod_wasm_trust_reset);

/* wasm.trust_policy() → {applied: bool, allow: int, deny: int}. */
static mp_obj_t mod_wasm_trust_policy(void) {
    mp_wasm_ensure_inited();
    mp_obj_t d = mp_obj_new_dict(3);
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_applied),
        mp_obj_new_bool(mp_wasm_trust_policy_applied()));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_allow),
        mp_obj_new_int_from_uint(mp_wasm_trust_policy_allow_count()));
    mp_obj_dict_store(d, MP_OBJ_NEW_QSTR(MP_QSTR_deny),
        mp_obj_new_int_from_uint(mp_wasm_trust_policy_deny_count()));
    return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm_trust_policy_obj, mod_wasm_trust_policy);

static int source_list_cb(void *ctx, const char *path, size_t path_len) {
    mp_obj_t list = *(mp_obj_t *)ctx;
    mp_obj_list_append(list, mp_obj_new_str(path, path_len));
    return 0;
}

static mp_obj_t mod_wasm_source_list(mp_obj_t name_in) {
    mp_wasm_ensure_inited();
    const char *name = mp_obj_str_get_str(name_in);
    vstr_t path;
    if (!mp_wasm_find_pack(name, &path)) {
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("no pack for '%s'"), name);
    }
    uint8_t *bytes = NULL;
    size_t blen = 0;
    if (!mp_wasm_read_file(vstr_null_terminated_str(&path), &bytes, &blen)) {
        vstr_clear(&path);
        mp_raise_OSError(MP_ENOENT);
    }
    vstr_clear(&path);

    const uint8_t *p = bytes;
    uint32_t len = (uint32_t)blen;
    uint8_t *owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &len, &owned)) {
        m_del(uint8_t, bytes, blen);
        mp_raise_ValueError(MP_ERROR_TEXT("corrupt MPZL artifact"));
    }
    mp_wasm_source_view_t *v = mp_wasm_source_open_buffer(p, len);
    mp_obj_t out = mp_obj_new_list(0, NULL);
    if (v != NULL) {
        (void)mp_wasm_source_list_files(v, NULL, &out, source_list_cb);
        mp_wasm_source_close(v);
    }
    MICROPY_WASM_FREE(owned);
    m_del(uint8_t, bytes, blen);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_wasm_source_list_obj, mod_wasm_source_list);

static mp_obj_t mod_wasm___init__(void) {
    mp_wasm_ensure_inited();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_wasm___init___obj, mod_wasm___init__);

static const mp_rom_map_elem_t mp_module_pymergetic_wasmmod_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&mod_wasm___init___obj) },
    { MP_ROM_QSTR(MP_QSTR_version), MP_ROM_PTR(&mod_wasm_version_obj) },
    { MP_ROM_QSTR(MP_QSTR_has), MP_ROM_PTR(&mod_wasm_has_obj) },
    { MP_ROM_QSTR(MP_QSTR_modules), MP_ROM_PTR(&mod_wasm_modules_obj) },
    { MP_ROM_QSTR(MP_QSTR_load), MP_ROM_PTR(&mod_wasm_load_obj) },
    { MP_ROM_QSTR(MP_QSTR_unload), MP_ROM_PTR(&mod_wasm_unload_obj) },
    { MP_ROM_QSTR(MP_QSTR_call), MP_ROM_PTR(&mod_wasm_call_obj) },
    { MP_ROM_QSTR(MP_QSTR_connect), MP_ROM_PTR(&mod_wasm_connect_obj) },
    { MP_ROM_QSTR(MP_QSTR_verify), MP_ROM_PTR(&mod_wasm_verify_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_add), MP_ROM_PTR(&mod_wasm_trust_add_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_apply), MP_ROM_PTR(&mod_wasm_trust_apply_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_reset), MP_ROM_PTR(&mod_wasm_trust_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_trust_policy), MP_ROM_PTR(&mod_wasm_trust_policy_obj) },
    { MP_ROM_QSTR(MP_QSTR_source_list), MP_ROM_PTR(&mod_wasm_source_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_path), MP_ROM_PTR(&mod_wasm_path_obj_fun) },
    { MP_ROM_QSTR(MP_QSTR_path_append), MP_ROM_PTR(&mod_wasm_path_append_obj) },
    { MP_ROM_QSTR(MP_QSTR_cdn), MP_ROM_PTR(&mod_wasm_cdn_obj) },
    { MP_ROM_QSTR(MP_QSTR_cdn_prepend), MP_ROM_PTR(&mod_wasm_cdn_prepend_obj) },
    { MP_ROM_QSTR(MP_QSTR_cdn_reset), MP_ROM_PTR(&mod_wasm_cdn_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_catalog), MP_ROM_PTR(&mod_wasm_catalog_obj) },
    { MP_ROM_QSTR(MP_QSTR_search), MP_ROM_PTR(&mod_wasm_search_obj) },
    { MP_ROM_QSTR(MP_QSTR_filter), MP_ROM_PTR(&mod_wasm_filter_obj) },
    { MP_ROM_QSTR(MP_QSTR_session_id), MP_ROM_PTR(&mod_wasm_session_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish), MP_ROM_PTR(&mod_wasm_publish_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish_file), MP_ROM_PTR(&mod_wasm_publish_file_obj) },
    { MP_ROM_QSTR(MP_QSTR_install_hook), MP_ROM_PTR(&mod_wasm_install_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_uninstall_hook), MP_ROM_PTR(&mod_wasm_uninstall_hook_obj) },
    { MP_ROM_QSTR(MP_QSTR_publish_presence), MP_ROM_PTR(&mod_wasm_publish_presence_obj) },
    { MP_ROM_QSTR(MP_QSTR_test), MP_ROM_PTR(&mod_wasm_test_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_all), MP_ROM_PTR(&mod_wasm_test_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_tests), MP_ROM_PTR(&mod_wasm_tests_obj) },
    { MP_ROM_QSTR(MP_QSTR_test_count), MP_ROM_PTR(&mod_wasm_test_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_bench), MP_ROM_PTR(&mod_wasm_bench_obj) },
    { MP_ROM_QSTR(MP_QSTR_bench_all), MP_ROM_PTR(&mod_wasm_bench_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_benches), MP_ROM_PTR(&mod_wasm_benches_obj) },
    { MP_ROM_QSTR(MP_QSTR_bench_count), MP_ROM_PTR(&mod_wasm_bench_count_obj) },
    { MP_ROM_QSTR(MP_QSTR_bind_py), MP_ROM_PTR(&mod_wasm_bind_py_obj) },
#if MICROPY_PY_WASM_GEN
    { MP_ROM_QSTR(MP_QSTR_gen), MP_ROM_PTR(&mod_wasm_gen_obj) },
#endif
    { MP_ROM_QSTR(MP_QSTR_guest), MP_ROM_PTR(&mp_module_pymergetic_wasmmod_guest) },
    { MP_ROM_QSTR(MP_QSTR_net), MP_ROM_PTR(&mp_module_pymergetic_wasmmod_net) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_wasmmod_globals, mp_module_pymergetic_wasmmod_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_wasmmod_globals,
};

/* The `pymergetic` package itself is modpymergetic.c — every seat links that
 * TU, this one only on seats that want the full face. */
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod, mp_module_pymergetic_wasmmod);
