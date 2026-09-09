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

typedef struct __attribute__((aligned(8))) pm_mod_bootdep {
    const char *fqn;
    const char *dep;
    /* The struct is aligned(8) so sizeof == the linker's section stride —
     * guest.h and guest.rs emit records aligned(8), and the collector
     * counts (stop - start)/sizeof(record). On ELF32 the natural layout
     * is sizeof 12 against a 16-byte section stride, misreading every
     * record past the first as garbage (empty dep strings, phantom
     * deps). aligned(8) makes sizeof 16 there and 24 on ELF64 — the
     * stride on both classes, matching the Rust mirror's
     * #[repr(C, align(8))]. Values are the enum above; only 0/1 ever
     * stored. */
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
