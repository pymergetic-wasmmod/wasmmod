/*
 * framebuf.FrameBuffer; machine.* when MICROPY_PY_MACHINE is built.
 */

#include "pm_upy/lib/hw.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_FRAMEBUF
#define MICROPY_PY_FRAMEBUF 0
#endif
#ifndef MICROPY_PY_MACHINE
#define MICROPY_PY_MACHINE 0
#endif

pm_upy_obj_t pm_upy_framebuf_new(pm_upy_obj_t buf, int w, int h, int fmt) {
#if MICROPY_PY_FRAMEBUF
    if (!buf) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(MP_QSTR_framebuf, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t cls = mp_load_attr(mod, MP_QSTR_FrameBuffer);
        mp_obj_t args[4] = {
            (mp_obj_t)(uintptr_t)buf,
            MP_OBJ_NEW_SMALL_INT(w),
            MP_OBJ_NEW_SMALL_INT(h),
            MP_OBJ_NEW_SMALL_INT(fmt),
        };
        mp_obj_t out = mp_call_function_n_kw(cls, 4, 0, args);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)buf;
    (void)w;
    (void)h;
    (void)fmt;
    return (pm_upy_obj_t)0;
#endif
}

static pm_upy_obj_t machine_ctor(const char *name, int id) {
#if MICROPY_PY_MACHINE
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(MP_QSTR_machine, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t cls = mp_load_attr(mod, qstr_from_str(name));
        mp_obj_t out = mp_call_function_1(cls, MP_OBJ_NEW_SMALL_INT(id));
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)name;
    (void)id;
    return (pm_upy_obj_t)0;
#endif
}

pm_upy_obj_t pm_upy_machine_pin(int pin) {
    return machine_ctor("Pin", pin);
}

pm_upy_obj_t pm_upy_machine_i2c(int id) {
    return machine_ctor("I2C", id);
}

pm_upy_obj_t pm_upy_machine_spi(int id) {
    return machine_ctor("SPI", id);
}

pm_upy_obj_t pm_upy_machine_uart(int id) {
    return machine_ctor("UART", id);
}
