/* pymergetic.util.limits — the knobs a seat grows against.
 *
 * A card that holds things — sockets, connections, routes — used to say how
 * many in a header, reserve that many at link time, and drop whatever came
 * after. Two costs: a seat carries the reservation whether it serves one
 * connection or none, and the number is a wall you find by losing a request.
 *
 * Here a count is a knob: a soft value the card grows to as it goes, a hard one
 * it may never pass, and the default it shipped with. Software moves the soft
 * while running; it can only ever lower the hard. Zero, in either, means no
 * ceiling — grow until the arena says no.
 *
 * The value lives in the card that uses it, as a plain struct read straight off
 * the hot path. This card holds no table of its own: the knobs are threaded on
 * a list through themselves, so the thing that gives out sizes reserves
 * nothing, needs no arena, and is whole before main runs. It is the place a
 * seat looks, not the place the number lives.
 *
 * It sits in util, beside the arena it hands cards, rather than in metal: a
 * seat without metal has capacities too (the module registry, the loader), and
 * they are the same kind of number. One card, both trees.
 */
#include "pymergetic/util/limits/__types__.h"
/* The arena the grow helper hands a card. The type comes from mem's own
 * header; the one call into it is declared here, the way every C card in this
 * tree reaches another's muscle before the faces are generated (this TU is
 * compiled by the crate's build script, which runs first). */
#include "pymergetic/util/mem/__types__.h"

#include <stddef.h>
#include <string.h>

void *pm_util_mem_realloc(pm_util_mem_arena_t *arena, void *ptr, size_t size);

static pm_util_limit_t *s_head;

static pm_util_limit_t *knob_at(int32_t i) {
    pm_util_limit_t *k = s_head;
    int32_t n = 0;
    if (i < 0) {
        return NULL;
    }
    while (k != NULL && n < i) {
        k = k->next;
        n++;
    }
    return k;
}

static pm_util_limit_t *knob_named(const char *name) {
    pm_util_limit_t *k;
    if (name == NULL) {
        return NULL;
    }
    for (k = s_head; k != NULL; k = k->next) {
        if (strcmp(k->name, name) == 0) {
            return k;
        }
    }
    return NULL;
}

int32_t pm_util_limits_attach(pm_util_limit_t *knob) {
    pm_util_limit_t **slot;
    if (knob == NULL || knob->name == NULL || knob->next != NULL) {
        return -1;
    }
    /* A knob without a module is a knob nobody can ask a card for. */
    if (knob->module == NULL || knob->leaf == NULL) {
        return -1;
    }
    if (knob_named(knob->name) != NULL) {
        return -1; /* one card, one knob of that name */
    }
    /* Sorted by name, so the list reads the way a listing should and an index
     * means the same thing on every boot — constructors run in whatever order
     * the linker felt like. */
    slot = &s_head;
    while (*slot != NULL && strcmp((*slot)->name, knob->name) < 0) {
        slot = &(*slot)->next;
    }
    knob->next = *slot;
    *slot = knob;
    return 0;
}

int32_t pm_util_limits_detach(pm_util_limit_t *knob) {
    pm_util_limit_t **slot;
    if (knob == NULL) {
        return -1;
    }
    for (slot = &s_head; *slot != NULL; slot = &(*slot)->next) {
        if (*slot == knob) {
            *slot = knob->next;
            knob->next = NULL;
            return 0;
        }
    }
    return -1;
}

uint32_t pm_util_limits_count(void) {
    uint32_t n = 0;
    pm_util_limit_t *k;
    for (k = s_head; k != NULL; k = k->next) {
        n++;
    }
    return n;
}

const char *pm_util_limits_next(const char *after) {
    pm_util_limit_t *k;
    if (after == NULL || after[0] == '\0') {
        return s_head != NULL ? s_head->name : NULL;
    }
    k = knob_named(after);
    if (k == NULL || k->next == NULL) {
        return NULL;
    }
    return k->next->name;
}

int32_t pm_util_limits_find(const char *name) {
    pm_util_limit_t *k;
    int32_t i = 0;
    if (name == NULL) {
        return -1;
    }
    for (k = s_head; k != NULL; k = k->next) {
        if (strcmp(k->name, name) == 0) {
            return i;
        }
        i++;
    }
    return -1;
}

const char *pm_util_limits_name(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return k != NULL ? k->name : NULL;
}

const char *pm_util_limits_module(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return k != NULL ? k->module : NULL;
}

const char *pm_util_limits_leaf(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return k != NULL ? k->leaf : NULL;
}

