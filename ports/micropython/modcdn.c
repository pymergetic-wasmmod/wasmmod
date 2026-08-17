/*
 * pymergetic.wasmmod.net.cdn — µPy face of the C card.
 *
 * Same module on unix, firmware, and emcc. Unix already registers
 * pymergetic.wasmmod (modwasmmod.c); this TU still registers .net / .net.cdn.
 */
#include "ports/micropython/modcdn.h"

#include "ports/common/boot.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "pymergetic/wasmmod/io/__exports__.h"
#include "pymergetic/wasmmod/net/cdn.h"
#if !MICROPY_PY_WASM_FULL
#include "ports/micropython/importhook.h"
#endif
#if MICROPY_WASM_FREESTANDING
#include "pymergetic/wasmmod/pack/alloc.h"
#endif

#include <stdlib.h>
#include <string.h>

extern const mp_obj_module_t mp_module_pymergetic_wasmmod_guest;

/* io.fetch bytes: on a freestanding seat the io fill sits in this image and
 * allocated from the image heap, so free it there. Elsewhere the fill is a
 * cargo TU on libc malloc — and a host heap macro here would be the wrong
 * allocator for those bytes. */
static void cdn_fetch_free(uint8_t *buf) {
#if MICROPY_WASM_FREESTANDING
    MICROPY_WASM_FREE(buf);
#else
    free(buf);
#endif
}

/* A CDN fetch needs whatever kernel is under us up first. Weak default on a
 * plain wasmmod seat says "nothing to boot, io is already up". */
static void cdn_ensure(void) {
    if (pm_wasmmod_host_kernel_ready() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("host kernel boot failed"));
    }
}

static const char *cdn_token_arg(size_t n_args, const mp_obj_t *args) {
    if (n_args < 2 || args[1] == mp_const_none) {
        return NULL;
    }
    if (!mp_obj_is_str(args[1])) {
        mp_raise_TypeError(MP_ERROR_TEXT("cdn: token must be str"));
    }
    return mp_obj_str_get_str(args[1]);
}

