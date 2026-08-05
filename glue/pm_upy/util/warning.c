/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/util/warning.h"
#include "py/runtime.h"
void pm_upy_warning(const char *msg) { mp_warning(NULL, "%s", msg ? msg : ""); }

