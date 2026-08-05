/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/host/slots.h"
#include "host.h"
#include <stdlib.h>

#define PM_WASM_C_SLOTS 64
typedef struct {
    int32_t (*fn)(int32_t);
    void *userdata;
    int used;
} pm_c_slot_t;
static pm_c_slot_t c_slots[PM_WASM_C_SLOTS];

void pm_wasmmod_host_clear_all(void) {
    mp_wasm_host_clear_all();
    for (int i = 0; i < PM_WASM_C_SLOTS; i++) {
        c_slots[i].fn = NULL;
        c_slots[i].userdata = NULL;
        c_slots[i].used = 0;
    }
}

bool pm_wasmmod_host_set_slot_c(int32_t slot, int32_t (*fn)(int32_t arg), void *userdata) {
    (void)userdata;
    if (slot < 0 || slot >= PM_WASM_C_SLOTS || fn == NULL) {
        return false;
    }
    c_slots[slot].fn = fn;
    c_slots[slot].userdata = userdata;
    c_slots[slot].used = 1;
    return true;
}

size_t pm_wasmmod_host_slot_count(void) {
    return mp_wasm_host_slot_count();
}

/* Used by future catalog to dispatch C slots; keep symbol for linker. */
int32_t pm_wasmmod_host_call_c_slot(int32_t slot, int32_t arg) {
    if (slot < 0 || slot >= PM_WASM_C_SLOTS || !c_slots[slot].used || !c_slots[slot].fn) {
        return 0;
    }
    return c_slots[slot].fn(arg);
}

