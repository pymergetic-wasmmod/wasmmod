/* pymergetic.wasmmod.boot — walk PM_MOD_BOOT_* records in dep order. */
#include "pymergetic/wasmmod/boot/__types__.h"

#include <stdint.h>
#include <string.h>

#ifndef PM_MOD_BOOT_MAX
#define PM_MOD_BOOT_MAX 64u
#endif

#ifndef PM_MOD_BOOTDEP_MAX
#define PM_MOD_BOOTDEP_MAX 128u
#endif

extern const pm_mod_boot_t __start_pm_mod_boot[] __attribute__((weak));
extern const pm_mod_boot_t __stop_pm_mod_boot[] __attribute__((weak));
extern const pm_mod_bootdep_t __start_pm_mod_bootdep[] __attribute__((weak));
extern const pm_mod_bootdep_t __stop_pm_mod_bootdep[] __attribute__((weak));

static const pm_mod_boot_t *s_order[PM_MOD_BOOT_MAX];
static const pm_mod_boot_t *s_extra[PM_MOD_BOOT_MAX];
static const pm_mod_bootdep_t *s_depextra[PM_MOD_BOOTDEP_MAX];
static uint32_t s_n;
static uint32_t s_nextra;
static uint32_t s_ndepextra;
static uint32_t s_ready;
static pm_util_mem_arena_t *s_arena;
static const pm_mod_boot_t *s_cur;

const pm_mod_boot_t *pm_mod_boot_current(void) {
    return s_cur;
}

static int32_t call_init(const pm_mod_boot_t *rec, pm_util_mem_arena_t *arena) {
    const pm_mod_boot_t *prev = s_cur;
    int32_t st;
    s_cur = rec;
    st = rec->init(arena);
    s_cur = prev;
    return st;
}

static int32_t call_ready(const pm_mod_boot_t *rec) {
    const pm_mod_boot_t *prev = s_cur;
    int32_t st;
    s_cur = rec;
    st = rec->ready();
    s_cur = prev;
    return st;
}

static void call_deinit(const pm_mod_boot_t *rec) {
    const pm_mod_boot_t *prev = s_cur;
    s_cur = rec;
    rec->deinit();
    s_cur = prev;
}

static int fqn_eq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

