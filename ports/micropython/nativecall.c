#include "ports/micropython/nativecall.h"

#include <stdio.h>
#include <string.h>

#include "py/mperrno.h"
#include "py/runtime.h"

#include "ports/common/memcookie.h"
#include "ports/micropython/objhandle.h"
#include "pymergetic/types/__types__.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"
#ifdef PM_WASMMOD_METAL_TYPES
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/metal/edit/__types__.h"
#include "pymergetic/metal/jit/c/__types__.h"
#include "pymergetic/metal/jit/cpp/__types__.h"
#include "pymergetic/metal/workspace/__types__.h"
#endif
#include "pymergetic/util/mem.h"

/* Wasm/AOT exports are registry_fn_t trampolines (args/results), not a
 * C ABI symbol. Casting them to int32_t(void) returns the trampoline
 * status (-1) instead of the guest i32. ELF/resident stay a real C fn. */
static int wasm_container(const char *fqn) {
    int32_t k = pm_wasmmod_registry_container((const uint8_t *)fqn, (uint32_t)strlen(fqn));
    return k == (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_WASM
        || k == (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_AOT;
}

#ifdef PM_WASMMOD_METAL_TYPES
/* metal.edit C editor (Phase 12): the upy editor flow is sequential (parse,
 * locate, edit, write), so one static tree slot serves every bridge — the
 * parse_c handle is the slot's validity, exactly like the accessor spine. */
static pm_metal_edit_tree_t s_edit_tree;

/* Kernel header roots for the jit.c compile bridges — the same roots the
 * host build compiles card sources against, absolute so the REPL's compiles
 * resolve regardless of cwd. metal.mk (native seats) and micropython.mk
 * already bake PM_METAL_TCC_LIB_DIR; the src roots derive from the wasmmod
 * tree the bridge itself lives in. */
#ifndef PM_NATIVECALL_WASMMOD_ROOT
#define PM_NATIVECALL_WASMMOD_ROOT "/nonexistent/wasmmod"
#endif
#define PM_NATIVECALL_METAL_SRC PM_NATIVECALL_WASMMOD_ROOT "/../metal/src"
#define PM_NATIVECALL_WASMMOD_SRC PM_NATIVECALL_WASMMOD_ROOT "/src"
/* firmware never JIT-compiles C in-process (its jit cards refuse), so it
 * carries no TCC root — the macro degenerates to an unused empty string
 * and the compile bridges are never driven there. */
#ifndef PM_METAL_TCC_LIB_DIR
#define PM_METAL_TCC_LIB_DIR ""
#endif
#define PM_NATIVECALL_TCC_INC PM_METAL_TCC_LIB_DIR "/include"

/* metal.build wasm-seat link (Phase 13): same posture — the upy build flow is
 * compile -> link -> lookup -> destroy, so one static artifact slot serves the
 * bridges; the link face repopulates it under the rebuild contract. */
static pm_metal_build_artifact_t s_build_artifact;

/* metal.jit.cpp transpile chain: lex -> parse -> lower(/ast_dump) walks one
 * arena (tokens and AST are arena-owned; the tree is gone when the arena is).
 * The upy flow is sequential by contract — same posture as the edit tree —
 * so one static arena + toklist + unit slot serves every bridge; lex(1)
 * repopulates the slots (dropping any previous chain) and lower/ast_dump
 * read them. The parse handle returned to Python is (1): the slots, not the
 * int, carry the state. */
static pm_util_mem_arena_t *s_cpp_arena;
static pm_jit_cpp_toklist_t s_cpp_toks;
static pm_jit_cpp_ast_t *s_cpp_unit;

/* The build card's artifact faces are resolved through the registry, not
 * linked: upywm's tree is wasmmod-only (no metal objects), so a direct call
 * would be an undefined symbol there. Registry-resident on every seat that
 * boots metal.build; resolve_native + a direct C call is the same posture
 * as compile_arena_cap above (resident C faces do not speak the trampoline
 * ABI). An unregistered card = polite None, not a link error. */
static void build_artifact_destroy(pm_metal_build_artifact_t *art) {
    void (*fn)(pm_metal_build_artifact_t *);
    fn = (void (*)(pm_metal_build_artifact_t *))pm_wasmmod_registry_resolve_native(
        (const uint8_t *)"pymergetic.metal.build", 22u,
        (const uint8_t *)"pm_metal_build_artifact_destroy", 31u);
    if (fn != NULL) {
        fn(art);
    }
}

static void *build_artifact_lookup(const pm_metal_build_artifact_t *art,
    const char *name) {
    void *(*fn)(const pm_metal_build_artifact_t *, const char *);
    fn = (void *(*)(const pm_metal_build_artifact_t *, const char *))
        pm_wasmmod_registry_resolve_native(
            (const uint8_t *)"pymergetic.metal.build", 22u,
            (const uint8_t *)"pm_metal_build_artifact_lookup", 30u);
    if (fn == NULL) {
        return NULL;
    }
    return fn(art, name);
}
#endif /* PM_WASMMOD_METAL_TYPES */

/* One-shot compile scratch shared by the build and jit.py compile bridges:
 * the upy flows are sequential (the card contract, same posture as the
 * artifact slot above), and per-bridge statics would bloat firmware bss —
 * one buffer serves both. Sized for the in-arena TCC compile: the tccpp
 * token and symbol pools are 2 x 256KB and every table rides the arena
 * (jit.c's arena reallocator); firmware never compiles C in-process (the
 * jit cards' compile faces refuse or are unused there), so it carries a
 * small buffer only to keep the layout honest. */
#if defined(PM_METAL_FIRMWARE)
#define PM_NATIVECALL_COMPILE_BACKING (2u * 1024u * 1024u)
#else
#define PM_NATIVECALL_COMPILE_BACKING (32u * 1024u * 1024u)
#endif
static char s_compile_backing[PM_NATIVECALL_COMPILE_BACKING];

#ifdef PM_WASMMOD_METAL_TYPES
/* jit.cpp transpile-chain scratch: one arena lives as long as the chain
 * (lex -> parse -> lower), so it cannot share the one-shot compile backing
 * above — that one is destroyed per call. The feed uses 64MB for the card's
 * own source; the REPL chain stays up until the next lex(1) drops it.
 * Firmware never transpiles C++ in-process (the card is boot-only there),
 * so it carries the same small honest buffer. */
#if defined(PM_METAL_FIRMWARE)
#define PM_NATIVECALL_CPP_BACKING (2u * 1024u * 1024u)
#else
#define PM_NATIVECALL_CPP_BACKING (64u * 1024u * 1024u)
#endif
static char s_cpp_backing[PM_NATIVECALL_CPP_BACKING]
    __attribute__((aligned(4096)));

static void cpp_chain_drop(void) {
    if (s_cpp_arena != NULL) {
        pm_util_mem_arena_destroy(s_cpp_arena);
        s_cpp_arena = NULL;
    }
    memset(&s_cpp_toks, 0, sizeof(s_cpp_toks));
    s_cpp_unit = NULL;
}
#endif /* PM_WASMMOD_METAL_TYPES */

/* Compile-scratch budget: when the current process carries a memory budget
 * (metal.process), the one-shot compile arena is capped to it — a compile in a
 * budgeted process gets NULLs past its cap (refusal, not abort) instead of
 * enjoying the whole static backing. Unbudgeted processes keep the full
 * backing. Resolved with resolve_native + a direct C call, the same posture
 * as the signature spine below (registry_call would go through the
 * trampoline ABI, which resident C faces do not speak). */
static size_t compile_arena_cap(void) {
    int32_t (*cur_fn)(void);
    int32_t (*budget_fn)(int32_t);
    int32_t pid;
    int32_t pid_budget;
    cur_fn = (int32_t (*)(void))pm_wasmmod_registry_resolve_native(
        (const uint8_t *)"pymergetic.metal.process", 24u,
        (const uint8_t *)"pm_metal_process_current", 24u);
    if (cur_fn == NULL) {
        return sizeof(s_compile_backing);
    }
    budget_fn = (int32_t (*)(int32_t))pm_wasmmod_registry_resolve_native(
        (const uint8_t *)"pymergetic.metal.process", 24u,
        (const uint8_t *)"pm_metal_process_budget", 23u);
    if (budget_fn == NULL) {
        return sizeof(s_compile_backing);
    }
    pid = cur_fn();
    if (pid < 0) {
        return sizeof(s_compile_backing);
    }
    pid_budget = budget_fn(pid);
    if (pid_budget <= 0) {
        return sizeof(s_compile_backing);
    }
    return ((size_t)pid_budget < sizeof(s_compile_backing))
        ? (size_t)pid_budget
        : sizeof(s_compile_backing);
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

/* pymergetic.types: unwrap a 16-byte bytes object into the universal
 * value. Python sees constructors return exactly this shape. */
static void fetch_type_value(mp_obj_t obj, pm_type_value_t *out) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(obj, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len != sizeof(pm_type_value_t)) {
        mp_raise_TypeError(MP_ERROR_TEXT("types: value must be 16 bytes"));
    }
    memcpy(out, bufinfo.buf, sizeof(*out));
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
    /* asgi defer_reply_ct: same bufptr reply, plus a per-response Content-Type
     * override (a deferred route registers one type, but a raw source reply is
     * per-path — C vs Rust vs TOML). The ctype rides the const char * tail. */
    if (strcmp(sig, "int32_t(const uint8_t *, uint32_t, const char *)") == 0) {
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("bufptr+ctype needs two args"));
        }
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[0], &bufinfo, MP_BUFFER_READ);
        return mp_obj_new_int(((int32_t (*)(const uint8_t *, uint32_t, const char *))p)(
            (const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len,
            mp_obj_str_get_str(args[1])));
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
#ifdef PM_WASMMOD_METAL_TYPES
    /* metal.build change ledger: note_add appends (target, kind, reason, refs)
     * and returns the status; the refs tuple rides a const char * array. */
    if (strcmp(sig, "int32_t(const char *, pm_metal_build_note_kind_t, const char *, const char *const *, uint32_t)") == 0) {
        const char *refs[8];
        mp_obj_t *items = NULL;
        size_t n_items = 0;
        if (n_args != 3 && n_args != 4) {
            mp_raise_TypeError(MP_ERROR_TEXT("note_add needs target, kind, reason[, refs]"));
        }
        if (n_args == 4) {
            mp_obj_get_array(args[3], &n_items, &items);
            if (n_items > 8) {
                n_items = 8;
            }
        }
        for (size_t i = 0; i < n_items; ++i) {
            refs[i] = mp_obj_str_get_str(items[i]);
        }
        return mp_obj_new_int(((int32_t (*)(const char *, int32_t, const char *,
            const char *const *, uint32_t))p)(
            mp_obj_str_get_str(args[0]), (int32_t)mp_obj_get_int(args[1]),
            mp_obj_str_get_str(args[2]), refs, (uint32_t)n_items));
    }
    /* metal.build note_has: (target, kind) -> gate. */
    if (strcmp(sig, "int32_t(const char *, pm_metal_build_note_kind_t)") == 0) {
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("note_has needs target, kind"));
        }
        return mp_obj_new_int(((int32_t (*)(const char *, int32_t))p)(
            mp_obj_str_get_str(args[0]), (int32_t)mp_obj_get_int(args[1])));
    }
