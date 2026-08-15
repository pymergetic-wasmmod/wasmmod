#include "ports/common/memcookie.h"

#include <string.h>

typedef struct {
    const uint8_t *ptr;
    uint32_t len;
    int used;
} pm_wasmmod_mem_slot_t;

static pm_wasmmod_mem_slot_t g_mem_slots[PM_WASMMOD_MEM_COOKIE_SLOTS];

pm_wasmmod_mem_cookie_t pm_wasmmod_mem_cookie_put(const uint8_t *ptr, uint32_t len) {
    if (len > 0 && ptr == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < PM_WASMMOD_MEM_COOKIE_SLOTS; i++) {
        if (!g_mem_slots[i].used) {
            g_mem_slots[i].ptr = ptr;
            g_mem_slots[i].len = len;
            g_mem_slots[i].used = 1;
            return (pm_wasmmod_mem_cookie_t)(i + 1);
        }
    }
    return 0;
}

int pm_wasmmod_mem_cookie_get(pm_wasmmod_mem_cookie_t cookie, const uint8_t **ptr_out,
    uint32_t *len_out) {
    if (cookie <= 0 || (uint32_t)cookie > PM_WASMMOD_MEM_COOKIE_SLOTS) {
        return -1;
    }
    pm_wasmmod_mem_slot_t *s = &g_mem_slots[(uint32_t)cookie - 1];
    if (!s->used) {
        return -1;
    }
    if (ptr_out) {
        *ptr_out = s->ptr;
    }
    if (len_out) {
        *len_out = s->len;
    }
    return 0;
}

void pm_wasmmod_mem_cookie_release(pm_wasmmod_mem_cookie_t cookie) {
    if (cookie <= 0 || (uint32_t)cookie > PM_WASMMOD_MEM_COOKIE_SLOTS) {
        return;
    }
    pm_wasmmod_mem_slot_t *s = &g_mem_slots[(uint32_t)cookie - 1];
    s->ptr = NULL;
    s->len = 0;
    s->used = 0;
}
