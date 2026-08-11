/* pymergetic.util.lz4 — hand-written stand-in for what `cbindgen` should
 * emit by reading lz4.rs. Delete once the real cbindgen pipeline exists;
 * this is training-scaffold, not the plan. */
#ifndef PYMERGETIC_UTIL_LZ4_EXPORT_H
#define PYMERGETIC_UTIL_LZ4_EXPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_LZ4_OK 0
#define PM_LZ4_ERR_NOSPACE (-1)
#define PM_LZ4_ERR_DATA (-2)

/* One block, no frame header/trailer. Returns bytes written or a
 * PM_LZ4_ERR_* code. */
int32_t pm_util_lz4_compress(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_cap);

/* dst must already be sized to the exact expected output length (no
 * streaming, no growth) — caller already knows it from the pack manifest. */
int32_t pm_util_lz4_decompress(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_LZ4_EXPORT_H */
