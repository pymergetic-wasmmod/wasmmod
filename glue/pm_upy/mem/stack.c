/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/mem/stack.h"
#include "py/stackctrl.h"
void pm_upy_stack_ctrl_init(void) { /* port sets stack top elsewhere */ }
void pm_upy_stack_check(void) { mp_stack_check(); }

