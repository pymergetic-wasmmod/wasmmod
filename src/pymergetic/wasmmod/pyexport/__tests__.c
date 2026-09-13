/* pymergetic.wasmmod.pyexport -- module tests (__tests__.c).
 * The pyexport card publishes typed trampoline pools so Python callables
 * appear as C ABI function pointers in the registry.  Without uPy the
 * compiled stubs always return -1; with uPy the pools fill and the
 * error-path guards operate on real mp_obj_t.
 *
 * Build: the host metal `make test` sets MICROPY_PY_WASM=1 via CPPFLAGS,
 * so the real trampoline code (not the stubs) is exercised. */
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/wasmmod/pyexport.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "wasmmod.pyexport test: %s\n", why);
    return 1;
}

/* A non-callable Python object for error-path tests.
 * Under uPy: mp_obj_new_int(0) -- an int, not callable.
 * Without uPy: a bare pointer; the stub doesn't inspect, rejects -1 anyway. */
#if MICROPY_PY_WASM
#include "py/obj.h"
static pm_wasmmod_py_obj_t s_not_callable(void) {
    return mp_obj_new_int(0);
}
#else
static pm_wasmmod_py_obj_t s_not_callable(void) {
    return (pm_wasmmod_py_obj_t)(uintptr_t)1;
}
#endif

static const char *const k_mod = "pymergetic.wasmmod.pyexport.tests";
static const char *const k_name = "pm_test_fn";

/* ---- null argument rejection ---- */

static int32_t case_null_fqn(void) {
    pm_wasmmod_py_obj_t nc = s_not_callable();
    if (pm_wasmmod_pyexport_export_py(NULL, k_name, nc, 0) != -1)
        return fail("export_py NULL fqn");
    if (pm_wasmmod_pyexport_export_py_i64(NULL, k_name, nc) != -1)
        return fail("export_py_i64 NULL fqn");
    if (pm_wasmmod_pyexport_export_py_f32(NULL, k_name, nc) != -1)
        return fail("export_py_f32 NULL fqn");
    if (pm_wasmmod_pyexport_export_py_f64(NULL, k_name, nc) != -1)
        return fail("export_py_f64 NULL fqn");
    if (pm_wasmmod_pyexport_export_py_bufptr(NULL, k_name, nc) != -1)
        return fail("export_py_bufptr NULL fqn");
    if (pm_wasmmod_pyexport_export_py_mem(NULL, k_name, nc) != -1)
        return fail("export_py_mem NULL fqn");
    if (pm_wasmmod_pyexport_export_py_obj(NULL, k_name, nc) != -1)
        return fail("export_py_obj NULL fqn");
    return 0;
}

static int32_t case_null_name(void) {
    pm_wasmmod_py_obj_t nc = s_not_callable();
    if (pm_wasmmod_pyexport_export_py(k_mod, NULL, nc, 0) != -1)
        return fail("export_py NULL name");
    if (pm_wasmmod_pyexport_export_py_i64(k_mod, NULL, nc) != -1)
        return fail("export_py_i64 NULL name");
    if (pm_wasmmod_pyexport_export_py_f32(k_mod, NULL, nc) != -1)
        return fail("export_py_f32 NULL name");
    if (pm_wasmmod_pyexport_export_py_f64(k_mod, NULL, nc) != -1)
        return fail("export_py_f64 NULL name");
    if (pm_wasmmod_pyexport_export_py_bufptr(k_mod, NULL, nc) != -1)
        return fail("export_py_bufptr NULL name");
    if (pm_wasmmod_pyexport_export_py_mem(k_mod, NULL, nc) != -1)
        return fail("export_py_mem NULL name");
    if (pm_wasmmod_pyexport_export_py_obj(k_mod, NULL, nc) != -1)
        return fail("export_py_obj NULL name");
    return 0;
}

