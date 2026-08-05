/*
 * Bindgen entry for host-side pm_* umbrellas.
 * Force host mode even if clang targets wasm unexpectedly.
 */
#ifndef PM_WASMMOD_GUEST
#define PM_WASMMOD_GUEST 0
#endif

#include "pm_guest.h" /* IWYU pragma: keep */
#include "pm_upy.h" /* IWYU pragma: keep */ 
#include "pm_wasmmod.h" /* IWYU pragma: keep */
