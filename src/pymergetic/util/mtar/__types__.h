/* pymergetic.util.mtar — shared ABI shapes. See SOURCETREE.md "Faces". */
#ifndef PYMERGETIC_UTIL_MTAR_TYPES_H
#define PYMERGETIC_UTIL_MTAR_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MtarEntry {
    const uint8_t *name_ptr;
    uint32_t name_len;
    const uint8_t *data_ptr;
    uint32_t data_len;
    int32_t is_dir;
    uint32_t header_off;
} MtarEntry;

#define PM_UTIL_MTAR_OK 0
#define PM_UTIL_MTAR_END 1
#define PM_UTIL_MTAR_ERR_TRUNCATED (-1)
#define PM_UTIL_MTAR_ERR_CHECKSUM (-2)
#define PM_UTIL_MTAR_ERR_SIZE (-3)

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_UTIL_MTAR_TYPES_H */