#endif /* PM_WASMMOD_METAL_TYPES */
    /* metal.build notes_query: (target, kind) -> the matching JSON lines as
     * one str. The out-buffer + count are marshalled here. */
    if (strcmp(sig, "int32_t(const char *, int32_t, char *, size_t, uint32_t *)") == 0) {
        static char buf[8192];
        uint32_t n_lines = 0;
        int32_t rc;
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("notes_query needs target, kind"));
        }
        rc = ((int32_t (*)(const char *, int32_t, char *, size_t, uint32_t *))p)(
            mp_obj_str_get_str(args[0]), (int32_t)mp_obj_get_int(args[1]),
            buf, sizeof(buf), &n_lines);
        if (rc < 0) {
            return mp_const_none;
        }
        return mp_obj_new_str(buf, strlen(buf));
    }
#ifdef PM_WASMMOD_METAL_TYPES
    /* metal.build wasm-seat link (Phase 13): compile(fqn, src) -> wasm module
     * bytes, link(fqn, bytes) -> load through the loader (registry publishes
     * the named exports), lookup(name) -> export existence, destroy() ->
     * unload. One static artifact slot: the upy build flow is sequential,
     * same posture as the editor's tree slot. */
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, const char *, const char *, uint8_t **, size_t *, char *, size_t)") == 0) {
        pm_util_mem_arena_t *arena;
        pm_metal_build_unit_t unit;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        int32_t rc;
        char err[PM_METAL_BUILD_ERR_MAX];
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("compile_source needs fqn, source"));
        }
        memset(&unit, 0, sizeof(unit));
        snprintf(unit.fqn, sizeof(unit.fqn), "%s", mp_obj_str_get_str(args[0]));
        {
            size_t cap = compile_arena_cap();
            arena = pm_util_mem_arena_create(s_compile_backing, cap);
        }
        if (arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const pm_metal_build_unit_t *,
            const char *, const char *, uint8_t **, size_t *, char *, size_t))p)(
            arena, &unit, NULL, mp_obj_str_get_str(args[1]),
            &obj, &obj_len, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        if (rc != PM_METAL_BUILD_OK || obj == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_bytes(obj, obj_len);
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const pm_metal_build_unit_t *, uint8_t **, const size_t *, uint32_t, pm_metal_build_artifact_t *, char *, size_t)") == 0) {
        /* PM_UTIL_MEM_MIN_SPAN is 8 pages and the base must be page-aligned:
         * over-allocate and hand the arena the aligned interior. 64MB: the
         * link relocates whole-card objects (the jit cards' self-host C is
         * ~250KB of source, ~1MB of ELF with symbol/reloc tables) and the
         * relocator's fixups ride the arena; small backings oom at load. */
        static char lbacking[64u * 1024u * 1024u] __attribute__((aligned(4096)));
        pm_util_mem_arena_t *arena;
        pm_metal_build_unit_t unit;
        uint8_t *obj;
        size_t obj_len;
        int32_t rc;
        char err[PM_METAL_BUILD_ERR_MAX];
        mp_buffer_info_t bi;
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("link needs fqn, bytes"));
        }
        mp_get_buffer_raise(args[1], &bi, MP_BUFFER_READ);
        obj = (uint8_t *)bi.buf;
        obj_len = bi.len;
        memset(&unit, 0, sizeof(unit));
        snprintf(unit.fqn, sizeof(unit.fqn), "%s", mp_obj_str_get_str(args[0]));
        memset(err, 0, sizeof(err));
        arena = pm_util_mem_arena_create(lbacking, sizeof(lbacking));
        if (arena == NULL) {
            return mp_const_none;
        }
        /* destroy the previous artifact first: the rebuild contract (the
         * loader publishes under the unit fqn; a live previous module
         * would shadow the new one). */
        build_artifact_destroy(&s_build_artifact);
        memset(&s_build_artifact, 0, sizeof(s_build_artifact));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const pm_metal_build_unit_t *,
            uint8_t **, const size_t *, uint32_t, pm_metal_build_artifact_t *,
            char *, size_t))p)(arena, &unit, &obj, &obj_len, 1,
            &s_build_artifact, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        {
            mp_obj_t pair[2];
            pair[0] = mp_obj_new_int(rc);
            pair[1] = mp_obj_new_str(err, strlen(err));
            return mp_obj_new_tuple(2, pair);
        }
    }
    if (strcmp(sig, "void *(const pm_metal_build_artifact_t *, const char *)") == 0) {
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("lookup needs name"));
        }
        if (s_build_artifact.n_loader_handles == 0
            && s_build_artifact.bytes == NULL) {
            return mp_const_none;
        }
        if (build_artifact_lookup(&s_build_artifact,
                mp_obj_str_get_str(args[0])) == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_int_from_uint(1u);
    }
    /* build.call(name, *args) -> int result. The artifact lives in the
     * static slot (link published it there); the call goes through the
     * card's artifact_call face, which routes native (ELF) or the wasm
     * trampoline by seat. Python ints/floats become i64 scalars; the
     * result comes back as the callee's return value widened to i64. */
    if (strcmp(sig, "int32_t(const pm_metal_build_artifact_t *, const char *, const int64_t *, uint32_t, int64_t *)") == 0) {
        int64_t cargs[8];
        int64_t res = 0;
        uint32_t n_cargs;
        int32_t rc;
        const char *name;
        if (n_args < 1 || n_args > 9) {
            mp_raise_TypeError(MP_ERROR_TEXT("call needs name plus up to 8 args"));
        }
        if (s_build_artifact.n_loader_handles == 0
            && s_build_artifact.bytes == NULL) {
            mp_raise_msg(&mp_type_RuntimeError,
                MP_ERROR_TEXT("call: no linked artifact (link first)"));
        }
        name = mp_obj_str_get_str(args[0]);
        for (n_cargs = 0; n_cargs < (uint32_t)(n_args - 1); n_cargs++) {
#if MICROPY_PY_BUILTINS_FLOAT
            if (mp_obj_is_float(args[1 + n_cargs])) {
                /* f64 travels as its bit pattern: the card's value union
                 * is the transport, and the callee decides the type. */
                union {
                    double d;
                    int64_t i;
                } bits;
                bits.d = mp_obj_get_float(args[1 + n_cargs]);
                cargs[n_cargs] = bits.i;
            } else {
                cargs[n_cargs] = (int64_t)mp_obj_get_int(args[1 + n_cargs]);
            }
#else
            cargs[n_cargs] = (int64_t)mp_obj_get_int(args[1 + n_cargs]);
#endif
        }
        rc = ((int32_t (*)(const pm_metal_build_artifact_t *, const char *,
            const int64_t *, uint32_t, int64_t *))p)(
            &s_build_artifact, name, &cargs[0], n_cargs, &res);
        if (rc != 0) {
            mp_raise_msg(&mp_type_RuntimeError,
                MP_ERROR_TEXT("call: artifact refused the call"));
        }
        return mp_obj_new_int_from_ull((unsigned long long)res);
    }
    if (strcmp(sig, "void(pm_metal_build_artifact_t *)") == 0) {
        build_artifact_destroy(&s_build_artifact);
        memset(&s_build_artifact, 0, sizeof(s_build_artifact));
        return mp_const_none;
    }
    /* metal.jit.cpp transpile chain: lex(src) -> handle (1), parse(handle)
     * -> handle (1), lower(handle) -> C source str, ast_dump(handle) ->
     * tree text. One arena under the whole chain (the slots above); lex(1)
     * drops any previous chain first. The handle is the slots' validity,
     * exactly like the editor's parse_c. */
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const char *, size_t, pm_jit_cpp_toklist_t *, char *, size_t)") == 0) {
        const char *src;
        int32_t rc;
        char err[256];
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("lex needs source"));
        }
        src = mp_obj_str_get_str(args[0]);
        cpp_chain_drop();
        s_cpp_arena = pm_util_mem_arena_create(s_cpp_backing, sizeof(s_cpp_backing));
        if (s_cpp_arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
            pm_jit_cpp_toklist_t *, char *, size_t))p)(
            s_cpp_arena, src, strlen(src), &s_cpp_toks, err, sizeof(err));
        if (rc != 0) {
            cpp_chain_drop();
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
        }
        return mp_obj_new_int_from_uint(1u);
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const pm_jit_cpp_toklist_t *, pm_jit_cpp_ast_t **, char *, size_t)") == 0) {
        int32_t rc;
        char err[256];
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("parse needs handle"));
        }
        if (s_cpp_arena == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("no lexed chain (lex first)"));
        }
        memset(err, 0, sizeof(err));
        s_cpp_unit = NULL;
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const pm_jit_cpp_toklist_t *,
            pm_jit_cpp_ast_t **, char *, size_t))p)(
            s_cpp_arena, &s_cpp_toks, &s_cpp_unit, err, sizeof(err));
        if (rc != 0 || s_cpp_unit == NULL) {
            s_cpp_unit = NULL;
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
        }
        return mp_obj_new_int_from_uint(1u);
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const pm_jit_cpp_ast_t *, char **, size_t *, char *, size_t)") == 0) {
        char *out = NULL;
        size_t out_len = 0;
        int32_t rc;
        char err[256];
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("lower needs handle"));
        }
        if (s_cpp_unit == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("no parsed unit (parse first)"));
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const pm_jit_cpp_ast_t *,
            char **, size_t *, char *, size_t))p)(
            s_cpp_arena, s_cpp_unit, &out, &out_len, err, sizeof(err));
        if (rc != 0 || out == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
        }
        return mp_obj_new_str(out, out_len);
    }
    if (strcmp(sig, "int32_t(const pm_jit_cpp_ast_t *, char *, size_t, char *, size_t)") == 0) {
        /* the dump rides a fixed buffer: the tree text of the card's own
         * source is ~10x the AST byte size and only ever read, so a 1MB
         * window is the honest cap (short dump -> -1 -> refusal). */
        static char dump[1024u * 1024u];
        int32_t rc;
        char err[256];
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("ast_dump needs handle"));
        }
        if (s_cpp_unit == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("no parsed unit (parse first)"));
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(const pm_jit_cpp_ast_t *, char *, size_t,
            char *, size_t))p)(s_cpp_unit, dump, sizeof(dump), err, sizeof(err));
        if (rc < 0) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
        }
        return mp_obj_new_str(dump, (size_t)rc);
    }
