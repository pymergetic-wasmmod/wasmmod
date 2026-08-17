/* pymergetic.wasmmod.net.cdn — artifact CDN client protocol (not a net stack).
 *
 * Wait class: fetch_pack / fetch_index / publish = async (via io.fetch /
 * io.request). configure / add / reset / url_is_base = sync.
 */
#ifndef PYMERGETIC_WASMMOD_NET_CDN_TYPES_H
#define PYMERGETIC_WASMMOD_NET_CDN_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_WASMMOD_NET_CDN_BASES_MAX
#define PM_WASMMOD_NET_CDN_BASES_MAX (4u)
#endif

typedef enum {
    PM_WASMMOD_NET_CDN_DRIVER_PATH = 0,  /* no artifact base configured */
    PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS = 1, /* artifacts/lead|pin on configured bases */
} pm_wasmmod_net_cdn_driver_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_NET_CDN_TYPES_H */