static int32_t find_boot_ptr(const pm_mod_boot_t *const *boots, uint32_t n, const char *fqn) {
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (boots[i] != NULL && fqn_eq(boots[i]->fqn, fqn)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static uint32_t collect_section_boot(const pm_mod_boot_t **out, uint32_t cap) {
#if defined(__wasm__)
    /* Custom ELF sections are not a C array in wasm linear memory; constructors
     * already called pm_mod_boot_add. Macros still emit the sections. */
    (void)out;
    (void)cap;
    return 0;
#else
    const pm_mod_boot_t *recs;
    uint32_t n;
    uint32_t i;
    uint32_t w = 0;
    recs = __start_pm_mod_boot;
    if ((uintptr_t)(const void *)recs == 0 || (uintptr_t)(const void *)__stop_pm_mod_boot == 0) {
        return 0;
    }
    n = (uint32_t)(__stop_pm_mod_boot - __start_pm_mod_boot);
    for (i = 0; i < n && w < cap; i++) {
        if (recs[i].fqn == NULL || recs[i].init == NULL) {
            continue;
        }
        out[w++] = &recs[i];
    }
    return w;
#endif
}

static uint32_t collect_section_dep(const pm_mod_bootdep_t **out, uint32_t cap) {
#if defined(__wasm__)
    (void)out;
    (void)cap;
    return 0;
#else
    const pm_mod_bootdep_t *recs;
    uint32_t n;
    uint32_t i;
    uint32_t w = 0;
    recs = __start_pm_mod_bootdep;
    if ((uintptr_t)(const void *)recs == 0 || (uintptr_t)(const void *)__stop_pm_mod_bootdep == 0) {
        return 0;
    }
    n = (uint32_t)(__stop_pm_mod_bootdep - __start_pm_mod_bootdep);
    for (i = 0; i < n && w < cap; i++) {
        if (recs[i].fqn == NULL || recs[i].dep == NULL) {
            continue;
        }
        out[w++] = &recs[i];
    }
    return w;
#endif
}

int32_t pm_mod_boot_add(const pm_mod_boot_t *rec) {
    uint32_t i;
    if (rec == NULL || rec->fqn == NULL || rec->init == NULL) {
        return -1;
    }
    for (i = 0; i < s_nextra; i++) {
        if (fqn_eq(s_extra[i]->fqn, rec->fqn)) {
            return 0;
        }
    }
    if (s_nextra >= PM_MOD_BOOT_MAX) {
        return -1;
    }
    s_extra[s_nextra++] = rec;
    if (s_ready) {
        if (s_n >= PM_MOD_BOOT_MAX) {
            return -1;
        }
        if (call_init(rec, s_arena) != 0) {
            return -1;
        }
        if (rec->ready != NULL && call_ready(rec) != 0) {
            if (rec->deinit != NULL) {
                call_deinit(rec);
            }
            return -1;
        }
        s_order[s_n++] = rec;
    }
    return 0;
}

int32_t pm_mod_bootdep_add(const pm_mod_bootdep_t *rec) {
    uint32_t i;
    if (rec == NULL || rec->fqn == NULL || rec->dep == NULL) {
        return -1;
    }
    for (i = 0; i < s_ndepextra; i++) {
        if (fqn_eq(s_depextra[i]->fqn, rec->fqn) && fqn_eq(s_depextra[i]->dep, rec->dep)
            && s_depextra[i]->flags == rec->flags) {
            return 0;
        }
    }
    if (s_ndepextra >= PM_MOD_BOOTDEP_MAX) {
        return -1;
    }
    s_depextra[s_ndepextra++] = rec;
    return 0;
}

void pm_mod_boot_unwind(void) {
    uint32_t i;
    if (!s_ready && s_n == 0) {
        s_nextra = 0;
        s_ndepextra = 0;
        s_arena = NULL;
        return;
    }
    i = s_n;
    while (i > 0) {
        i--;
        if (s_order[i] != NULL && s_order[i]->deinit != NULL) {
            call_deinit(s_order[i]);
        }
        s_order[i] = NULL;
    }
    s_n = 0;
    s_ready = 0;
    s_arena = NULL;
}

uint32_t pm_mod_boot_count(void) {
    return s_n;
}

const char *pm_mod_boot_fqn(uint32_t i) {
    if (i >= s_n || s_order[i] == NULL) {
        return NULL;
    }
    return s_order[i]->fqn;
}

int32_t pm_mod_boot_run(pm_util_mem_arena_t *arena) {
    const pm_mod_boot_t *boots[PM_MOD_BOOT_MAX];
    const pm_mod_bootdep_t *deps[PM_MOD_BOOTDEP_MAX];
    uint32_t nboot;
    uint32_t ndep;
    uint32_t indeg[PM_MOD_BOOT_MAX];
    uint8_t edge[PM_MOD_BOOT_MAX][PM_MOD_BOOT_MAX];
    uint32_t q[PM_MOD_BOOT_MAX];
    uint32_t qh;
    uint32_t qt;
    uint32_t i;
    uint32_t j;
    uint32_t k;

    if (s_ready) {
        return 0;
    }
    if (arena == NULL) {
        return -1;
    }
    s_n = 0;
    memset(s_order, 0, sizeof(s_order));
    memset(indeg, 0, sizeof(indeg));
    memset(edge, 0, sizeof(edge));
    memset(boots, 0, sizeof(boots));
    memset(deps, 0, sizeof(deps));

    nboot = collect_section_boot(boots, PM_MOD_BOOT_MAX);
    for (i = 0; i < s_nextra && nboot < PM_MOD_BOOT_MAX; i++) {
        if (find_boot_ptr(boots, nboot, s_extra[i]->fqn) >= 0) {
            continue;
        }
        boots[nboot++] = s_extra[i];
    }
    if (nboot == 0 || nboot > PM_MOD_BOOT_MAX) {
        return -1;
    }
    for (i = 0; i < nboot; i++) {
        if (boots[i] == NULL || boots[i]->fqn == NULL || boots[i]->init == NULL) {
            return -1;
        }
        if (find_boot_ptr(boots, i, boots[i]->fqn) >= 0) {
            return -1;
        }
    }

    ndep = collect_section_dep(deps, PM_MOD_BOOTDEP_MAX);
    for (i = 0; i < s_ndepextra && ndep < PM_MOD_BOOTDEP_MAX; i++) {
        uint32_t j;
        int seen = 0;
        for (j = 0; j < ndep; j++) {
            if (deps[j] == s_depextra[i]) {
                seen = 1;
                break;
            }
        }
        if (!seen) {
            deps[ndep++] = s_depextra[i];
        }
    }

    for (k = 0; k < ndep; k++) {
        int32_t src;
        int32_t dst;
        uint32_t optional;
        if (deps[k] == NULL || deps[k]->fqn == NULL || deps[k]->dep == NULL) {
            continue;
        }
        optional = deps[k]->flags == PM_MOD_BOOTDEP_CHILD;
        src = find_boot_ptr(boots, nboot, deps[k]->fqn);
        dst = find_boot_ptr(boots, nboot, deps[k]->dep);
        if (src < 0) {
            continue;
        }
        if (dst < 0) {
            if (optional) {
                continue;
            }
            return -1;
        }
        if (src == dst) {
            return -1;
        }
        if (!edge[dst][src]) {
            edge[dst][src] = 1;
            indeg[src]++;
        }
    }

    qh = 0;
    qt = 0;
    for (i = 0; i < nboot; i++) {
        if (indeg[i] == 0) {
            q[qt++] = i;
        }
    }
    while (qh < qt) {
        i = q[qh++];
        s_order[s_n++] = boots[i];
        for (j = 0; j < nboot; j++) {
            if (edge[i][j]) {
                indeg[j]--;
                if (indeg[j] == 0) {
                    q[qt++] = j;
                }
            }
        }
    }
    if (s_n != nboot) {
        s_n = 0;
        return -1;
    }

    s_arena = arena;
    for (i = 0; i < s_n; i++) {
        if (call_init(s_order[i], arena) != 0) {
            uint32_t done = i;
            s_n = done;
            pm_mod_boot_unwind();
            return -1;
        }
        if (s_order[i]->ready != NULL && call_ready(s_order[i]) != 0) {
            uint32_t done = i + 1u;
            s_n = done;
            pm_mod_boot_unwind();
            return -1;
        }
    }
    s_ready = 1;
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.wasmmod.boot, pm_mod_boot_run, pm_mod_boot_run, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.boot, pm_mod_boot_unwind, pm_mod_boot_unwind, void(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.boot, pm_mod_boot_add, pm_mod_boot_add, int32_t(const pm_mod_boot_t *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.boot, pm_mod_bootdep_add, pm_mod_bootdep_add, int32_t(const pm_mod_bootdep_t *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.boot, pm_mod_boot_count, pm_mod_boot_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.boot, pm_mod_boot_fqn, pm_mod_boot_fqn, const char *(uint32_t));