#endif /* PM_WASMMOD_METAL_TYPES */
    /* metal.jit.py object loop: compile(source, module_name) -> mpy bytes
     * (µPy compiling Python in-process), load(mpy_bytes, module_name) ->
     * rc (bytes back into a live module). The Python twin of build's
     * compile_source/link. Shares the one-shot compile scratch above —
     * the flows are sequential, and a second 512KB static would bloat
     * firmware bss. */
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const char *, size_t, const char *, uint8_t **, size_t *, char *, size_t)") == 0) {
        pm_util_mem_arena_t *arena;
        uint8_t *mpy = NULL;
        size_t mpy_len = 0;
        size_t src_len;
        int32_t rc;
        char err[512];
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("object_compile needs source, module_name"));
        }
        src_len = strlen(mp_obj_str_get_str(args[0]));
        {
            size_t cap = compile_arena_cap();
            arena = pm_util_mem_arena_create(s_compile_backing, cap);
        }
        if (arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const char *, size_t, const char *,
            uint8_t **, size_t *, char *, size_t))p)(
            arena, mp_obj_str_get_str(args[0]), src_len,
            mp_obj_str_get_str(args[1]), &mpy, &mpy_len, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        if (rc != 0 || mpy == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_bytes(mpy, mpy_len);
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const uint8_t *, size_t, const char *, char *, size_t)") == 0) {
        mp_buffer_info_t bi;
        int32_t rc;
        char err[512];
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("object_load needs mpy bytes, module_name"));
        }
        mp_get_buffer_raise(args[0], &bi, MP_BUFFER_READ);
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const uint8_t *, size_t, const char *,
            char *, size_t))p)(
            NULL, (const uint8_t *)bi.buf, bi.len,
            mp_obj_str_get_str(args[1]), err, sizeof(err));
        return mp_obj_new_int(rc);
    }
    /* metal.jit.c object compile: object_compile(source) -> object bytes
     * (ELF on native seats, a wasm module on wasm32) — the kernel's own C
     * compiler card, TCC, in-process. The Python twin of the build card's
     * compile_source (which carries the unit/fqn seam); this face is the
     * raw one the cpp/rsx transpile chains hand their lowered C to.
     * compile_opts(source, include_dirs, defines) carries the seam the
     * feeds drive: include roots for the lowered C's own #includes, plus
     * NAME / NAME=VALUE defines. */
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const char *, size_t, uint8_t **, size_t *, char *, size_t)") == 0) {
        pm_util_mem_arena_t *arena;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        size_t src_len;
        int32_t rc;
        char err[512];
        if (n_args != 1 && n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("object_compile needs source"));
        }
        if (n_args == 2) {
            /* the target kwarg folded in: reroute to the target-aware face
             * so the plain sig never has to grow one of its own */
            int32_t (*target_fn)(pm_util_mem_arena_t *, const char *, size_t,
                const char **, uint32_t, const char **, uint32_t, int32_t,
                uint8_t **, size_t *, char *, size_t);
            target_fn = (int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
                const char **, uint32_t, const char **, uint32_t, int32_t,
                uint8_t **, size_t *, char *, size_t))
                pm_wasmmod_registry_resolve_native(
                    (const uint8_t *)"pymergetic.metal.jit.c", 22u,
                    (const uint8_t *)"pm_metal_jit_c_object_compile_target", 36u);
            if (target_fn != NULL) {
                const char *tincs[24];
                size_t t_n_incs = 0;
#ifdef PM_WASMMOD_METAL_TYPES
                tincs[t_n_incs++] = PM_NATIVECALL_METAL_SRC;
                tincs[t_n_incs++] = PM_NATIVECALL_WASMMOD_SRC;
                tincs[t_n_incs++] = PM_NATIVECALL_WASMMOD_ROOT;
                tincs[t_n_incs++] = PM_NATIVECALL_TCC_INC;
#endif
                {
                    size_t cap = compile_arena_cap();
                    arena = pm_util_mem_arena_create(s_compile_backing, cap);
                }
                if (arena == NULL) {
                    return mp_const_none;
                }
                memset(err, 0, sizeof(err));
                rc = target_fn(arena, mp_obj_str_get_str(args[0]),
                    strlen(mp_obj_str_get_str(args[0])),
                    tincs, (uint32_t)t_n_incs, NULL, 0,
                    (int32_t)mp_obj_get_int(args[1]),
                    &obj, &obj_len, err, sizeof(err));
                pm_util_mem_arena_destroy(arena);
                if (rc != 0 || obj == NULL) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
                }
                return mp_obj_new_bytes(obj, obj_len);
            }
            mp_raise_TypeError(MP_ERROR_TEXT("object_compile: target arg not supported on this seat"));
        }
        src_len = strlen(mp_obj_str_get_str(args[0]));
        /* Plain object_compile carries no include seam, so route through the
         * card's _opts sibling with the kernel's own header roots — the
         * REPL's lowered C must resolve its #includes wherever the cwd
         * points (the feeds pass the same roots explicitly). */
        {
            const char *incs[24];
            size_t n_incs = 0;
            int32_t (*opts_fn)(pm_util_mem_arena_t *, const char *, size_t,
                const char **, uint32_t, const char **, uint32_t,
                uint8_t **, size_t *, char *, size_t);
            opts_fn = (int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
                const char **, uint32_t, const char **, uint32_t,
                uint8_t **, size_t *, char *, size_t))
                pm_wasmmod_registry_resolve_native(
                    (const uint8_t *)"pymergetic.metal.jit.c", 22u,
                    (const uint8_t *)"pm_metal_jit_c_object_compile_opts", 34u);
            if (opts_fn == NULL) {
                /* no _opts on this seat (unregistered card): the raw face */
                size_t cap = compile_arena_cap();
                arena = pm_util_mem_arena_create(s_compile_backing, cap);
                if (arena == NULL) {
                    return mp_const_none;
                }
                memset(err, 0, sizeof(err));
                rc = ((int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
                    uint8_t **, size_t *, char *, size_t))p)(
                    arena, mp_obj_str_get_str(args[0]), src_len,
                    &obj, &obj_len, err, sizeof(err));
                pm_util_mem_arena_destroy(arena);
                if (rc != 0 || obj == NULL) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
                }
                return mp_obj_new_bytes(obj, obj_len);
            }
