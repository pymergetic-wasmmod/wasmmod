/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/init.h"
#include "pm_common.h"

static int ready;

int pm_upy_init(void *heap_start, size_t heap_len) {
    (void)heap_start;
    (void)heap_len;
    ready = 1;
    return PM_OK;
}

void pm_upy_deinit(void) {
    ready = 0;
}

int pm_upy_ready(void) {
    return ready ? 1 : 0;
}
