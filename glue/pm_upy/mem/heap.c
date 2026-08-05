/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/mem/heap.h"
#include "alloc.h"
void *pm_upy_alloc(size_t size) { return MICROPY_WASM_MALLOC(size); }
void pm_upy_free(void *ptr) { MICROPY_WASM_FREE(ptr); }
void *pm_upy_realloc(void *ptr, size_t size) {
    /* ports may lack realloc helper — alloc+copy not done here */
    (void)ptr; (void)size;
    return NULL;
}

