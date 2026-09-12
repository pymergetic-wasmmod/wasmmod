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
    /* Who the knob belongs to and what it counts, and the two of them joined.
     * A knob is a thing under a module, the way an export is: `module` is the
     * card's own fqn, `leaf` is what it counts there, and `name` is
     * "<module>.<leaf>" — so a seat can ask for one knob by its full name, or
     * for every knob of a card, without taking a name apart. All three are
     * literals the declaration built at compile time; nothing here splits a
     * string at runtime. */
    const char *name;   /* "pymergetic.metal.net.ip.socket" */
    const char *module; /* "pymergetic.metal.net.ip" */
    const char *leaf;   /* "socket" */
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

/* The module path and the leaf, as written — dotted tokens, the same way
 * PM_MOD_EXPORT_C takes an fqn. Two steps because a macro argument has to be
 * expanded before it is stringified. */
#define PM_UTIL_LIMIT_STR_(x) #x
#define PM_UTIL_LIMIT_STR(x) PM_UTIL_LIMIT_STR_(x)

/* Declare a card's knob and put it on the list. The card owns the variable and
 * reads `knob.soft` wherever it grows; nothing here is in that path.
 *
 *   PM_UTIL_LIMIT_C(pm_ip_limit_socket, pymergetic.metal.net.ip, socket,
 *       32u, 0u, &pm_ip_socket_used);
 *
 * The module is the card's own fqn and the leaf is what it counts there, both
 * unquoted: the knob is a thing under that module, and writing the two apart
 * is what lets a seat ask a card for its knobs. Name it after the count, not
 * after the storage — `socket`, `connection`, `device`, `frame`.
 *
 * Not static: a card whose muscle spans translation units (net.ip does) needs
 * one knob both halves can see, declared extern in its private header.
 */
#define PM_UTIL_LIMIT_C(knob, mod, leaf, soft_dflt, hard_dflt, used_ptr) \
    PM_UTIL_LIMIT_APPLY_C(knob, mod, leaf, soft_dflt, hard_dflt, used_ptr, NULL)

/* The same, for a card that has to act when the knob moves rather than at its
 * next growth: `apply_fn` runs in the setter's context. */
#define PM_UTIL_LIMIT_APPLY_C(knob, mod, leaf, soft_dflt, hard_dflt, used_ptr, apply_fn) \
    pm_util_limit_t knob = { PM_UTIL_LIMIT_STR(mod) "." PM_UTIL_LIMIT_STR(leaf), \
        PM_UTIL_LIMIT_STR(mod), PM_UTIL_LIMIT_STR(leaf), \
        (soft_dflt), (hard_dflt), (soft_dflt), (used_ptr), NULL, (apply_fn) }; \
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
const char *pm_util_limits_module(int32_t i);
const char *pm_util_limits_leaf(int32_t i);
/* The knobs of one card: `count_of`/`nth_of` take the module exactly,
 * `count_under`/`nth_under` take it and everything below it, so a branch of the
 * tree answers as a branch. The nth forms give back the index the readers take,
 * or -1 once the nth is past the end. */
uint32_t pm_util_limits_count_of(const char *module);
int32_t pm_util_limits_nth_of(const char *module, int32_t nth);
uint32_t pm_util_limits_count_under(const char *module);
int32_t pm_util_limits_nth_under(const char *module, int32_t nth);
int32_t pm_util_limits_find_of(const char *module, const char *leaf);
uint32_t pm_util_limits_soft(int32_t i);
uint32_t pm_util_limits_hard(int32_t i);
uint32_t pm_util_limits_default(int32_t i);
uint32_t pm_util_limits_counted(int32_t i);
uint32_t pm_util_limits_used(int32_t i);
int32_t pm_util_limits_set(const char *name, pm_mod_value_t soft);
int32_t pm_util_limits_tighten(const char *name, pm_mod_value_t hard);
int32_t pm_util_limits_reset(const char *name);
/* The same three by index, for a caller that already has one — from `find`,
 * `find_of`, `nth_of` or `nth_under`. The readers have always been by index; these are
 * the writers to match, so a face that walked a card's knobs can move one
 * without building its name back up. */
int32_t pm_util_limits_set_at(int32_t i, pm_mod_value_t soft);
int32_t pm_util_limits_tighten_at(int32_t i, pm_mod_value_t hard);
int32_t pm_util_limits_reset_at(int32_t i);

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
