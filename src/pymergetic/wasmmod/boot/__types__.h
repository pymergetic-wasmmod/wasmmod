/* pymergetic.wasmmod.boot — boot + bootdep records (linker sections). */
#ifndef PYMERGETIC_WASMMOD_BOOT_TYPES_H
#define PYMERGETIC_WASMMOD_BOOT_TYPES_H

#include <stdint.h>

#include "pymergetic/util/mem/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*pm_mod_boot_init_fn)(pm_util_mem_arena_t *arena);
typedef void (*pm_mod_boot_deinit_fn)(void);
typedef int32_t (*pm_mod_boot_ready_fn)(void);

enum {
    PM_MOD_BOOTDEP_HARD = 0u,
    PM_MOD_BOOTDEP_CHILD = 1u,
};

typedef struct pm_mod_boot {
    const char *fqn;
    pm_mod_boot_init_fn init;
    pm_mod_boot_deinit_fn deinit;
    pm_mod_boot_ready_fn ready;
} pm_mod_boot_t;

typedef struct pm_mod_bootdep {
    const char *fqn;
    const char *dep;
    uint32_t flags;
} pm_mod_bootdep_t;

int32_t pm_mod_boot_add(const pm_mod_boot_t *rec);
int32_t pm_mod_bootdep_add(const pm_mod_bootdep_t *rec);
const pm_mod_boot_t *pm_mod_boot_current(void);
uint32_t pm_mod_boot_count(void);
const char *pm_mod_boot_fqn(uint32_t i);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_BOOT_TYPES_H */
