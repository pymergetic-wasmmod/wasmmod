/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/exc.h"
#include "py/runtime.h"

void pm_upy_raise_msg(const char *type_name, const char *msg) {
    (void)type_name;
    mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("%s"), msg ? msg : "error");
}

void pm_upy_raise_feature(const char *api_name) {
    mp_raise_msg_varg(&mp_type_NotImplementedError,
        MP_ERROR_TEXT("feature unavailable: %s"), api_name ? api_name : "?");
}
