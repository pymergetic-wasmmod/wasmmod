/* pymergetic.util.limits — a knob moves between its two ends, and nowhere
 * else. The scratch knob below is attached for the length of the prove and
 * taken off again, so a test never leaves a name on a seat's listing. */
#include "pymergetic/util/limits.h"
#include "pymergetic/wasmmod/guest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "util.limits test: %s\n", why);
    return 1;
}

static uint32_t s_probe_used;
static pm_util_limit_t s_probe = { "zzz.prove.thing", 8u, 32u, 8u, &s_probe_used, NULL, NULL };
static pm_util_limit_t s_open = { "zzz.prove.open", 4u, 0u, 4u, NULL, NULL, NULL };

static int32_t case_room(void) {
    pm_util_limit_t k = { "zzz.room", 2u, 0u, 2u, NULL, NULL, NULL };
    if (!PM_UTIL_LIMIT_ROOM(k, 0u) || !PM_UTIL_LIMIT_ROOM(k, 1u)) {
        return fail("a knob refused below its own soft");
    }
    if (PM_UTIL_LIMIT_ROOM(k, 2u) || PM_UTIL_LIMIT_ROOM(k, 9u)) {
        return fail("a knob let a card past its soft");
    }
    k.soft = 0u;
    if (!PM_UTIL_LIMIT_ROOM(k, 1000000u)) {
        return fail("zero is meant to be no ceiling at all");
    }
    return 0;
}

static int32_t case_knob(void) {
    int32_t i;
    const char *n;

    if (pm_util_limits_attach(&s_probe) != 0 || pm_util_limits_attach(&s_open) != 0) {
        return fail("attach");
    }
    if (pm_util_limits_attach(&s_probe) == 0) {
        return fail("the same knob went on the list twice");
    }
    i = pm_util_limits_find("zzz.prove.thing");
    if (i < 0 || pm_util_limits_soft(i) != 8u || pm_util_limits_hard(i) != 32u
        || pm_util_limits_default(i) != 8u) {
        return fail("a knob does not read back the way it was declared");
    }
    if (pm_util_limits_counted(i) != 1u || pm_util_limits_used(i) != 0u) {
        return fail("in-use count");
    }
    s_probe_used = 5u;
    if (pm_util_limits_used(i) != 5u) {
        return fail("the listing does not follow the card's own count");
    }
    if (pm_util_limits_counted(pm_util_limits_find("zzz.prove.open")) != 0u) {
        return fail("a knob with nothing to count claimed a count");
    }

    /* Software moves the soft, inside the hard. */
    if (pm_util_limits_set("zzz.prove.thing", 20u) != 0 || pm_util_limits_soft(i) != 20u) {
        return fail("set within the hard");
    }
    if (pm_util_limits_set("zzz.prove.thing", 33u) != -2 || pm_util_limits_soft(i) != 20u) {
        return fail("a soft was allowed past its hard");
    }
    if (pm_util_limits_set("zzz.prove.thing", 0u) != -2) {
        return fail("unlimited was allowed under a hard that is not");
    }
    if (pm_util_limits_set("zzz.nobody", 1u) != -1) {
        return fail("a name nobody registered was accepted");
    }
    /* A knob with no hard takes anything, unlimited included. */
    if (pm_util_limits_set("zzz.prove.open", 4096u) != 0
        || pm_util_limits_set("zzz.prove.open", 0u) != 0
        || pm_util_limits_soft(pm_util_limits_find("zzz.prove.open")) != 0u) {
        return fail("a knob with no hard refused");
    }

    /* The hard only ever comes down, and brings a soft above it along. */
    if (pm_util_limits_tighten("zzz.prove.thing", 64u) != -2) {
        return fail("a hard was raised");
    }
    if (pm_util_limits_tighten("zzz.prove.thing", 0u) != -2) {
        return fail("a hard was thrown away");
    }
    if (pm_util_limits_tighten("zzz.prove.thing", 12u) != 0
        || pm_util_limits_hard(i) != 12u || pm_util_limits_soft(i) != 12u) {
        return fail("tighten");
    }
    /* Back to the build's number, but not back through a tightened ceiling. */
    if (pm_util_limits_reset("zzz.prove.thing") != 0 || pm_util_limits_soft(i) != 8u) {
        return fail("reset");
    }
    if (pm_util_limits_tighten("zzz.prove.thing", 4u) != 0
        || pm_util_limits_reset("zzz.prove.thing") != 0
        || pm_util_limits_soft(i) != 4u) {
        return fail("reset climbed back over a tightened hard");
    }

    /* The listing walks every knob, in one order, name by name. */
    n = pm_util_limits_next("");
    if (n == NULL) {
        return fail("the listing starts nowhere");
    }
    {
        uint32_t walked = 0;
        const char *prev = NULL;
        while (n != NULL) {
            if (prev != NULL && strcmp(prev, n) >= 0) {
                return fail("the listing is not in one order");
            }
            prev = n;
            walked++;
            n = pm_util_limits_next(n);
        }
        if (walked != pm_util_limits_count()) {
            return fail("the walk and the count disagree");
        }
    }
    if (pm_util_limits_next("zzz.nobody") != NULL) {
        return fail("a name nobody registered walked somewhere");
    }
    return 0;
}

