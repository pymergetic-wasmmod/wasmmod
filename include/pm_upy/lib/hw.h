/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LIB_HW_H_
#define PM_PM_UPY_LIB_HW_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_hw_available(void);

#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_machine_pin(int pin);
pm_upy_obj_t pm_upy_machine_i2c(int id);
pm_upy_obj_t pm_upy_machine_spi(int id);
pm_upy_obj_t pm_upy_machine_uart(int id);
pm_upy_obj_t pm_upy_framebuf_new(pm_upy_obj_t buf, int w, int h, int fmt);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_HW_H_ */
