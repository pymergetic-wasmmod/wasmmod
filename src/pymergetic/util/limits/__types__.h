/* pymergetic.util.limits — how big a card may grow, and who says so. */
#ifndef PYMERGETIC_UTIL_LIMITS_TYPES_H
#define PYMERGETIC_UTIL_LIMITS_TYPES_H

#include <stdint.h>

/* pm_mod_value_t — what a knob is set to as it crosses a face. A number the
 * caller means, not the length of the name beside it. */
#include "pymergetic/wasmmod/guest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One knob.
 *
 * `soft` is how far the card will grow on its own: it allocates as it goes and
 * stops here. A seat moves it while running. `hard` is the ceiling soft may
 * never pass — a seat can only ever lower it. `dflt` is the soft this build
 * shipped with, so "put it back" needs no remembering.
 *
 * Zero means no ceiling in both: a soft of zero grows until the arena says no,
 * a hard of zero lets the soft go anywhere.
 *
 * `used` points at the card's own live count when it keeps one, so a reader can
 * see how close a knob is to biting. NULL when the card counts nothing.
 *
 * The struct is the card's, not this one's: it is a static in the card that
 * grows against it, read directly on the hot path (no call, no lookup), and
 * only linked here so a seat has one place to find them all. That is also why
 * this card reserves nothing itself — the list is threaded through the knobs.
 */
typedef struct pm_util_limit {
    const char *name; /* "net.ip.socket" — card path, then the thing counted */
    uint32_t soft;
    uint32_t hard;
    uint32_t dflt;
    const uint32_t *used;
    struct pm_util_limit *next;
    /* Called right after the soft moves, in the caller's own context, for a
     * card whose storage has to be reshaped rather than just grown next time
     * (the console's scrollback ring is the case: printing must never reach
     * the allocator, so the ring is resized here, by whoever moved the knob).
     * Returning non-zero puts the old value back. NULL for the usual card,
     * which simply reads the new soft the next time it grows. */
    int32_t (*apply)(struct pm_util_limit *knob);
} pm_util_limit_t;

/* Declare a card's knob and put it on the list. The card owns the variable and
 * reads `knob.soft` wherever it grows; nothing here is in that path.
 *
 *   PM_UTIL_LIMIT_C(pm_ip_limit_socket, "net.ip.socket", 32u, 0u, &pm_ip_socket_used);
 *
 * Not static: a card whose muscle spans translation units (net.ip does) needs
 * one knob both halves can see, declared extern in its private header.
 */
#define PM_UTIL_LIMIT_C(knob, knob_name, soft_dflt, hard_dflt, used_ptr) \
    PM_UTIL_LIMIT_APPLY_C(knob, knob_name, soft_dflt, hard_dflt, used_ptr, NULL)

/* The same, for a card that has to act when the knob moves rather than at its
 * next growth: `apply_fn` runs in the setter's context. */
#define PM_UTIL_LIMIT_APPLY_C(knob, knob_name, soft_dflt, hard_dflt, used_ptr, apply_fn) \
    pm_util_limit_t knob = { (knob_name), (soft_dflt), (hard_dflt), (soft_dflt), (used_ptr), \
        NULL, (apply_fn) }; \
    static void __attribute__((constructor)) pm_util_limit_reg_##knob(void) { \
        (void)pm_util_limits_attach(&knob); \
    }

/* Room for one more? `have` is what the caller holds now. A knob at zero never
 * refuses; past that the arena is the only other thing that can say no. */
#define PM_UTIL_LIMIT_ROOM(knob, have) ((knob).soft == 0u || (uint32_t)(have) < (knob).soft)

/* The card's own face, declared here as well as in the generated
 * __exports__.h: this module's muscle is compiled by the crate's build script,
 * which runs before the faces exist. Consumers include the umbrella
 * (pymergetic/util/limits.h), same as every other card. */
int32_t pm_util_limits_attach(pm_util_limit_t *knob);
int32_t pm_util_limits_detach(pm_util_limit_t *knob);
uint32_t pm_util_limits_count(void);
uint32_t pm_util_limits_ready(void);
const char *pm_util_limits_next(const char *after);
int32_t pm_util_limits_find(const char *name);
const char *pm_util_limits_name(int32_t i);
uint32_t pm_util_limits_soft(int32_t i);
uint32_t pm_util_limits_hard(int32_t i);
uint32_t pm_util_limits_default(int32_t i);
uint32_t pm_util_limits_counted(int32_t i);
uint32_t pm_util_limits_used(int32_t i);
int32_t pm_util_limits_set(const char *name, pm_mod_value_t soft);
int32_t pm_util_limits_tighten(const char *name, pm_mod_value_t hard);
int32_t pm_util_limits_reset(const char *name);

/* One more row in a table a card grows under a knob: doubles what the caller
 * holds (from eight), never past the knob's soft, zeroing the new tail so a
 * fresh row reads as free. NULL when the knob or the arena says no, leaving
 * the caller's table exactly as it was. */
struct pm_util_mem_arena;
void *pm_util_limits_grow(struct pm_util_mem_arena *arena, void *base, uint32_t *cap,
    uint32_t item, const pm_util_limit_t *knob);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_LIMITS_TYPES_H */