#ifdef PM_WASMMOD_METAL_TYPES
            incs[n_incs++] = PM_NATIVECALL_METAL_SRC;
            incs[n_incs++] = PM_NATIVECALL_WASMMOD_SRC;
            incs[n_incs++] = PM_NATIVECALL_WASMMOD_ROOT;
            incs[n_incs++] = PM_NATIVECALL_TCC_INC;
#endif
            {
                size_t cap = compile_arena_cap();
                arena = pm_util_mem_arena_create(s_compile_backing, cap);
            }
            if (arena == NULL) {
                return mp_const_none;
            }
            memset(err, 0, sizeof(err));
            rc = opts_fn(arena, mp_obj_str_get_str(args[0]), src_len,
                incs, (uint32_t)n_incs, NULL, 0,
                &obj, &obj_len, err, sizeof(err));
            pm_util_mem_arena_destroy(arena);
            if (rc != 0 || obj == NULL) {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
            }
            return mp_obj_new_bytes(obj, obj_len);
        }
    }
    /* metal.jit.c cross-compile knob: object_compile(source, target=N) ->
     * object bytes made by the Nth backend. TARGET 0 is the seat's own
     * backend (the path above); TARGET 1 is wasm32 — on ELF seats that
     * means the second, pm_tccw_-prefixed TCC instance, on wasm32 seats
     * the native one. Seats without the requested backend refuse with
     * the card's errbuf, never a silent fallback. */
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const char *, size_t, const char **, uint32_t, const char **, uint32_t, int32_t, uint8_t **, size_t *, char *, size_t)") == 0) {
        pm_util_mem_arena_t *arena;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        size_t src_len;
        int32_t rc;
        char err[512];
        const char *incs[24];
        size_t n_incs = 0;
        mp_int_t target = 0;
        if (n_args != 1 && n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("object_compile needs source[, target]"));
        }
        if (n_args == 2 && args[1] != mp_const_none) {
            target = mp_obj_get_int(args[1]);
        }
        src_len = strlen(mp_obj_str_get_str(args[0]));
#ifdef PM_WASMMOD_METAL_TYPES
        incs[n_incs++] = PM_NATIVECALL_METAL_SRC;
        incs[n_incs++] = PM_NATIVECALL_WASMMOD_SRC;
        incs[n_incs++] = PM_NATIVECALL_WASMMOD_ROOT;
        incs[n_incs++] = PM_NATIVECALL_TCC_INC;