/* Is this knob the module's own, or one from a card below it? `deep` is what
 * separates "the knobs of net.ip" from "the knobs under drivers": the dot check
 * is there so `drivers.net` never claims `drivers.network`. */
static int32_t knob_in(const pm_util_limit_t *k, const char *module, int32_t deep) {
    size_t n;
    if (module == NULL || k->module == NULL) {
        return 0;
    }
    n = strlen(module);
    if (strncmp(k->module, module, n) != 0) {
        return 0;
    }
    if (k->module[n] == '\0') {
        return 1;
    }
    return (deep != 0 && k->module[n] == '.') ? 1 : 0;
}

static uint32_t count_in(const char *module, int32_t deep) {
    pm_util_limit_t *k;
    uint32_t n = 0;
    for (k = s_head; k != NULL; k = k->next) {
        if (knob_in(k, module, deep)) {
            n++;
        }
    }
    return n;
}

/* The nth knob of a module, answered as the index the readers take: the list is
 * sorted by name, so a card's knobs come out in one run and in the same order
 * on every boot. */
static int32_t nth_in(const char *module, int32_t nth, int32_t deep) {
    pm_util_limit_t *k;
    int32_t i = 0;
    int32_t seen = 0;
    if (nth < 0) {
        return -1;
    }
    for (k = s_head; k != NULL; k = k->next) {
        if (knob_in(k, module, deep)) {
            if (seen == nth) {
                return i;
            }
            seen++;
        }
        i++;
    }
    return -1;
}

uint32_t pm_util_limits_count_of(const char *module) {
    return count_in(module, 0);
}

int32_t pm_util_limits_nth_of(const char *module, int32_t nth) {
    return nth_in(module, nth, 0);
}

uint32_t pm_util_limits_count_under(const char *module) {
    return count_in(module, 1);
}

int32_t pm_util_limits_nth_under(const char *module, int32_t nth) {
    return nth_in(module, nth, 1);
}

/* One knob of one card, asked for the way it was declared: the module and the
 * leaf, not the two glued back together. */
int32_t pm_util_limits_find_of(const char *module, const char *leaf) {
    pm_util_limit_t *k;
    int32_t i = 0;
    if (module == NULL || leaf == NULL) {
        return -1;
    }
    for (k = s_head; k != NULL; k = k->next) {
        if (knob_in(k, module, 0) && k->leaf != NULL && strcmp(k->leaf, leaf) == 0) {
            return i;
        }
        i++;
    }
    return -1;
}

uint32_t pm_util_limits_soft(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return k != NULL ? k->soft : 0u;
}

uint32_t pm_util_limits_hard(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return k != NULL ? k->hard : 0u;
}

uint32_t pm_util_limits_default(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return k != NULL ? k->dflt : 0u;
}

uint32_t pm_util_limits_counted(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return (k != NULL && k->used != NULL) ? 1u : 0u;
}

uint32_t pm_util_limits_used(int32_t i) {
    pm_util_limit_t *k = knob_at(i);
    return (k != NULL && k->used != NULL) ? *k->used : 0u;
}

/* The new soft, and the card's chance to act on it. A card that refuses puts
 * the seat back where it was rather than leaving a knob that says one thing
 * and storage that does another. */
static int32_t soft_moved(pm_util_limit_t *k, uint32_t was) {
    if (k->apply == NULL || k->soft == was) {
        return 0;
    }
    if (k->apply(k) != 0) {
        k->soft = was;
        return -3;
    }
    return 0;
}

/* The three moves, once, on a knob the caller already has. Whether it was found
 * by name or by index changes nothing about what may happen to it. */
static int32_t knob_set(pm_util_limit_t *k, uint32_t soft) {
    uint32_t was;
    if (k == NULL) {
        return -1;
    }
    /* Zero is "no ceiling", which is more than any finite hard allows. */
    if (k->hard != 0u && (soft == 0u || soft > k->hard)) {
        return -2;
    }
    was = k->soft;
    k->soft = soft;
    return soft_moved(k, was);
}

static int32_t knob_tighten(pm_util_limit_t *k, uint32_t hard) {
    uint32_t was;
    if (k == NULL) {
        return -1;
    }
    /* One way only. A seat may decide it will never need more than this, and
     * nothing it runs later gets to change its mind — that is the whole point
     * of there being two numbers rather than one. */
    if (hard == 0u || (k->hard != 0u && hard > k->hard)) {
        return -2;
    }
    k->hard = hard;
    was = k->soft;
    if (k->soft == 0u || k->soft > hard) {
        k->soft = hard;
    }
    return soft_moved(k, was);
}

