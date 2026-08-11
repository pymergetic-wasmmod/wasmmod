/* pymergetic.util.pysample — hand-written stand-in for what the packer's
 * py-facegen should emit by parsing __init__.py's type hints (see
 * SOURCETREE.md "Py export face"). Delete once that pipeline exists; this
 * is training-scaffold, not the plan.
 *
 * Both of these are real, callable C functions with a Python function
 * underneath (see __pmm__.toml: impl = "py") — same slot-backed
 * same-artifact call shape as any c/rs export ("Same-artifact calls stay
 * private"); the fact that the real body happens to be Python is
 * invisible from this side of the face.
 */
#ifndef PYMERGETIC_UTIL_PYSAMPLE_EXPORT_H
#define PYMERGETIC_UTIL_PYSAMPLE_EXPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* hello() -> int : no args, int return -> the default i32 shape. */
int32_t pm_util_pysample_hello(void);

/* echo_len(data: bytes) -> int : bytes param -> the mem shape, native
 * passes a real pointer+length, marshaled into a Python bytes object
 * before the call. */
int32_t pm_util_pysample_echo_len(const uint8_t *data, uint32_t data_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_PYSAMPLE_EXPORT_H */
