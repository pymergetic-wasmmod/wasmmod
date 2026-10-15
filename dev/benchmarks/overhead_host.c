#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/loader/__exports__.h"

typedef uint32_t (*native_fn)(uint32_t, uint32_t);
extern uint32_t overhead_mix(uint32_t, uint32_t);
extern native_fn overhead_native_pointer(void);
static native_fn native_cached;
static volatile uint64_t live_checksum;
static const char *names[] = {"native_direct", "native_cached", "interp_cached",
    "interp_lookup", "aot_cached", "aot_lookup"};
static const uint32_t works[] = {0, 1, 64, 4096};
static const uint8_t export_name[] = "overhead_mix";
static const uint8_t *fqns[] = {(const uint8_t *)"bench.overhead.interp",
    (const uint8_t *)"bench.overhead.aot"};
static pm_wasmmod_registry_fn_t cached[2];
static pm_wasmmod_registry_handle_t handles[2];
static uint8_t *images[2];
static uint32_t image_sizes[2];

static void fail(const char *msg) {
    fprintf(stderr, "overhead benchmark FAILED: %s\n", msg);
    exit(1);
}
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) fail("clock_gettime");
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static uint32_t fqn_len(unsigned backend) {
    return (uint32_t)strlen((const char *)fqns[backend]);
}
static void load(unsigned b) {
    handles[b] = pm_wasmmod_loader_load(fqns[b], fqn_len(b), images[b], image_sizes[b]);
    if (handles[b].index == UINT32_MAX) fail(b ? "AOT load/instantiate/register" : "Wasm load/instantiate/register");
}
static void unload(unsigned b) {
    if (pm_wasmmod_loader_unload(handles[b])) fail("unload");
}
static void resolve(unsigned b) {
    cached[b] = (pm_wasmmod_registry_fn_t)pm_wasmmod_registry_resolve_native(
        fqns[b], fqn_len(b), export_name, sizeof(export_name) - 1);
    if (!cached[b]) fail("resolve trampoline");
}
static uint32_t call_one(unsigned mode, uint32_t seed, uint32_t work) {
    if (mode == 0) return overhead_mix(seed, work);
    if (mode == 1) return native_cached(seed, work);
    unsigned b = (mode - 2) / 2;
    pm_wasmmod_registry_value_t args[2] = {
        pm_wasmmod_registry_value_i32((int32_t)seed),
        pm_wasmmod_registry_value_i32((int32_t)work)};
    pm_wasmmod_registry_value_t out = pm_wasmmod_registry_value_i32(0);
    int32_t rc = mode % 2 ? pm_wasmmod_registry_call(fqns[b], fqn_len(b),
        export_name, sizeof(export_name) - 1, args, 2, &out, 1)
        : cached[b](args, 2, &out, 1);
    if (rc || out.kind != PM_WASMMOD_REGISTRY_VALKIND_I32) fail("call status/result type");
    return (uint32_t)out.of.i32;
}

/* Dispatch is outside the timed loop. All modes use identical dependency and
 * checksum bookkeeping. Registry modes include argument/result marshaling and
 * status checks. No empty-loop subtraction, and no volatile load per call. */