static int32_t case_null_callable(void) {
    pm_wasmmod_py_obj_t null_obj = PM_WASMMOD_PY_OBJ_NULL;
    if (pm_wasmmod_pyexport_export_py(k_mod, k_name, null_obj, 0) != -1)
        return fail("export_py NULL callable");
    if (pm_wasmmod_pyexport_export_py_i64(k_mod, k_name, null_obj) != -1)
        return fail("export_py_i64 NULL callable");
    if (pm_wasmmod_pyexport_export_py_f32(k_mod, k_name, null_obj) != -1)
        return fail("export_py_f32 NULL callable");
    if (pm_wasmmod_pyexport_export_py_f64(k_mod, k_name, null_obj) != -1)
        return fail("export_py_f64 NULL callable");
    if (pm_wasmmod_pyexport_export_py_bufptr(k_mod, k_name, null_obj) != -1)
        return fail("export_py_bufptr NULL callable");
    if (pm_wasmmod_pyexport_export_py_mem(k_mod, k_name, null_obj) != -1)
        return fail("export_py_mem NULL callable");
    if (pm_wasmmod_pyexport_export_py_obj(k_mod, k_name, null_obj) != -1)
        return fail("export_py_obj NULL callable");
    return 0;
}

/* ---- bind_module null-arg rejection ---- */

static int32_t case_bind_module_null(void) {
    if (pm_wasmmod_pyexport_bind_module(NULL, s_not_callable()) != -1)
        return fail("bind_module NULL fqn");
    if (pm_wasmmod_pyexport_bind_module(k_mod, PM_WASMMOD_PY_OBJ_NULL) != -1)
        return fail("bind_module NULL module");
    return 0;
}

/* ---- arity gate: 4+ rejected ---- */

static int32_t case_arity(void) {
    pm_wasmmod_py_obj_t nc = s_not_callable();
    if (pm_wasmmod_pyexport_export_py(k_mod, "pm_test_fn4", nc, 4u) != -1)
        return fail("nargs=4 accepted");
    if (pm_wasmmod_pyexport_export_py(k_mod, "pm_test_fnmax", nc, ~0u) != -1)
        return fail("nargs=UINT32_MAX accepted");
    return 0;
}

/* ---- pool fill (uPy only) ---- */

#if MICROPY_PY_WASM
#include "py/runtime.h"

static int32_t case_zero_arg_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    int i;
    char buf[64];

    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    for (i = 0; i < 8; i++) {
        snprintf(buf, sizeof(buf), "pz.%d", i);
        if (pm_wasmmod_pyexport_export_py(buf, k_name, cb, 0) != 0)
            return fail("zero-arg fill");
    }
    if (pm_wasmmod_pyexport_export_py("pz.9", k_name, cb, 0) != -1)
        return fail("zero-arg pool 9th");
    return 0;
}

static int32_t case_one_arg_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    int i;
    char buf[64];

    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    for (i = 0; i < 8; i++) {
        snprintf(buf, sizeof(buf), "p1.%d", i);
        if (pm_wasmmod_pyexport_export_py(buf, k_name, cb, 1) != 0)
            return fail("one-arg fill");
    }
    if (pm_wasmmod_pyexport_export_py("p1.9", k_name, cb, 1) != -1)
        return fail("one-arg pool 9th");
    return 0;
}

static int32_t case_two_arg_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py("p2a", k_name, cb, 2) != 0
        || pm_wasmmod_pyexport_export_py("p2b", k_name, cb, 2) != 0
        || pm_wasmmod_pyexport_export_py("p2c", k_name, cb, 2) != 0
        || pm_wasmmod_pyexport_export_py("p2d", k_name, cb, 2) != 0)
        return fail("two-arg fill");
    if (pm_wasmmod_pyexport_export_py("p2.5", k_name, cb, 2) != -1)
        return fail("two-arg pool 5th");
    return 0;
}

static int32_t case_three_arg_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py("p3a", k_name, cb, 3) != 0
        || pm_wasmmod_pyexport_export_py("p3b", k_name, cb, 3) != 0
        || pm_wasmmod_pyexport_export_py("p3c", k_name, cb, 3) != 0
        || pm_wasmmod_pyexport_export_py("p3d", k_name, cb, 3) != 0)
        return fail("three-arg fill");
    if (pm_wasmmod_pyexport_export_py("p3.5", k_name, cb, 3) != -1)
        return fail("three-arg pool 5th");
    return 0;
}

