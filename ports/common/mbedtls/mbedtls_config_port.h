/* Default-fill TLS for pymergetic.wasmmod.io (unix/CPython cargo).
 * Same shape as ports/unix/mbedtls: platform entropy, not bare-metal.
 * Metal / browser never compile this — they fill io_ops instead.
 */
#ifndef MICROPY_INCLUDED_MBEDTLS_CONFIG_H
#define MICROPY_INCLUDED_MBEDTLS_CONFIG_H

#ifndef MICROPY_PY_SSL_DTLS
#define MICROPY_PY_SSL_DTLS (0)
#endif
#ifndef MICROPY_MBEDTLS_CONFIG_BARE_METAL
#define MICROPY_MBEDTLS_CONFIG_BARE_METAL (0)
#endif

#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_TIMING_C

#include "extmod/mbedtls/mbedtls_config_common.h" // IWYU pragma: keep

#endif /* MICROPY_INCLUDED_MBEDTLS_CONFIG_H */