static uint64_t run(unsigned mode, uint32_t work, uint64_t n, uint64_t *elapsed) {
    uint32_t state = UINT32_C(0x12345678);
    uint64_t sum = 0;
    uint64_t start, end;
#define STEP(value) do { uint32_t v = (value); sum += v; state = v + UINT32_C(0x9e3779b9); } while (0)
    if (mode == 0) {
        start = now_ns();
        for (uint64_t i = 0; i < n; ++i) STEP(overhead_mix(state, work));
        end = now_ns();
    } else if (mode == 1) {
        native_fn fn = native_cached;
        start = now_ns();
        for (uint64_t i = 0; i < n; ++i) STEP(fn(state, work));
        end = now_ns();
    } else {
        unsigned b = (mode - 2) / 2;
        pm_wasmmod_registry_fn_t fn = cached[b];
        const uint8_t *fqn = fqns[b];
        uint32_t len = fqn_len(b);
        pm_wasmmod_registry_value_t args[2] = {
            pm_wasmmod_registry_value_i32(0), pm_wasmmod_registry_value_i32((int32_t)work)};
        pm_wasmmod_registry_value_t out = pm_wasmmod_registry_value_i32(0);
        if (mode % 2) {
            start = now_ns();
            for (uint64_t i = 0; i < n; ++i) {
                args[0].of.i32 = (int32_t)state;
                if (pm_wasmmod_registry_call(fqn, len, export_name,
                        sizeof(export_name) - 1, args, 2, &out, 1)) fail("lookup timed call");
                STEP((uint32_t)out.of.i32);
            }
            end = now_ns();
        } else {
            start = now_ns();
            for (uint64_t i = 0; i < n; ++i) {
                args[0].of.i32 = (int32_t)state;
                if (fn(args, 2, &out, 1)) fail("cached timed call");
                STEP((uint32_t)out.of.i32);
            }
            end = now_ns();
        }
    }
#undef STEP
    *elapsed = end - start;
    uint64_t checksum = sum ^ ((uint64_t)state << 32);
    live_checksum ^= checksum;
    return checksum;
}
static void read_image(unsigned b, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) fail("open image");
    long n = ftell(f);
    if (n <= 0 || (unsigned long)n > UINT32_MAX || fseek(f, 0, SEEK_SET)) fail("image size");
    images[b] = malloc((size_t)n);
    if (!images[b] || fread(images[b], 1, (size_t)n, f) != (size_t)n || fclose(f)) fail("read image");
    image_sizes[b] = (uint32_t)n;
}
int main(int argc, char **argv) {
    if (argc != 4) fail("usage: overhead_host workload.wasm workload.aot sample_ms");
    char *end;
    errno = 0;
    double ms = strtod(argv[3], &end);
    if (errno || *end || !(ms >= 30 && ms <= 50)) fail("sample_ms must be 30..50");
    uint64_t target = (uint64_t)(ms * 1000000.0);
    read_image(0, argv[1]); read_image(1, argv[2]);
    if (pm_wasmmod_loader_init()) fail("loader init");
    native_cached = overhead_native_pointer();
    /* Init, file I/O and a first load/unload are deliberately not load samples. */
    for (unsigned b = 0; b < 2; ++b) { load(b); resolve(b); (void)call_one(2 + 2*b, 7, 64); unload(b); }
    for (unsigned s = 0; s < 7; ++s) {
        for (unsigned j = 0; j < 2; ++j) {
            unsigned b = (s+j)%2;
            uint64_t start = now_ns(); load(b); uint64_t ns = now_ns()-start;
            printf("BENCH_JSON {\"type\":\"load\",\"backend\":\"%s\",\"sample\":%u,\"elapsed_ns\":%" PRIu64 "}\n", b ? "aot" : "interp", s, ns);
            unload(b);
        }
    }
    for (unsigned b = 0; b < 2; ++b) { load(b); resolve(b); }
    /* Exact cross-backend values, outside timing, including adversarial words. */
    const uint32_t seeds[] = {0, 1, UINT32_MAX, UINT32_C(0x80000000), UINT32_C(0x12345678)};
    for (unsigned w = 0; w < 4; ++w)
        for (unsigned k = 0; k < sizeof(seeds)/sizeof(*seeds); ++k)
            for (unsigned mode = 1; mode < 6; ++mode)
                if (call_one(mode, seeds[k], works[w]) != overhead_mix(seeds[k], works[w])) fail("exact output mismatch");
    uint64_t iterations[24], expected[24];
    for (unsigned c = 0; c < 24; ++c) {
        unsigned mode = c % 6, w = c / 6;
        uint64_t ns, n = 32;
        (void)run(mode, works[w], n, &ns);
        /* Geometric calibration avoids trusting sub-millisecond observations. */
        do { (void)run(mode, works[w], n, &ns); if (ns < target/4) n *= 2; }
        while (ns < target/4);
        n = (uint64_t)((double)n * (double)target / (double)ns);
        if (!n) n = 1;
        iterations[c] = n;
        expected[c] = run(0, works[w], n, &ns);
        if (run(mode, works[w], n, &ns) != expected[c]) fail("warm-up checksum mismatch");
    }
    /* Coprime permutation plus rotation interleaves modes and work sizes. */
    for (unsigned s = 0; s < 7; ++s) {
        for (unsigned j = 0; j < 24; ++j) {
            unsigned c = (j * 7 + s * 5) % 24;
            unsigned mode = c % 6, w = c / 6;
            uint64_t ns, checksum = run(mode, works[w], iterations[c], &ns);
            if (checksum != expected[c]) fail("sample live checksum mismatch");
            printf("BENCH_JSON {\"type\":\"call\",\"mode\":\"%s\",\"work\":%u,\"sample\":%u,\"order\":%u,\"iterations\":%" PRIu64 ",\"elapsed_ns\":%" PRIu64 ",\"checksum\":%" PRIu64 "}\n",
                names[mode], works[w], s, j, iterations[c], ns, checksum);
        }
    }
    for (unsigned b = 0; b < 2; ++b) { unload(b); free(images[b]); }
    printf("BENCH_JSON {\"type\":\"success\",\"live_checksum\":%" PRIu64 "}\n", live_checksum);
    return 0;
}
