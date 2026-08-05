/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Honest feature probes for lib/util surfaces (config / module presence).
 */

#include "py/mpconfig.h"

#ifndef MICROPY_PY_UCTYPES
#define MICROPY_PY_UCTYPES 0
#endif
#ifndef MICROPY_PY_RE
#define MICROPY_PY_RE 0
#endif
#ifndef MICROPY_PY_JSON
#define MICROPY_PY_JSON 0
#endif
#ifndef MICROPY_PY_DEFLATE
#define MICROPY_PY_DEFLATE 0
#endif
#ifndef MICROPY_PY_SELECT
#define MICROPY_PY_SELECT 0
#endif
#ifndef MICROPY_PY_SOCKET
#define MICROPY_PY_SOCKET 0
#endif
#ifndef MICROPY_PY_NETWORK
#define MICROPY_PY_NETWORK 0
#endif
#ifndef MICROPY_PY_LWIP
#define MICROPY_PY_LWIP 0
#endif
#ifndef MICROPY_PY_BLUETOOTH
#define MICROPY_PY_BLUETOOTH 0
#endif
#ifndef MICROPY_PY_WEBSOCKET
#define MICROPY_PY_WEBSOCKET 0
#endif
#ifndef MICROPY_PY_ASYNCIO
#define MICROPY_PY_ASYNCIO 0
#endif
#ifndef MICROPY_PY_SSL
#define MICROPY_PY_SSL 0
#endif
#ifndef MICROPY_PY_MACHINE
#define MICROPY_PY_MACHINE 0
#endif
#ifndef MICROPY_LONGINT_IMPL
#define MICROPY_LONGINT_IMPL 0
#endif
#ifndef MICROPY_ENABLE_COMPILER
#define MICROPY_ENABLE_COMPILER 0
#endif
#ifndef MICROPY_PERSISTENT_CODE_LOAD
#define MICROPY_PERSISTENT_CODE_LOAD 0
#endif
#ifndef MICROPY_VFS
#define MICROPY_VFS 0
#endif
#ifndef MICROPY_PY_SYS_STDFILES
#define MICROPY_PY_SYS_STDFILES 0
#endif
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL 0
#endif

int pm_upy_compile_available(void) {
    return MICROPY_ENABLE_COMPILER ? 1 : 0;
}

int pm_upy_reader_available(void) {
    return 1;
}

int pm_upy_dynruntime_available(void) {
#ifdef MICROPY_ENABLE_DYNRUNTIME
    return MICROPY_ENABLE_DYNRUNTIME ? 1 : 0;
#else
    return 0;
#endif
}

int pm_upy_ops_available(void) {
    return 1;
}

int pm_upy_stream_available(void) {
    return MICROPY_PY_SYS_STDFILES ? 1 : 0;
}

int pm_upy_arg_available(void) {
    return 1;
}

int pm_upy_binary_available(void) {
    return 1;
}

int pm_upy_gen_available(void) {
    return 1;
}

int pm_upy_vfs_blockdev_available(void) {
    return MICROPY_VFS ? 1 : 0;
}

int pm_upy_uctypes_available(void) {
    return MICROPY_PY_UCTYPES ? 1 : 0;
}

int pm_upy_re_available(void) {
    return MICROPY_PY_RE ? 1 : 0;
}

int pm_upy_json_available(void) {
    return MICROPY_PY_JSON ? 1 : 0;
}

int pm_upy_deflate_available(void) {
    return MICROPY_PY_DEFLATE ? 1 : 0;
}

int pm_upy_select_available(void) {
    return MICROPY_PY_SELECT ? 1 : 0;
}

int pm_upy_socket_available(void) {
    return MICROPY_PY_SOCKET ? 1 : 0;
}

int pm_upy_bluetooth_available(void) {
    return MICROPY_PY_BLUETOOTH ? 1 : 0;
}

int pm_upy_websocket_available(void) {
    return MICROPY_PY_WEBSOCKET ? 1 : 0;
}

int pm_upy_asyncio_available(void) {
    return MICROPY_PY_ASYNCIO ? 1 : 0;
}

int pm_upy_ssl_available(void) {
    return MICROPY_PY_SSL ? 1 : 0;
}

int pm_upy_hw_available(void) {
    return MICROPY_PY_MACHINE ? 1 : 0;
}

int pm_upy_mpz_available(void) {
    return (MICROPY_LONGINT_IMPL == MICROPY_LONGINT_IMPL_MPZ) ? 1 : 0;
}

int pm_upy_pairheap_available(void) {
    return 1;
}

int pm_upy_ringbuf_available(void) {
    return 1;
}

int pm_upy_formatfloat_available(void) {
    return MICROPY_FLOAT_IMPL ? 1 : 0;
}

int pm_upy_parsenum_available(void) {
    return 1;
}

int pm_upy_scope_available(void) {
    return MICROPY_ENABLE_COMPILER ? 1 : 0;
}

int pm_upy_asm_available(void) {
#if MICROPY_EMIT_NATIVE
    return 1;
#else
    return 0;
#endif
}

int pm_upy_stackalt_available(void) {
#if MICROPY_STACKLESS
    return 1;
#else
    return 0;
#endif
}

int pm_upy_autoload_available(void) {
    return 0;
}

int pm_upy_persistentcode_available(void) {
    return MICROPY_PERSISTENT_CODE_LOAD ? 1 : 0;
}
