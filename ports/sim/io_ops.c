/*
 * Mode A simulator io_ops — natural host fill for metalpython/unix.
 * Metal Mode B uses ports/metal/io_ops.c + Metal HTTP park.
 *
 * Wire with MICROPY_WASM_IO_OPS + mp_wasm_io_set(&mp_wasm_sim_io_ops).
 * Default unix builds may keep MICROPY_WASM_HTTP_NATIVE instead; this port
 * is for dual-mode tests that share the io_ops shape with Metal.
 */
#include "ports/sim/io_ops.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"

#ifndef MICROPY_WASM_HTTP_NATIVE
#define MICROPY_WASM_HTTP_NATIVE 1
#endif

/* Reuse wasmmod's posix HTTP when available (fetch.c). */
extern int mp_wasm_http_fetch(const char *url, uint8_t **out, size_t *out_len,
			     char *err, size_t err_len) __attribute__((weak));

static int sim_fetch(const char *url, uint8_t **out, size_t *out_len, char *err,
		     size_t err_len)
{
	if (mp_wasm_http_fetch != NULL) {
		return mp_wasm_http_fetch(url, out, out_len, err, err_len);
	}
	if (err && err_len) {
		snprintf(err, err_len, "sim io_ops: no HTTP fetch");
	}
	return -1;
}

static int sim_probe(const char *url, char *err, size_t err_len)
{
	(void)url;
	if (err && err_len) {
		err[0] = '\0';
	}
	return 0;
}

static void sim_yield(void)
{
	/* Mode A: natural sched; nothing to park. */
}

mp_wasm_io_ops_t mp_wasm_sim_io_ops = {
	.fetch = sim_fetch,
	.probe = sim_probe,
	.yield = sim_yield,
	.request = NULL,
	.put = NULL,
};

void mp_wasm_sim_io_ops_init(void)
{
	/* Table is static; nothing to fill at runtime. */
}
