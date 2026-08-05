/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/obj/qstr.h"
#include "py/qstr.h"
uint32_t pm_upy_qstr_from_str(const char *s) { return (uint32_t)qstr_from_str(s); }
const char *pm_upy_qstr_str(uint32_t q) { return qstr_str((qstr)q); }
size_t pm_upy_qstr_len(uint32_t q) { return qstr_len((qstr)q); }