static int32_t case_i64_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py_i64("i4a", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_i64("i4b", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_i64("i4c", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_i64("i4d", k_name, cb) != 0)
        return fail("i64 fill");
    if (pm_wasmmod_pyexport_export_py_i64("i4.5", k_name, cb) != -1)
        return fail("i64 pool 5th");
    return 0;
}

static int32_t case_f32_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py_f32("fsa", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_f32("fsb", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_f32("fsc", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_f32("fsd", k_name, cb) != 0)
        return fail("f32 fill");
    if (pm_wasmmod_pyexport_export_py_f32("fs.5", k_name, cb) != -1)
        return fail("f32 pool 5th");
    return 0;
}

static int32_t case_f64_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py_f64("fda", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_f64("fdb", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_f64("fdc", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_f64("fdd", k_name, cb) != 0)
        return fail("f64 fill");
    if (pm_wasmmod_pyexport_export_py_f64("fd.5", k_name, cb) != -1)
        return fail("f64 pool 5th");
    return 0;
}

static int32_t case_bufptr_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py_bufptr("bpa", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_bufptr("bpb", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_bufptr("bpc", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_bufptr("bpd", k_name, cb) != 0)
        return fail("bufptr fill");
    if (pm_wasmmod_pyexport_export_py_bufptr("bp.5", k_name, cb) != -1)
        return fail("bufptr pool 5th");
    return 0;
}

static int32_t case_mem_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py_mem("mma", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_mem("mmb", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_mem("mmc", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_mem("mmd", k_name, cb) != 0)
        return fail("mem fill");
    if (pm_wasmmod_pyexport_export_py_mem("mm.5", k_name, cb) != -1)
        return fail("mem pool 5th");
    return 0;
}

static int32_t case_obj_pool(void) {
    nlr_buf_t nlr;
    mp_obj_t cb;
    if (nlr_push(&nlr) == 0) {
        cb = mp_load_name(MP_QSTR_print);
        nlr_pop();
    } else {
        return 0;
    }
    if (cb == MP_OBJ_NULL || !mp_obj_is_callable(cb))
        return 0;

    if (pm_wasmmod_pyexport_export_py_obj("ooa", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_obj("oob", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_obj("ooc", k_name, cb) != 0
        || pm_wasmmod_pyexport_export_py_obj("ood", k_name, cb) != 0)
        return fail("obj fill");
    if (pm_wasmmod_pyexport_export_py_obj("oo.5", k_name, cb) != -1)
        return fail("obj pool 5th");
    return 0;
}

/* ---- bind_module empty registry (uPy only) ---- */

static int32_t case_bind_module_empty(void) {
    mp_obj_t mod = mp_obj_new_dict(0);
    int ret = pm_wasmmod_pyexport_bind_module(
        "pymergetic.nowhere.nothing", mod);
    if (ret != 0)
        return fail("bind_module empty registry");
    return 0;
}

#endif /* MICROPY_PY_WASM */

/* ---- driver ---- */

static int32_t pm_wasmmod_pyexport_tests(void) {
    int32_t st;
    st = case_null_fqn();
    if (st == 0) st = case_null_name();
    if (st == 0) st = case_null_callable();
    if (st == 0) st = case_bind_module_null();
    if (st == 0) st = case_arity();
#if MICROPY_PY_WASM
    if (st == 0) st = case_zero_arg_pool();
    if (st == 0) st = case_one_arg_pool();
    if (st == 0) st = case_two_arg_pool();
    if (st == 0) st = case_three_arg_pool();
    if (st == 0) st = case_i64_pool();
    if (st == 0) st = case_f32_pool();
    if (st == 0) st = case_f64_pool();
    if (st == 0) st = case_bufptr_pool();
    if (st == 0) st = case_mem_pool();
    if (st == 0) st = case_obj_pool();
    if (st == 0) st = case_bind_module_empty();
#endif
    return st;
}

PM_MOD_TEST_C(pymergetic.wasmmod.pyexport, tests,
    pm_wasmmod_pyexport_tests);