/* The apply hook: a card whose storage has to be reshaped when the knob moves
 * is told, in the context of whoever moved it, and gets to refuse. */
static uint32_t s_applied;
static uint32_t s_apply_saw;
static int s_apply_refuse;

static int32_t probe_apply(pm_util_limit_t *knob) {
    s_applied++;
    s_apply_saw = knob->soft;
    return s_apply_refuse ? -1 : 0;
}

static int32_t case_apply(void) {
    static pm_util_limit_t hooked = { "zzz.prove.hooked", 8u, 64u, 8u, NULL, NULL, probe_apply };
    int32_t i;
    if (pm_util_limits_attach(&hooked) != 0) {
        return fail("attach the hooked knob");
    }
    i = pm_util_limits_find("zzz.prove.hooked");
    s_applied = 0;
    if (pm_util_limits_set("zzz.prove.hooked", 16u) != 0 || s_applied != 1u
        || s_apply_saw != 16u || pm_util_limits_soft(i) != 16u) {
        (void)pm_util_limits_detach(&hooked);
        return fail("the card was not told its knob moved");
    }
    /* The same value again is not a move. */
    if (pm_util_limits_set("zzz.prove.hooked", 16u) != 0 || s_applied != 1u) {
        (void)pm_util_limits_detach(&hooked);
        return fail("a knob that did not move told the card it had");
    }
    /* A card that cannot do it leaves the seat as it was. */
    s_apply_refuse = 1;
    if (pm_util_limits_set("zzz.prove.hooked", 32u) != -3
        || pm_util_limits_soft(i) != 16u) {
        s_apply_refuse = 0;
        (void)pm_util_limits_detach(&hooked);
        return fail("a refused apply still moved the knob");
    }
    s_apply_refuse = 0;
    if (pm_util_limits_reset("zzz.prove.hooked") != 0 || s_apply_saw != 8u) {
        (void)pm_util_limits_detach(&hooked);
        return fail("reset did not reach the card");
    }
    if (pm_util_limits_detach(&hooked) != 0) {
        return fail("detach the hooked knob");
    }
    return 0;
}

static int32_t pm_util_limits_tests(void) {
    int32_t st = case_room();
    uint32_t before = pm_util_limits_count();
    if (st == 0) {
        st = case_knob();
    }
    if (st == 0) {
        st = case_apply();
    }
    (void)pm_util_limits_detach(&s_probe);
    (void)pm_util_limits_detach(&s_open);
    if (st == 0 && pm_util_limits_count() != before) {
        return fail("the prove left its own knobs on the seat");
    }
    return st;
}

PM_MOD_TEST_C(pymergetic.util.limits, tests, pm_util_limits_tests);
