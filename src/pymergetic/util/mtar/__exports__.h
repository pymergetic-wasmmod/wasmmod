/* pymergetic.util.mtar — hand-written stand-in for what `cbindgen` should
 * emit by reading mtar.rs (types + `#[unsafe(no_mangle)] pub extern "C"`
 * fns live directly in the impl file for a Rust-defined module — see
 * SOURCETREE.md "Types SoT"). Delete once the real cbindgen pipeline
 * exists; this is training-scaffold, not the plan. */
#ifndef PYMERGETIC_UTIL_MTAR_EXPORT_H
#define PYMERGETIC_UTIL_MTAR_EXPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t *name_ptr;
    uint32_t name_len;
    const uint8_t *data_ptr;
    uint32_t data_len;
    int32_t is_dir;
    uint32_t header_off;
} pm_util_mtar_entry_t;

#define PM_UTIL_MTAR_OK 0
#define PM_UTIL_MTAR_END 1
#define PM_UTIL_MTAR_ERR_TRUNCATED (-1)
#define PM_UTIL_MTAR_ERR_CHECKSUM (-2)
#define PM_UTIL_MTAR_ERR_SIZE (-3)

int32_t pm_util_mtar_first(const uint8_t *buf, uint32_t buf_len, pm_util_mtar_entry_t *out);
int32_t pm_util_mtar_next(const uint8_t *buf, uint32_t buf_len, const pm_util_mtar_entry_t *prev, pm_util_mtar_entry_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_MTAR_EXPORT_H */
