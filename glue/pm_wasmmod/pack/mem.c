/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/pack/mem.h"
#include "runtime.h"
bool pm_wasmmod_pack_linear(pm_wasmmod_pack_t *pack, uint32_t off, uint32_t n, void **out) {
    return mp_pack_linear((mp_pack_t *)pack, off, n, out);
}
bool pm_wasmmod_pack_mem_read(pm_wasmmod_pack_t *pack, uint32_t off, uint32_t n, void *dst) {
    return mp_pack_mem_read((mp_pack_t *)pack, off, n, dst);
}
bool pm_wasmmod_pack_mem_write(pm_wasmmod_pack_t *pack, uint32_t off, uint32_t n, const void *src) {
    return mp_pack_mem_write((mp_pack_t *)pack, off, n, src);
}
uint32_t pm_wasmmod_pack_mem_alloc(pm_wasmmod_pack_t *pack, uint32_t n, void **native_out) {
    return mp_pack_mem_alloc((mp_pack_t *)pack, n, native_out);
}
void pm_wasmmod_pack_mem_free(pm_wasmmod_pack_t *pack, uint32_t off) {
    mp_pack_mem_free((mp_pack_t *)pack, off);
}