#endif
        {
            size_t cap = compile_arena_cap();
            arena = pm_util_mem_arena_create(s_compile_backing, cap);
        }
        if (arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
            const char **, uint32_t, const char **, uint32_t, int32_t,
            uint8_t **, size_t *, char *, size_t))p)(
            arena, mp_obj_str_get_str(args[0]), src_len,
            incs, (uint32_t)n_incs, NULL, 0, (int32_t)target,
            &obj, &obj_len, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        if (rc != 0 || obj == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
        }
        return mp_obj_new_bytes(obj, obj_len);
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const char *, size_t, const char **, uint32_t, const char **, uint32_t, uint8_t **, size_t *, char *, size_t)") == 0) {
        pm_util_mem_arena_t *arena;
        uint8_t *obj = NULL;
        size_t obj_len = 0;
        size_t src_len;
        int32_t rc;
        char err[512];
        const char *incs[24];
        const char *defs[16];
        size_t n_incs = 0;
        size_t n_defs = 0;
        mp_obj_t *items = NULL;
        size_t n_items = 0;
        if (n_args != 1 && n_args != 2 && n_args != 3) {
            mp_raise_TypeError(MP_ERROR_TEXT("compile_opts needs source[, include_dirs][, defines]"));
        }
        src_len = strlen(mp_obj_str_get_str(args[0]));
        /* The kernel's own header roots ride every compile — the same roots
         * the host build compiles card sources against (metal src,
         * wasmmod src + root, and TCC's freestanding headers for stddef
         * etc.). Baked at compile time, so the REPL's lowered C always
         * resolves its #includes wherever the cwd points; the caller's
         * list rides after them. */
#ifdef PM_WASMMOD_METAL_TYPES
        incs[n_incs++] = PM_NATIVECALL_METAL_SRC;
        incs[n_incs++] = PM_NATIVECALL_WASMMOD_SRC;
        incs[n_incs++] = PM_NATIVECALL_WASMMOD_ROOT;
        incs[n_incs++] = PM_NATIVECALL_TCC_INC;
#endif
        if (n_args >= 2 && args[1] != mp_const_none) {
            mp_obj_get_array(args[1], &n_items, &items);
            for (size_t i = 0; i < n_items && n_incs < 24; ++i) {
                incs[n_incs++] = mp_obj_str_get_str(items[i]);
            }
        }
        if (n_args == 3 && args[2] != mp_const_none) {
            mp_obj_get_array(args[2], &n_items, &items);
            n_defs = n_items < 16 ? n_items : 16;
            for (size_t i = 0; i < n_defs; ++i) {
                defs[i] = mp_obj_str_get_str(items[i]);
            }
        }
        {
            size_t cap = compile_arena_cap();
            arena = pm_util_mem_arena_create(s_compile_backing, cap);
        }
        if (arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const char *, size_t,
            const char **, uint32_t, const char **, uint32_t,
            uint8_t **, size_t *, char *, size_t))p)(
            arena, mp_obj_str_get_str(args[0]), src_len,
            incs, (uint32_t)n_incs, defs, (uint32_t)n_defs,
            &obj, &obj_len, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        if (rc != 0 || obj == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT(err));
        }
        return mp_obj_new_bytes(obj, obj_len);
    }