static int32_t knob_reset(pm_util_limit_t *k) {
    uint32_t was;
    if (k == NULL) {
        return -1;
    }
    /* Back to the build's own number — unless the seat has since tightened
     * below it, in which case the tighter one stands. */
    was = k->soft;
    k->soft = (k->hard != 0u && k->dflt > k->hard) ? k->hard : k->dflt;
    return soft_moved(k, was);
}

int32_t pm_util_limits_set(const char *name, pm_mod_value_t soft) {
    return knob_set(knob_named(name), soft);
}

int32_t pm_util_limits_tighten(const char *name, pm_mod_value_t hard) {
    return knob_tighten(knob_named(name), hard);
}

int32_t pm_util_limits_reset(const char *name) {
    return knob_reset(knob_named(name));
}

int32_t pm_util_limits_set_at(int32_t i, pm_mod_value_t soft) {
    return knob_set(knob_at(i), soft);
}

int32_t pm_util_limits_tighten_at(int32_t i, pm_mod_value_t hard) {
    return knob_tighten(knob_at(i), hard);
}

int32_t pm_util_limits_reset_at(int32_t i) {
    return knob_reset(knob_at(i));
}

/* Is there anything to ask about? The list is threaded through the knobs
 * themselves and every one of them puts itself on before main, so a seat with
 * cards answers 1 the moment it starts — there is no init to wait for and no
 * arena to hand over. A seat whose cards declare no capacity at all answers 0,
 * which is the honest reading of "nothing to configure here". */
uint32_t pm_util_limits_ready(void) {
    return s_head != NULL ? 1u : 0u;
}

void *pm_util_limits_grow(pm_util_mem_arena_t *arena, void *base, uint32_t *cap, uint32_t item,
    const pm_util_limit_t *knob) {
    uint32_t have;
    uint32_t want;
    void *next;
    if (arena == NULL || cap == NULL || item == 0u || knob == NULL) {
        return NULL;
    }
    have = *cap;
    if (!PM_UTIL_LIMIT_ROOM(*knob, have)) {
        return NULL;
    }
    /* Double, because a table that had to grow once usually grows again, and
     * eight to begin with, because most of these hold a handful of things.
     * Never past what the knob allows — asking for one more must not quietly
     * take room for eight. */
    want = have == 0u ? 8u : have * 2u;
    if (knob->soft != 0u && want > knob->soft) {
        want = knob->soft;
    }
    if (want <= have) {
        return NULL;
    }
    next = pm_util_mem_realloc(arena, base, (size_t)want * (size_t)item);
    if (next == NULL) {
        return NULL;
    }
    /* Fresh room reads as free: every caller takes an all-zero entry to mean
     * nothing is there yet. */
    memset((uint8_t *)next + (size_t)have * (size_t)item, 0, (size_t)(want - have) * (size_t)item);
    *cap = want;
    return next;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_attach, pm_util_limits_attach, int32_t(pm_util_limit_t *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_detach, pm_util_limits_detach, int32_t(pm_util_limit_t *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_count, pm_util_limits_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_next, pm_util_limits_next, const char *(const char *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_find, pm_util_limits_find, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_name, pm_util_limits_name, const char *(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_module, pm_util_limits_module, const char *(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_leaf, pm_util_limits_leaf, const char *(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_count_of, pm_util_limits_count_of, uint32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_nth_of, pm_util_limits_nth_of, int32_t(const char *, int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_count_under, pm_util_limits_count_under, uint32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_nth_under, pm_util_limits_nth_under, int32_t(const char *, int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_find_of, pm_util_limits_find_of, int32_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_soft, pm_util_limits_soft, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_hard, pm_util_limits_hard, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_default, pm_util_limits_default, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_counted, pm_util_limits_counted, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_used, pm_util_limits_used, uint32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_set, pm_util_limits_set, int32_t(const char *, pm_mod_value_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_tighten, pm_util_limits_tighten, int32_t(const char *, pm_mod_value_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_reset, pm_util_limits_reset, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_set_at, pm_util_limits_set_at, int32_t(int32_t, pm_mod_value_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_tighten_at, pm_util_limits_tighten_at, int32_t(int32_t, pm_mod_value_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_reset_at, pm_util_limits_reset_at, int32_t(int32_t));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_ready, pm_util_limits_ready, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.util.limits, pm_util_limits_grow, pm_util_limits_grow, void *(pm_util_mem_arena_t *, void *, uint32_t *, uint32_t, const pm_util_limit_t *));
