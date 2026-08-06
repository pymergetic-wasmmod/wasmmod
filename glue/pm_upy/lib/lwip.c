/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/lib/lwip.h"
#include "pm_common.h"
#include "py/mpconfig.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_LWIP
#define MICROPY_PY_LWIP 0
#endif
#ifndef MICROPY_PY_SOCKET
#define MICROPY_PY_SOCKET 0
#endif

#if MICROPY_PY_LWIP
#include "extmod/modnetwork.h"
#endif

int pm_upy_lwip_available(void) {
    return MICROPY_PY_LWIP ? 1 : 0;
}

int pm_upy_lwip_init(void) {
#if MICROPY_PY_LWIP
    mod_network_lwip_init();
    return PM_OK;
#else
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_lwip_poll(uint32_t ticks_ms) {
#if MICROPY_PY_LWIP
    mod_network_lwip_poll_wrapper(ticks_ms);
    return PM_OK;
#else
    (void)ticks_ms;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_lwip_gethostbyname(const char *host, uint8_t ip[4]) {
#if MICROPY_PY_SOCKET
    if (!host || !ip) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(MP_QSTR_socket, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t getaddrinfo = mp_load_attr(mod, MP_QSTR_getaddrinfo);
        mp_obj_t args[2] = {
            mp_obj_new_str(host, strlen(host)),
            MP_OBJ_NEW_SMALL_INT(0),
        };
        mp_obj_t infos = mp_call_function_n_kw(getaddrinfo, 2, 0, args);
        mp_obj_t first = mp_obj_subscr(infos, MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_SENTINEL);
        mp_obj_t sockaddr = mp_obj_subscr(first, MP_OBJ_NEW_SMALL_INT(4), MP_OBJ_SENTINEL);
        mp_obj_t addr = mp_obj_subscr(sockaddr, MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_SENTINEL);
        size_t len = 0;
        const char *s = mp_obj_str_get_data(addr, &len);
        /* IPv4 dotted form: a.b.c.d */
        unsigned parts[4] = {0, 0, 0, 0};
        size_t pi = 0;
        for (size_t i = 0; i < len && pi < 4; i++) {
            char ch = s[i];
            if (ch >= '0' && ch <= '9') {
                parts[pi] = parts[pi] * 10u + (unsigned)(ch - '0');
                if (parts[pi] > 255u) {
                    nlr_pop();
                    return PM_ERR;
                }
            } else if (ch == '.' && pi < 3) {
                pi++;
            } else {
                nlr_pop();
                return PM_ERR;
            }
        }
        if (pi != 3) {
            nlr_pop();
            return PM_ERR;
        }
        ip[0] = (uint8_t)parts[0];
        ip[1] = (uint8_t)parts[1];
        ip[2] = (uint8_t)parts[2];
        ip[3] = (uint8_t)parts[3];
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)host;
    (void)ip;
    return PM_ERR_FEATURE;
#endif
}