#ifdef PM_WASMMOD_METAL_TYPES
    /* metal.build accessor spine (Phase 11): at(fqn, name) -> handle (an
     * int; 0 = unresolvable), at_info(handle) -> dict of the joined answer,
     * at_ast(handle) -> (has_editor, lang). The struct out-param of
     * at_info is marshalled field-by-field here. */
    if (strcmp(sig, "pm_metal_build_at_handle_t(const char *, const char *)") == 0) {
        const char *name = NULL;
        if (n_args != 1 && n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("at needs fqn[, name]"));
        }
        if (n_args == 2 && args[1] != mp_const_none) {
            name = mp_obj_str_get_str(args[1]);
        }
        return mp_obj_new_int_from_uint(((uint32_t (*)(const char *, const char *))p)(
            mp_obj_str_get_str(args[0]), name));
    }
    if (strcmp(sig, "int32_t(pm_metal_build_at_handle_t, pm_metal_build_at_info_t *)") == 0) {
        static pm_metal_build_at_info_t info;
        int32_t rc;
        mp_obj_t items[16];
        size_t n_items = 0;
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("at_info needs handle"));
        }
        memset(&info, 0, sizeof(info));
        rc = ((int32_t (*)(uint32_t, pm_metal_build_at_info_t *))p)(
            (uint32_t)mp_obj_get_int(args[0]), &info);
        if (rc != 0) {
            return mp_const_none;
        }
        /* fqn, name, kind, lang, sig, has_record, n_sources, n_syms, doc,
         * file, line, notes, n_notes, deps (tuple), n_deps */
        items[n_items++] = mp_obj_new_str(info.fqn, strlen(info.fqn));
        items[n_items++] = mp_obj_new_str(info.name, strlen(info.name));
        items[n_items++] = mp_obj_new_str(info.kind, strlen(info.kind));
        items[n_items++] = mp_obj_new_str(info.lang, strlen(info.lang));
        items[n_items++] = mp_obj_new_str(info.sig, strlen(info.sig));
        items[n_items++] = mp_obj_new_int(info.has_record);
        items[n_items++] = mp_obj_new_int(info.n_sources);
        items[n_items++] = mp_obj_new_int(info.n_syms);
        items[n_items++] = mp_obj_new_str(info.doc, strlen(info.doc));
        items[n_items++] = mp_obj_new_str(info.file, strlen(info.file));
        items[n_items++] = mp_obj_new_int(info.line);
        items[n_items++] = mp_obj_new_str(info.notes, strlen(info.notes));
        {
            mp_obj_t deps[PM_METAL_BUILD_AT_REFS_MAX];
            uint32_t i;
            for (i = 0; i < info.n_deps && i < PM_METAL_BUILD_AT_REFS_MAX; i++) {
                deps[i] = mp_obj_new_str(info.deps[i], strlen(info.deps[i]));
            }
            items[n_items++] = mp_obj_new_tuple(info.n_deps, deps);
        }
        items[n_items++] = mp_obj_new_int(info.n_deps);
        return mp_obj_new_tuple(n_items, items);
    }
    if (strcmp(sig, "int32_t(pm_metal_build_at_handle_t, char *, size_t)") == 0) {
        char lang[8];
        int32_t rc;
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("at_ast needs handle"));
        }
        memset(lang, 0, sizeof(lang));
        rc = ((int32_t (*)(uint32_t, char *, size_t))p)(
            (uint32_t)mp_obj_get_int(args[0]), lang, sizeof(lang));
        if (rc < 0) {
            return mp_const_none;
        }
        {
            mp_obj_t pair[2];
            pair[0] = mp_obj_new_int(rc);
            pair[1] = mp_obj_new_str(lang, strlen(lang));
            return mp_obj_new_tuple(2, pair);
        }
    }
    /* metal.edit C editor (Phase 12): parse_c(src) -> handle (int, 0 = parse
     * error), locate(handle, kind, name) -> (kind, name, line, span) or None,
     * set_define/set_fn_body(handle, name, text) -> edited source or None,
     * typecheck_c(src) -> (rc, err), write_back(target, path, src) -> (rc, err).
     * The tree lives in a static slot — the upy editor flow is sequential by
     * contract, same posture as the build accessor spine's info slot. */
    if (strcmp(sig, "int32_t(pm_metal_edit_tree_t *, const char *, size_t)") == 0) {
        int32_t rc;
        const char *src;
        size_t src_len;
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("parse_c needs source"));
        }
        src = mp_obj_str_get_str(args[0]);
        src_len = strlen(src);
        memset(&s_edit_tree, 0, sizeof(s_edit_tree));
        rc = ((int32_t (*)(pm_metal_edit_tree_t *, const char *, size_t))p)(
            &s_edit_tree, src, src_len);
        if (rc != 0) {
            return mp_const_none;
        }
        return mp_obj_new_int_from_uint(1u);    /* the one live tree */
    }
    if (strcmp(sig, "const pm_metal_edit_node_t *(const pm_metal_edit_tree_t *, pm_metal_edit_kind_t, const char *)") == 0) {
        const pm_metal_edit_node_t *n;
        mp_int_t kind_i;
        const char *kind_s;
        const char *name;
        if (n_args != 3) {
            mp_raise_TypeError(MP_ERROR_TEXT("locate needs handle, kind, name"));
        }
        if (mp_obj_get_int_maybe(args[1], &kind_i) && kind_i >= 0
            && kind_i <= (mp_int_t)PM_METAL_EDIT_DEFINE) {
            /* numeric kind passthrough */
        } else {
            kind_s = mp_obj_str_get_str(args[1]);
            if (strcmp(kind_s, "fn") == 0) {
                kind_i = PM_METAL_EDIT_FN;
            } else if (strcmp(kind_s, "define") == 0) {
                kind_i = PM_METAL_EDIT_DEFINE;
            } else {
                return mp_const_none;
            }
        }
        name = mp_obj_str_get_str(args[2]);
        n = ((const pm_metal_edit_node_t *(*)(const pm_metal_edit_tree_t *,
            pm_metal_edit_kind_t, const char *))p)(&s_edit_tree,
            (pm_metal_edit_kind_t)kind_i, name);
        if (n == NULL) {
            return mp_const_none;
        }
        {
            mp_obj_t items[4];
            mp_obj_t span[2];
            items[0] = mp_obj_new_int(n->kind);
            items[1] = mp_obj_new_str(n->name, strlen(n->name));
            items[2] = mp_obj_new_int(n->line);
            span[0] = mp_obj_new_int(n->span_start);
            span[1] = mp_obj_new_int(n->span_end);
            items[3] = mp_obj_new_tuple(2, span);
            return mp_obj_new_tuple(4, items);
        }
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const pm_metal_edit_tree_t *, const char *, const char *, char **, size_t *)") == 0) {
        static char backing[PM_METAL_EDIT_SRC_MAX + 4096u];
        pm_util_mem_arena_t *arena;
        char *out = NULL;
        size_t out_len = 0;
        int32_t rc;
        const char *name;
        const char *text;
        if (n_args != 3) {
            mp_raise_TypeError(MP_ERROR_TEXT("edit needs handle, name, text"));
        }
        name = mp_obj_str_get_str(args[1]);
        text = mp_obj_str_get_str(args[2]);
        arena = pm_util_mem_arena_create(backing, sizeof(backing));
        if (arena == NULL) {
            return mp_const_none;
        }
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const pm_metal_edit_tree_t *,
            const char *, const char *, char **, size_t *))p)(
            arena, &s_edit_tree, name, text, &out, &out_len);
        pm_util_mem_arena_destroy(arena);
        if (rc != 0 || out == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_str(out, out_len);
    }
    if (strcmp(sig, "int32_t(const char *, size_t, char *, size_t)") == 0) {
        char err[PM_METAL_EDIT_ERR_MAX];
        const char *src;
        int32_t rc;
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("typecheck_c needs source"));
        }
        src = mp_obj_str_get_str(args[0]);
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(const char *, size_t, char *, size_t))p)(
            src, strlen(src), err, sizeof(err));
        {
            mp_obj_t pair[2];
            pair[0] = mp_obj_new_int(rc);
            pair[1] = mp_obj_new_str(err, strlen(err));
            return mp_obj_new_tuple(2, pair);
        }
    }
    if (strcmp(sig, "int32_t(const char *, const char *, const char *, size_t, char *, size_t)") == 0) {
        char err[PM_METAL_EDIT_ERR_MAX];
        const char *target;
        const char *path;
        const char *src;
        int32_t rc;
        if (n_args != 3) {
            mp_raise_TypeError(MP_ERROR_TEXT("write_back needs target, path, source"));
        }
        target = mp_obj_str_get_str(args[0]);
        path = mp_obj_str_get_str(args[1]);
        src = mp_obj_str_get_str(args[2]);
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(const char *, const char *, const char *, size_t,
            char *, size_t))p)(target, path, src, strlen(src), err, sizeof(err));
        {
            mp_obj_t pair[2];
            pair[0] = mp_obj_new_int(rc);
            pair[1] = mp_obj_new_str(err, strlen(err));
            return mp_obj_new_tuple(2, pair);
        }
    }
#endif /* PM_WASMMOD_METAL_TYPES */
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
    /* Card source tree (pymergetic.metal.inspect): a NUL-terminated embedded
     * source string given a fqn, or a fqn+path. Both NULL-safe (None on miss),
     * so a missing card never aborts the inspector. */
    if (strcmp(sig, "const char *(const char *)") == 0) {
        const char *s;
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        s = ((const char *(*)(const char *))p)(mp_obj_str_get_str(args[0]));
        if (s == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_str(s, strlen(s));
    }
    if (strcmp(sig, "const char *(const char *, const char *)") == 0) {
        const char *s;
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        s = ((const char *(*)(const char *, const char *))p)(
            mp_obj_str_get_str(args[0]), mp_obj_str_get_str(args[1]));
        if (s == NULL) {
            return mp_const_none;
        }
        return mp_obj_new_str(s, strlen(s));
    }

    if (strcmp(sig, "int32_t(uint32_t, uint16_t, uint32_t)") == 0) {
        /* net.zenoh.peer: peer locator (addr_be, port, mode). */
        if (n_args != 3) {
            mp_raise_TypeError(MP_ERROR_TEXT("peer needs three ints"));
        }
        return mp_obj_new_int(((int32_t (*)(uint32_t, uint16_t, uint32_t))p)(
            (uint32_t)mp_obj_get_int(args[0]), (uint16_t)mp_obj_get_int(args[1]),
            (uint32_t)mp_obj_get_int(args[2])));
    }
    if (strcmp(sig, "int32_t(uint8_t *)") == 0) {
        /* net.zenoh.zid: sole export with an uint8_t * out-param; it writes the
         * 16-byte Zenoh ZID and returns 1 on readiness. Present as bytes. */
        if (n_args != 0) {
            mp_raise_TypeError(MP_ERROR_TEXT("zid takes no args"));
        }
        uint8_t z[16];
        int32_t ok = ((int32_t (*)(uint8_t *))p)(z);
        return ok ? mp_obj_new_bytes(z, 16) : mp_const_none;
    }
    if (strcmp(sig, "int32_t(const char *)") == 0) {
        /* net.swarm.membership.start: a single C string argument (the group).
         * Deferred (0) when no session is open, armed (1), misuse (-1). */
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("native call arity"));
        }
        const char *s = mp_obj_str_get_str(args[0]);
        return mp_obj_new_int(((int32_t (*)(const char *))p)(s));
    }
