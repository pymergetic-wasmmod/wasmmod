/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Prefer Emscripten's wasi/api.h over WAMR's duplicate platform_wasi_types.h
 * when building nested WAMR with the Emscripten toolchain (wasm-in-wasm host).
 * Force-included via CMAKE_C_FLAGS when WASMMOD_EMSCRIPTEN=1.
 *
 * Host clang/clangd: empty — this header is only meaningful under emcc.
 */
#ifndef WASMMOD_WAMR_EM_WASI_SHIM_H
#define WASMMOD_WAMR_EM_WASI_SHIM_H

#if defined(__EMSCRIPTEN__)
#ifndef _PLATFORM_WASI_TYPES_H
#include <wasi/api.h>
#define _PLATFORM_WASI_TYPES_H
#endif
#endif

#endif /* WASMMOD_WAMR_EM_WASI_SHIM_H */
