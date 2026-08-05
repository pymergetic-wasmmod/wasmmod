/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef PM_PM_WASMMOD_PATH_IO_H_
#define PM_PM_WASMMOD_PATH_IO_H_

#include "pm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_WASMMOD_IO_OK = 0,
    PM_WASMMOD_IO_DECLINE = 1,
    PM_WASMMOD_IO_ERR = -1,
} pm_wasmmod_io_result_t;

typedef pm_wasmmod_io_result_t (*pm_wasmmod_io_request_t)(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len,
    const char *content_type,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);

typedef struct pm_wasmmod_io_ops {
    uint32_t version;
    pm_wasmmod_io_result_t (*fetch)(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
        char *errbuf, size_t errbuf_len);
    pm_wasmmod_io_result_t (*probe)(const char *uri);
    void (*yield)(void);
    pm_wasmmod_io_request_t request;
} pm_wasmmod_io_ops_t;

void pm_wasmmod_io_set(const pm_wasmmod_io_ops_t *ops);
const pm_wasmmod_io_ops_t *pm_wasmmod_io_get(void);
void pm_wasmmod_io_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PATH_IO_H_ */