#ifdef PM_WASMMOD_METAL_TYPES
    if (strcmp(sig, "int32_t(char[PM_METAL_NET_SWARM_MEMBER_ID_LEN])") == 0) {
        /* net.swarm.membership.node_id: an out-buffer of the node's base-16
         * identity (40 chars). Present as bytes, mirroring zenoh zid. */
        if (n_args != 0) {
            mp_raise_TypeError(MP_ERROR_TEXT("node_id takes no args"));
        }
        char out[40];
        int32_t n = ((int32_t (*)(char[40]))p)(out);
        return n > 0 ? mp_obj_new_bytes((const uint8_t *)out, (size_t)n) : mp_const_none;
    }
    if (strcmp(sig, "int32_t(uint8_t[PM_METAL_NET_SWARM_PEER_ID_LEN], uint8_t *)") == 0) {
        /* net.swarm.discovery.scout: both params are outs (peer_id*, whatami*),
         * whatami-target is fixed to Peer by the card. Returns (rc,
         * peer_id_bytes) so the µPy guest can read the peer identity. */
        if (n_args != 0) {
            mp_raise_TypeError(MP_ERROR_TEXT("scout takes no args"));
        }
        uint8_t zid[16];
        uint8_t fwhat;
        int32_t rc = ((int32_t (*)(uint8_t *, uint8_t *))p)(zid, &fwhat);
        (void)fwhat;
        mp_obj_t items[2];
        items[0] = mp_obj_new_int(rc);
        items[1] = (rc == 1) ? mp_obj_new_bytes(zid, 16) : mp_const_none;
        return mp_obj_new_tuple(2, items);
    }
#endif /* PM_WASMMOD_METAL_TYPES */
    if (strcmp(sig, "int32_t(uint8_t, uint8_t *, uint8_t *)") == 0) {
        /* net.zenoh.scout: raw scout with a whatami-target arg + two outs.
         * Returns (rc, peer_id_bytes) too; the target rides the one arg. */
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("scout takes one arg"));
        }
        uint8_t what = (uint8_t)mp_obj_get_int(args[0]);
        uint8_t zid[16];
        uint8_t fwhat;
        int32_t rc = ((int32_t (*)(uint8_t, uint8_t *, uint8_t *))p)(what, zid, &fwhat);
        (void)fwhat;
        mp_obj_t items[2];
        items[0] = mp_obj_new_int(rc);
        items[1] = (rc == 1) ? mp_obj_new_bytes(zid, 16) : mp_const_none;
        return mp_obj_new_tuple(2, items);
    }
    if (strcmp(sig, "int32_t(const char *, const uint8_t *, uint32_t)") == 0) {
        /* net.swarm.task.dispatch: (verb, payload-bytes) -> int32 status. A byte
         * payload rides a bytes-like; verb rides a C string. */
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("dispatch needs verb+payload"));
        }
        const char *verb = mp_obj_str_get_str(args[0]);
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_READ);
        return mp_obj_new_int(((int32_t (*)(const char *, const uint8_t *, uint32_t))p)(
            verb, (const uint8_t *)bufinfo.buf, (uint32_t)bufinfo.len));
    }

    /* metal.fs read/stat (Phase 14 proves): read(path, max_len) -> bytes or
     * None, stat(path) -> (rc, len). The workspace materialize prove reads a
     * file back through fs and compares it to the embedded bytes; the bridge
     * grows the fs card to a first-class upy citizen instead of a C-only
     * face. max_len caps the read (the C face copies min(len, file)). */
    if (strcmp(sig, "int32_t(const char *, uint8_t *, uint32_t *)") == 0) {
        const char *path;
        mp_int_t max_len;
        uint8_t *buf;
        uint32_t n;
        int32_t rc;
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("read needs path+max_len"));
        }
        path = mp_obj_str_get_str(args[0]);
        max_len = mp_obj_get_int(args[1]);
        /* 2MB: card sources are ~250KB, mpy/firmware blobs are the only
         * larger bodies — a read of those is a probe, not a load. */
        if (max_len < 0 || max_len > (2u * 1024u * 1024u)) {
            mp_raise_ValueError(MP_ERROR_TEXT("read max_len out of range"));
        }
        buf = m_new(uint8_t, max_len == 0 ? 1u : (size_t)max_len);
        n = (uint32_t)max_len;
        rc = ((int32_t (*)(const char *, uint8_t *, uint32_t *))p)(path, buf, &n);
        if (rc != 0 || n == 0) {
            m_del(uint8_t, buf, max_len == 0 ? 1u : (size_t)max_len);
            return mp_const_none;
        }
        return mp_obj_new_bytes(buf, n);
    }
    if (strcmp(sig, "int32_t(const char *, uint32_t *)") == 0) {
        const char *path;
        uint32_t len = 0;
        int32_t rc;
        mp_obj_t pair[2];
        if (n_args != 1) {
            mp_raise_TypeError(MP_ERROR_TEXT("stat needs path"));
        }
        path = mp_obj_str_get_str(args[0]);
        rc = ((int32_t (*)(const char *, uint32_t *))p)(path, &len);
        pair[0] = mp_obj_new_int(rc);
        pair[1] = mp_obj_new_int_from_uint(len);
        return mp_obj_new_tuple(2, pair);
    }

#ifdef PM_WASMMOD_METAL_TYPES
    /* metal.workspace (Phase 14): materialize() -> (rc, n_files), mirror_set
     * (root) -> rc, file_count() -> int, extract_external(name, gz-bytes) ->
     * (rc, n_files). The materialize/extract arena is per-call scratch
     * (path building + the inflated tar stream); the tree itself lands in
     * the fs card's own arena. */
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, uint32_t *, char *, size_t)") == 0) {
        static char backing[64u * 1024u];
        pm_util_mem_arena_t *arena;
        uint32_t n_files = 0;
        char err[PM_METAL_WORKSPACE_ERR_MAX];
        int32_t rc;
        if (n_args != 0) {
            mp_raise_TypeError(MP_ERROR_TEXT("materialize takes no args"));
        }
        arena = pm_util_mem_arena_create(backing, sizeof(backing));
        if (arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, uint32_t *, char *, size_t))p)(
            arena, &n_files, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        if (rc != 0) {
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT(
                "materialize failed: %s"), err);
        }
        mp_obj_t pair[2];
        pair[0] = mp_obj_new_int(rc);
        pair[1] = mp_obj_new_int_from_uint(n_files);
        return mp_obj_new_tuple(2, pair);
    }
    if (strcmp(sig, "int32_t(pm_util_mem_arena_t *, const char *, const uint8_t *, size_t, uint32_t *, char *, size_t)") == 0) {
        /* the inflated tar stream is sized by the gzip trailer, so the arena
         * must fit the archive's uncompressed length: this bridge is for the
         * prove's small fixtures; a real 575 MB rustc tarball goes through
         * the C build card's host-loaded path. */
        static char backing[512u * 1024u];
        pm_util_mem_arena_t *arena;
        const char *name;
        mp_buffer_info_t bufinfo;
        uint32_t n_files = 0;
        char err[PM_METAL_WORKSPACE_ERR_MAX];
        int32_t rc;
        if (n_args != 2) {
            mp_raise_TypeError(MP_ERROR_TEXT("extract_external needs name+bytes"));
        }
        name = mp_obj_str_get_str(args[0]);
        mp_get_buffer_raise(args[1], &bufinfo, MP_BUFFER_READ);
        arena = pm_util_mem_arena_create(backing, sizeof(backing));
        if (arena == NULL) {
            return mp_const_none;
        }
        memset(err, 0, sizeof(err));
        rc = ((int32_t (*)(pm_util_mem_arena_t *, const char *, const uint8_t *,
            size_t, uint32_t *, char *, size_t))p)(
            arena, name, (const uint8_t *)bufinfo.buf, bufinfo.len,
            &n_files, err, sizeof(err));
        pm_util_mem_arena_destroy(arena);
        if (rc != 0) {
            return mp_const_none;
        }
        mp_obj_t pair[2];
        pair[0] = mp_obj_new_int(rc);
        pair[1] = mp_obj_new_int_from_uint(n_files);
        return mp_obj_new_tuple(2, pair);
    }
