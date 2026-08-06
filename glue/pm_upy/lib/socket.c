/*
 * socket.* via the builtin socket module when present.
 */

#include "pm_upy/lib/socket.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_SOCKET
#define MICROPY_PY_SOCKET 0
#endif

pm_upy_obj_t pm_upy_socket_create(int af, int type, int proto) {
#if MICROPY_PY_SOCKET
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(MP_QSTR_socket, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t cls = mp_load_attr(mod, MP_QSTR_socket);
        mp_obj_t args[3] = {
            MP_OBJ_NEW_SMALL_INT(af),
            MP_OBJ_NEW_SMALL_INT(type),
            MP_OBJ_NEW_SMALL_INT(proto),
        };
        mp_obj_t sock = mp_call_function_n_kw(cls, 3, 0, args);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)sock;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)af;
    (void)type;
    (void)proto;
    return (pm_upy_obj_t)0;
#endif
}

static int socket_call_addr(pm_upy_obj_t sock, qstr meth, const char *host, int port) {
#if MICROPY_PY_SOCKET
    if (!sock || !host) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)sock, meth);
        mp_obj_t items[2] = {
            mp_obj_new_str(host, strlen(host)),
            MP_OBJ_NEW_SMALL_INT(port),
        };
        mp_obj_t addr = mp_obj_new_tuple(2, items);
        mp_call_function_1(fn, addr);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)sock;
    (void)meth;
    (void)host;
    (void)port;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_socket_connect(pm_upy_obj_t sock, const char *host, int port) {
    return socket_call_addr(sock, MP_QSTR_connect, host, port);
}

int pm_upy_socket_bind(pm_upy_obj_t sock, const char *host, int port) {
    return socket_call_addr(sock, MP_QSTR_bind, host, port);
}

int pm_upy_socket_listen(pm_upy_obj_t sock, int backlog) {
#if MICROPY_PY_SOCKET
    if (!sock) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)sock, MP_QSTR_listen);
        mp_call_function_1(fn, MP_OBJ_NEW_SMALL_INT(backlog));
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)sock;
    (void)backlog;
    return PM_ERR_FEATURE;
#endif
}

pm_upy_obj_t pm_upy_socket_accept(pm_upy_obj_t sock) {
#if MICROPY_PY_SOCKET
    if (!sock) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)sock, MP_QSTR_accept);
        mp_obj_t out = mp_call_function_0(fn);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)sock;
    return (pm_upy_obj_t)0;
#endif
}

int pm_upy_socket_send(pm_upy_obj_t sock, const void *buf, size_t len) {
#if MICROPY_PY_SOCKET
    if (!sock || (!buf && len)) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)sock, MP_QSTR_send);
        mp_obj_t data = mp_obj_new_bytes(buf, len);
        mp_obj_t n = mp_call_function_1(fn, data);
        int sent = (int)mp_obj_get_int(n);
        nlr_pop();
        return sent;
    }
    return PM_ERR;
#else
    (void)sock;
    (void)buf;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_socket_recv(pm_upy_obj_t sock, void *buf, size_t len) {
#if MICROPY_PY_SOCKET
    if (!sock || !buf) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)sock, MP_QSTR_recv);
        mp_obj_t data = mp_call_function_1(fn, MP_OBJ_NEW_SMALL_INT((mp_int_t)len));
        mp_buffer_info_t info;
        mp_get_buffer_raise(data, &info, MP_BUFFER_READ);
        size_t n = info.len < len ? info.len : len;
        memcpy(buf, info.buf, n);
        nlr_pop();
        return (int)n;
    }
    return PM_ERR;
#else
    (void)sock;
    (void)buf;
    (void)len;
    return PM_ERR_FEATURE;
#endif
}
