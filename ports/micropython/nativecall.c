#include "ports/micropython/nativecall.h"

#include <string.h>

#include "py/mperrno.h"
#include "py/runtime.h"

#include "ports/common/memcookie.h"
#include "ports/micropython/objhandle.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"
#include "pymergetic/metal/build/__types__.h"

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

    mp_raise_ValueError(MP_ERROR_TEXT("unsupported native sig (use CONNECT from C/RS)"));
}
