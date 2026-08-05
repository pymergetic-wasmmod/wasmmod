/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/hal/stdio.h"
#include "py/mphal.h"
int pm_upy_stdin_rx(void) { return mp_hal_stdin_rx_chr(); }
void pm_upy_stdout_tx(const char *s, size_t len) { mp_hal_stdout_tx_strn(s, len); }
int pm_upy_stdio_poll(uintptr_t poll) { (void)poll; return 0; }