#endif /* PM_WASMMOD_METAL_TYPES */

    /*----------------------------------------------------------------------
     * pymergetic.types — the universal 16-byte value crosses as a 16-byte
     * bytes object (host layout: tag/aux/aux2/_rsv + payload). Constructors
     * get a bridge-owned scratch arena; compound values live in it for the
     * session (arena owns the block — same posture as the C tests). Mutators
     * (field_set, list_push, dict_set) return the updated value bytes since
     * µPy bytes are immutable; callers rebind like `v = t.set(v, ...)`.
     *--------------------------------------------------------------------*/
    if (strcmp(fqn, "pymergetic.types") == 0) {
        static char tbacking[64u * 1024u];
        static pm_util_mem_arena_t *tarena;
        if (tarena == NULL) {
            tarena = pm_util_mem_arena_create(tbacking, sizeof(tbacking));
        }
        if (tarena == NULL) {
            mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("types arena init failed"));
        }
        if (strcmp(export_name, "pm_types_nil") == 0
            || strcmp(export_name, "pm_types_i32") == 0
            || strcmp(export_name, "pm_types_i64") == 0
            || strcmp(export_name, "pm_types_f64") == 0
            || strcmp(export_name, "pm_types_bool") == 0
            || strcmp(export_name, "pm_types_str") == 0
            || strcmp(export_name, "pm_types_list_new") == 0
            || strcmp(export_name, "pm_types_dict_new") == 0) {
            /* Constructors. */
            pm_type_value_t v = pm_types_nil();
            if (strcmp(export_name, "pm_types_i32") == 0) {
                v = pm_types_i32((int32_t)mp_obj_get_int(args[0]));
            }
#if MICROPY_LONGINT_IMPL != MICROPY_LONGINT_IMPL_NONE
            else if (strcmp(export_name, "pm_types_i64") == 0) {
                v = pm_types_i64((int64_t)mp_obj_get_ll(args[0]));
            }
#endif
#if MICROPY_PY_BUILTINS_FLOAT
            else if (strcmp(export_name, "pm_types_f64") == 0) {
                v = pm_types_f64(mp_obj_get_float(args[0]));
            }
#endif
            else if (strcmp(export_name, "pm_types_bool") == 0) {
                v = pm_types_bool(mp_obj_is_true(args[0]));
            } else if (strcmp(export_name, "pm_types_str") == 0) {
                const char *s = mp_obj_str_get_str(args[0]);
                v = pm_types_str(tarena, s, (uint32_t)strlen(s));
            } else if (strcmp(export_name, "pm_types_list_new") == 0) {
                v = pm_types_list_new(tarena, (uint32_t)mp_obj_get_int(args[0]));
            } else if (strcmp(export_name, "pm_types_dict_new") == 0) {
                v = pm_types_dict_new(tarena, (uint32_t)mp_obj_get_int(args[0]));
            }
            return mp_obj_new_bytes((const byte *)&v, sizeof(v));
        }
        if (strcmp(export_name, "pm_types_kind") == 0
            || strcmp(export_name, "pm_types_is_nil") == 0
            || strcmp(export_name, "pm_types_is_struct") == 0
            || strcmp(export_name, "pm_types_registry_count") == 0) {
            /* Zero/one-arg int probes. */
            if (strcmp(export_name, "pm_types_registry_count") == 0) {
                return mp_obj_new_int_from_uint(pm_types_registry_count());
            }
            pm_type_value_t v;
            fetch_type_value(args[0], &v);
            if (strcmp(export_name, "pm_types_kind") == 0) {
                return mp_obj_new_int(pm_types_kind(v));
            }
            if (strcmp(export_name, "pm_types_is_nil") == 0) {
                return mp_obj_new_int(pm_types_is_nil(v));
            }
            return mp_obj_new_int(pm_types_is_struct(v));
        }
        if (strcmp(export_name, "pm_types_field_i32") == 0
            || strcmp(export_name, "pm_types_field_f64") == 0
            || strcmp(export_name, "pm_types_field_i64") == 0) {
            /* (value, name_hash) -> int/float; -2 hash means field miss. */
            pm_type_value_t v;
            fetch_type_value(args[0], &v);
            uint16_t hash = (uint16_t)mp_obj_get_int(args[1]);
            if (strcmp(export_name, "pm_types_field_i32") == 0) {
                int32_t out = 0;
                if (pm_types_field_i32(v, hash, &out) != 0) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("types: field miss"));
                }
                return mp_obj_new_int(out);
            }
#if MICROPY_PY_BUILTINS_FLOAT
            if (strcmp(export_name, "pm_types_field_f64") == 0) {
                double out = 0.0;
                if (pm_types_field_f64(v, hash, &out) != 0) {
                    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("types: field miss"));
                }
                return mp_obj_new_float(out);
            }
#endif
            int64_t out = 0;
#if MICROPY_LONGINT_IMPL != MICROPY_LONGINT_IMPL_NONE
            if (pm_types_field_i64(v, hash, &out) != 0) {
                mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("types: field miss"));
            }
            return mp_obj_new_int_from_ll(out);
#else
            (void)out;
            mp_raise_TypeError(MP_ERROR_TEXT("types: i64 needs longint"));
#endif
        }
        if (strcmp(export_name, "pm_types_field_set_i32") == 0
            || strcmp(export_name, "pm_types_field_set_f64") == 0) {
            /* (value, name_hash, newval) -> updated value bytes. */
            pm_type_value_t v;
            fetch_type_value(args[0], &v);
            uint16_t hash = (uint16_t)mp_obj_get_int(args[1]);
            if (strcmp(export_name, "pm_types_field_set_i32") == 0) {
                (void)pm_types_field_set_i32(&v, hash, (int32_t)mp_obj_get_int(args[2]));
            }
#if MICROPY_PY_BUILTINS_FLOAT
            else {
                (void)pm_types_field_set_f64(&v, hash, mp_obj_get_float(args[2]));
            }
#endif
            return mp_obj_new_bytes((const byte *)&v, sizeof(v));
        }
        if (strcmp(export_name, "pm_types_list_push") == 0) {
            pm_type_value_t v, item;
            fetch_type_value(args[0], &v);
            fetch_type_value(args[1], &item);
            (void)pm_types_list_push(&v, item);
            return mp_obj_new_bytes((const byte *)&v, sizeof(v));
        }
        if (strcmp(export_name, "pm_types_dict_set") == 0) {
            pm_type_value_t v, key, val;
            fetch_type_value(args[0], &v);
            fetch_type_value(args[1], &key);
            fetch_type_value(args[2], &val);
            (void)pm_types_dict_set(&v, key, val);
            return mp_obj_new_bytes((const byte *)&v, sizeof(v));
        }
        if (strcmp(export_name, "pm_types_registry_find") == 0) {
            const pm_type_descriptor_t *d = pm_types_registry_find(
                mp_obj_str_get_str(args[0]));
            if (d == NULL) {
                return mp_const_none;
            }
            return mp_obj_new_int_from_uint((mp_uint_t)d);
        }
        if (strcmp(export_name, "pm_types_name_hash") == 0) {
            return mp_obj_new_int(pm_types_name_hash(mp_obj_str_get_str(args[0])));
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("unsupported native sig (use CONNECT from C/RS)"));
}
