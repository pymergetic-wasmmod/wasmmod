#include "ports/common/boot.h"

#include <string.h>

#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/registry/__exports__.h"
#include "pymergetic/wasmmod/registry/__types__.h"
#include "pymergetic/wasmmod/verify/__exports__.h"

static int g_booted;

__attribute__((weak)) int pm_metal_boot(void) {
    return 0;
}

int pm_wasmmod_host_boot(const char *kernel_fqn, const char *version) {
    if (g_booted) {
        return 0;
    }
    if (kernel_fqn == NULL || kernel_fqn[0] == '\0') {
        kernel_fqn = "pymergetic.wasmmod";
    }
    if (version == NULL) {
        version = "";
    }
    pm_wasmmod_registry_init();
    if (!pm_wasmmod_io_is_set()) {
        pm_wasmmod_io_set(NULL); /* builtin DECLINE → file / optional POSIX HTTP */
    }
    if (pm_wasmmod_loader_init() != 0) {
        return -1;
    }
    size_t klen = strlen(kernel_fqn);
    (void)pm_wasmmod_registry_ensure((const uint8_t *)kernel_fqn, (uint32_t)klen,
        PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT);
    if (version[0] != '\0') {
        (void)pm_wasmmod_registry_set_version((const uint8_t *)kernel_fqn, (uint32_t)klen,
            (const uint8_t *)version, (uint32_t)strlen(version));
    }
    mp_wasm_trust_init_session();
    g_booted = 1;
    return 0;
}

void pm_wasmmod_host_presence_publish(const char *name) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    size_t n = strlen(name);
    if (pm_wasmmod_registry_has((const uint8_t *)name, (uint32_t)n)) {
        return;
    }
    (void)pm_wasmmod_registry_publish((const uint8_t *)name, (uint32_t)n,
        PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT);
}
