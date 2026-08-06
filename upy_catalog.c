/*
 * Minimal micropython.* host catalog.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/upy_catalog.h"

#include <stdlib.h>
#include <string.h>

#include "extmod/wasmmod/host.h"
#include "extmod/wasmmod/runtime.h"
#include "pm_common.h"
#include "pm_upy/exec/run.h"
#include "pm_upy/features.h"
#include "pm_upy/hal/time.h"
#include "pm_upy/loop/sched.h"
#include "pm_upy/loop/step.h"
#include "pm_upy/mem/gc.h"
#include "py/obj.h"
#include "wasm_export.h"

static int upy_catalog_registered;

// WAMR: () -> i32
static int32_t upy_ticks_ms_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)pm_upy_ticks_ms();
}

static int32_t upy_ticks_us_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)pm_upy_ticks_us();
}

static int64_t upy_time_ns_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int64_t)pm_upy_time_ns();
}

static void upy_delay_ms_wasm(wasm_exec_env_t exec_env, int32_t ms) {
    (void)exec_env;
    if (ms < 0) {
        return;
    }
    pm_upy_delay_ms((uint32_t)ms);
}

static int32_t upy_features_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)pm_upy_features();
}

static int32_t upy_has_wasm(wasm_exec_env_t exec_env, int32_t feat) {
    (void)exec_env;
    return pm_upy_has((pm_upy_feat_t)feat) ? 1 : 0;
}

static int32_t upy_gc_collect_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)pm_upy_gc_collect();
}

static int32_t upy_handle_pending_wasm(wasm_exec_env_t exec_env) {
    (void)exec_env;
    return (int32_t)pm_upy_handle_pending();
}

// Guest linear [off,len] → host C string → pm_upy_run_str.
static int32_t upy_run_str_wasm(wasm_exec_env_t exec_env, int32_t off, int32_t len) {
    if (len < 0) {
        return PM_ERR_ARG;
    }
    if (len == 0) {
        return pm_upy_run_str("");
    }
    void *linear = NULL;
    if (!mp_wasm_linear_from_exec(exec_env, (uint32_t)off, (uint32_t)len, &linear) || linear == NULL) {
        return PM_ERR_ARG;
    }
    char *buf = (char *)malloc((size_t)len + 1);
    if (buf == NULL) {
        return PM_ERR_NOMEM;
    }
    memcpy(buf, linear, (size_t)len);
    buf[len] = '\0';
    int st = pm_upy_run_str(buf);
    free(buf);
    return (int32_t)st;
}

// fun/arg are host object handles (0 = none).
static int32_t upy_sched_schedule_wasm(wasm_exec_env_t exec_env, int32_t fun_h, int32_t arg_h) {
    (void)exec_env;
    mp_obj_t fun = mp_wasm_handle_resolve(fun_h);
    if (fun == mp_const_none) {
        return PM_ERR_ARG;
    }
    mp_obj_t arg = (arg_h == 0) ? mp_const_none : mp_wasm_handle_resolve(arg_h);
    return (int32_t)pm_upy_sched_schedule((void *)(uintptr_t)fun, (void *)(uintptr_t)arg);
}

static NativeSymbol upy_runtime_symbols[] = {
    { "ticks_ms", (void *)upy_ticks_ms_wasm, "()i", NULL },
    { "ticks_us", (void *)upy_ticks_us_wasm, "()i", NULL },
    { "time_ns", (void *)upy_time_ns_wasm, "()I", NULL },
    { "delay_ms", (void *)upy_delay_ms_wasm, "(i)", NULL },
    { "features", (void *)upy_features_wasm, "()i", NULL },
    { "has", (void *)upy_has_wasm, "(i)i", NULL },
    { "gc_collect", (void *)upy_gc_collect_wasm, "()i", NULL },
    { "handle_pending", (void *)upy_handle_pending_wasm, "()i", NULL },
    { "run_str", (void *)upy_run_str_wasm, "(ii)i", NULL },
    { "sched_schedule", (void *)upy_sched_schedule_wasm, "(ii)i", NULL },
};

bool mp_wasm_upy_catalog_register(void) {
    if (upy_catalog_registered) {
        return true;
    }
    if (!wasm_runtime_register_natives("micropython.runtime", upy_runtime_symbols,
            sizeof(upy_runtime_symbols) / sizeof(upy_runtime_symbols[0]))) {
        return false;
    }
    upy_catalog_registered = 1;
    return true;
}

#if MICROPY_PY_WASM_ELF

static uint32_t upy_ticks_ms_elf(void) {
    return pm_upy_ticks_ms();
}

static uint32_t upy_ticks_us_elf(void) {
    return pm_upy_ticks_us();
}

static uint64_t upy_time_ns_elf(void) {
    return pm_upy_time_ns();
}

static void upy_delay_ms_elf(uint32_t ms) {
    pm_upy_delay_ms(ms);
}

static uint32_t upy_features_elf(void) {
    return pm_upy_features();
}

static int32_t upy_has_elf(int32_t feat) {
    return pm_upy_has((pm_upy_feat_t)feat) ? 1 : 0;
}

static int32_t upy_gc_collect_elf(void) {
    return (int32_t)pm_upy_gc_collect();
}

static int32_t upy_handle_pending_elf(void) {
    return (int32_t)pm_upy_handle_pending();
}

static int32_t upy_run_str_elf(const char *src) {
    return (int32_t)pm_upy_run_str(src);
}

static int32_t upy_sched_schedule_elf(void *fun, void *arg) {
    return (int32_t)pm_upy_sched_schedule(fun, arg);
}

typedef struct {
    const char *module;
    const char *func;
    void *addr;
} upy_elf_slot_t;

static const upy_elf_slot_t upy_elf_slots[] = {
    { "micropython.runtime", "ticks_ms", (void *)upy_ticks_ms_elf },
    { "micropython.runtime", "ticks_us", (void *)upy_ticks_us_elf },
    { "micropython.runtime", "time_ns", (void *)upy_time_ns_elf },
    { "micropython.runtime", "delay_ms", (void *)upy_delay_ms_elf },
    { "micropython.runtime", "features", (void *)upy_features_elf },
    { "micropython.runtime", "has", (void *)upy_has_elf },
    { "micropython.runtime", "gc_collect", (void *)upy_gc_collect_elf },
    { "micropython.runtime", "handle_pending", (void *)upy_handle_pending_elf },
    { "micropython.runtime", "run_str", (void *)upy_run_str_elf },
    { "micropython.runtime", "sched_schedule", (void *)upy_sched_schedule_elf },
};

void *mp_wasm_upy_catalog_elf_lookup(const char *module, const char *func) {
    if (module == NULL || func == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(upy_elf_slots) / sizeof(upy_elf_slots[0]); ++i) {
        if (strcmp(module, upy_elf_slots[i].module) == 0
            && strcmp(func, upy_elf_slots[i].func) == 0) {
            return upy_elf_slots[i].addr;
        }
    }
    return NULL;
}

#endif // MICROPY_PY_WASM_ELF

#endif // MICROPY_PY_WASM
