/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * mbedtls config for MicroPython ports/webassembly + wasmmod signature verify.
 * Same shape as ports/unix (platform entropy; not bare-metal).
 */
#ifndef MICROPY_INCLUDED_MBEDTLS_CONFIG_H
#define MICROPY_INCLUDED_MBEDTLS_CONFIG_H

#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_TIMING_C

// Side-effect include: mbedtls feature macros (no symbols to "use").
#include "extmod/mbedtls/mbedtls_config_common.h" // IWYU pragma: keep

#endif /* MICROPY_INCLUDED_MBEDTLS_CONFIG_H */
