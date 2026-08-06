/*
 * NIC control methods on a network interface object.
 */

#include "pm_upy/lib/network.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_NETWORK
#define MICROPY_PY_NETWORK 0
#endif

pm_upy_obj_t pm_upy_network_ifconfig(pm_upy_obj_t nic) {
#if MICROPY_PY_NETWORK
    if (!nic) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)nic, qstr_from_str("ifconfig"));
        mp_obj_t out = mp_call_function_0(fn);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)nic;
    return (pm_upy_obj_t)0;
#endif
}

int pm_upy_network_active(pm_upy_obj_t nic, int on) {
#if MICROPY_PY_NETWORK
    if (!nic) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)nic, qstr_from_str("active"));
        mp_call_function_1(fn, on ? mp_const_true : mp_const_false);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)nic;
    (void)on;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_network_connect(pm_upy_obj_t nic, const char *ssid, const char *key) {
#if MICROPY_PY_NETWORK
    if (!nic || !ssid) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)nic, MP_QSTR_connect);
        mp_obj_t args[2] = {
            mp_obj_new_str(ssid, strlen(ssid)),
            mp_obj_new_str(key ? key : "", key ? strlen(key) : 0),
        };
        mp_call_function_n_kw(fn, 2, 0, args);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)nic;
    (void)ssid;
    (void)key;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_network_status(pm_upy_obj_t nic) {
#if MICROPY_PY_NETWORK
    if (!nic) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fn = mp_load_attr((mp_obj_t)(uintptr_t)nic, qstr_from_str("status"));
        mp_obj_t out = mp_call_function_0(fn);
        int st = (int)mp_obj_get_int(out);
        nlr_pop();
        return st;
    }
    return PM_ERR;
#else
    (void)nic;
    return PM_ERR_FEATURE;
#endif
}