static mp_obj_t cdn_configure(size_t n_args, const mp_obj_t *args) {
    const char *url;
    const char *name;
    cdn_ensure();
    if (!mp_obj_is_str(args[0])) {
        mp_raise_TypeError(MP_ERROR_TEXT("configure(url, token=None)"));
    }
    url = mp_obj_str_get_str(args[0]);
    if (!pm_wasmmod_io_uri_is_http(url)) {
        mp_raise_ValueError(MP_ERROR_TEXT("cdn: url must be http(s)"));
    }
    pm_wasmmod_net_cdn_configure(url, cdn_token_arg(n_args, args));
    name = pm_wasmmod_net_cdn_driver_name();
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(cdn_configure_obj, 1, 2, cdn_configure);

static mp_obj_t cdn_prepend(size_t n_args, const mp_obj_t *args) {
    const char *url;
    const char *name;
    cdn_ensure();
    if (!mp_obj_is_str(args[0])) {
        mp_raise_TypeError(MP_ERROR_TEXT("prepend(url, token=None)"));
    }
    url = mp_obj_str_get_str(args[0]);
    if (!pm_wasmmod_io_uri_is_http(url)) {
        mp_raise_ValueError(MP_ERROR_TEXT("cdn: url must be http(s)"));
    }
    if (pm_wasmmod_net_cdn_base_count() == 0) {
        pm_wasmmod_net_cdn_configure(url, cdn_token_arg(n_args, args));
    } else {
        (void)pm_wasmmod_net_cdn_prepend(url, cdn_token_arg(n_args, args));
    }
    name = pm_wasmmod_net_cdn_driver_name();
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(cdn_prepend_obj, 1, 2, cdn_prepend);

static mp_obj_t cdn_reset(void) {
    cdn_ensure();
    pm_wasmmod_net_cdn_reset();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(cdn_reset_obj, cdn_reset);

static mp_obj_t cdn_session_id(size_t n_args, const mp_obj_t *args) {
    const char *sid;
    cdn_ensure();
    if (n_args >= 1) {
        if (args[0] == mp_const_none) {
            pm_wasmmod_net_cdn_set_session_id(NULL);
        } else if (mp_obj_is_str(args[0])) {
            pm_wasmmod_net_cdn_set_session_id(mp_obj_str_get_str(args[0]));
        } else {
            mp_raise_TypeError(MP_ERROR_TEXT("session_id must be str or None"));
        }
    }
    sid = pm_wasmmod_net_cdn_session_id();
    if (sid == NULL || sid[0] == '\0') {
        return mp_const_none;
    }
    return mp_obj_new_str(sid, strlen(sid));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(cdn_session_id_obj, 0, 1, cdn_session_id);

static mp_obj_t cdn_fetch_pack(size_t n_args, const mp_obj_t *args) {
    const char *name;
    const char *ver = NULL;
    uint8_t *buf = NULL;
    uint32_t n = 0;
    char err[80];
    mp_obj_t out;
    cdn_ensure();
    name = mp_obj_str_get_str(args[0]);
    if (n_args >= 2 && args[1] != mp_const_none) {
        ver = mp_obj_str_get_str(args[1]);
    }
    if (pm_wasmmod_net_cdn_fetch_pack(name, ver, &buf, &n, err, sizeof(err)) != 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }
    out = mp_obj_new_bytes(buf, n);
    cdn_fetch_free(buf);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(cdn_fetch_pack_obj, 1, 2, cdn_fetch_pack);

static mp_obj_t cdn_fetch_index(size_t n_args, const mp_obj_t *args) {
    const char *channel = "lead";
    uint8_t *buf = NULL;
    uint32_t n = 0;
    char err[80];
    mp_obj_t out;
    cdn_ensure();
    if (n_args >= 1 && args[0] != mp_const_none) {
        channel = mp_obj_str_get_str(args[0]);
    }
    if (pm_wasmmod_net_cdn_fetch_index(channel, &buf, &n, err, sizeof(err)) != 0) {
        mp_raise_msg_varg(&mp_type_OSError, MP_ERROR_TEXT("%s"), err);
    }
    out = mp_obj_new_bytes(buf, n);
    cdn_fetch_free(buf);
    return out;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(cdn_fetch_index_obj, 0, 1, cdn_fetch_index);

static mp_obj_t cdn_driver_name(void) {
    const char *name;
    cdn_ensure();
    name = pm_wasmmod_net_cdn_driver_name();
    return mp_obj_new_str(name, strlen(name));
}
static MP_DEFINE_CONST_FUN_OBJ_0(cdn_driver_name_obj, cdn_driver_name);

static mp_obj_t cdn_base(void) {
    const char *b;
    cdn_ensure();
    b = pm_wasmmod_net_cdn_base();
    if (b == NULL || b[0] == '\0') {
        return mp_const_none;
    }
    return mp_obj_new_str(b, strlen(b));
}
static MP_DEFINE_CONST_FUN_OBJ_0(cdn_base_obj, cdn_base);

static const mp_rom_map_elem_t cdn_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod_dot_net_dot_cdn) },
    { MP_ROM_QSTR(MP_QSTR_configure), MP_ROM_PTR(&cdn_configure_obj) },
    { MP_ROM_QSTR(MP_QSTR_prepend), MP_ROM_PTR(&cdn_prepend_obj) },
    { MP_ROM_QSTR(MP_QSTR_reset), MP_ROM_PTR(&cdn_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_session_id), MP_ROM_PTR(&cdn_session_id_obj) },
    { MP_ROM_QSTR(MP_QSTR_fetch_pack), MP_ROM_PTR(&cdn_fetch_pack_obj) },
    { MP_ROM_QSTR(MP_QSTR_fetch_index), MP_ROM_PTR(&cdn_fetch_index_obj) },
    { MP_ROM_QSTR(MP_QSTR_driver_name), MP_ROM_PTR(&cdn_driver_name_obj) },
    { MP_ROM_QSTR(MP_QSTR_base), MP_ROM_PTR(&cdn_base_obj) },
};
static MP_DEFINE_CONST_DICT(cdn_globals, cdn_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod_net_cdn = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&cdn_globals,
};

static const mp_rom_map_elem_t net_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod_dot_net) },
    { MP_ROM_QSTR(MP_QSTR_cdn), MP_ROM_PTR(&mp_module_pymergetic_wasmmod_net_cdn) },
};
static MP_DEFINE_CONST_DICT(net_globals, net_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod_net = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&net_globals,
};

/* Seats without modwasmmod.c take pymergetic.wasmmod from here instead — same
 * name, smaller face. */
#if !MICROPY_PY_WASM_FULL
static mp_obj_t wasmmod___init__(void) {
    mp_wasm_ensure_inited();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(wasmmod___init___obj, wasmmod___init__);

static const mp_rom_map_elem_t wasmmod_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_wasmmod) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&wasmmod___init___obj) },
    { MP_ROM_QSTR(MP_QSTR_modules), MP_ROM_PTR(&mod_wasm_modules_obj) },
    { MP_ROM_QSTR(MP_QSTR_guest), MP_ROM_PTR(&mp_module_pymergetic_wasmmod_guest) },
    { MP_ROM_QSTR(MP_QSTR_net), MP_ROM_PTR(&mp_module_pymergetic_wasmmod_net) },
};
static MP_DEFINE_CONST_DICT(wasmmod_globals, wasmmod_globals_table);

const mp_obj_module_t mp_module_pymergetic_wasmmod = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&wasmmod_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod, mp_module_pymergetic_wasmmod);
#endif
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod_dot_net, mp_module_pymergetic_wasmmod_net);
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_wasmmod_dot_net_dot_cdn, mp_module_pymergetic_wasmmod_net_cdn);
