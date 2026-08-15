/* pymergetic.wasmmod.io — module tests (`__tests__.c`). */
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/pack/alloc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int32_t fail(const char *why) {
    (void)why;
    return 1;
}

static int32_t case_uri_is_http(void) {
    if (pm_wasmmod_io_uri_is_http("http://x") != 1) {
        return fail("http");
    }
    if (pm_wasmmod_io_uri_is_http("https://x") != 1) {
        return fail("https");
    }
    if (pm_wasmmod_io_uri_is_http("/tmp/x") != 0) {
        return fail("file");
    }
    if (pm_wasmmod_io_uri_is_http(NULL) != 0) {
        return fail("null");
    }
    return 0;
}

static int32_t case_join_uri(void) {
    char out[64];
    pm_wasmmod_io_join_uri("http://cdn.example", "pack.bin", out, sizeof(out));
    if (strcmp(out, "http://cdn.example/pack.bin") != 0) {
        return fail("join slash");
    }
    pm_wasmmod_io_join_uri("http://cdn.example/", "/pack.bin", out, sizeof(out));
    if (strcmp(out, "http://cdn.example/pack.bin") != 0) {
        return fail("join strip");
    }
    return 0;
}

static int32_t case_file_roundtrip(void) {
    char path[80];
    snprintf(path, sizeof(path), "/tmp/pm_wasmmod_io_%d.bin", (int)getpid());
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return fail("open write");
    }
    const char *payload = "hello-io";
    if (fwrite(payload, 1, strlen(payload), f) != strlen(payload)) {
        fclose(f);
        unlink(path);
        return fail("write");
    }
    fclose(f);

    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[64];
    int32_t st = pm_wasmmod_io_fetch(path, &buf, &len, err, sizeof(err));
    unlink(path);
    if (st != 0 || buf == NULL || len != strlen(payload) || memcmp(buf, payload, len) != 0) {
        if (buf != NULL) {
            MICROPY_WASM_FREE(buf);
        }
        return fail("fetch");
    }
    MICROPY_WASM_FREE(buf);
    return 0;
}

static pm_wasmmod_io_result_t stub_ok(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)uri;
    (void)errbuf;
    (void)errbuf_len;
    const char *s = "ops-ok";
    size_t n = strlen(s);
    uint8_t *p = (uint8_t *)MICROPY_WASM_MALLOC(n);
    if (p == NULL) {
        return PM_WASMMOD_IO_ERR;
    }
    memcpy(p, s, n);
    *out_bytes = p;
    *out_len = (uint32_t)n;
    return PM_WASMMOD_IO_OK;
}

static pm_wasmmod_io_result_t stub_decline(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)uri;
    (void)out_bytes;
    (void)out_len;
    (void)errbuf;
    (void)errbuf_len;
    return PM_WASMMOD_IO_DECLINE;
}

static int32_t case_ops_ok_short_circuits(void) {
    static const pm_wasmmod_io_ops_t ops = {
        .version = PM_WASMMOD_IO_OPS_VERSION,
        .fetch = stub_ok,
        .probe = NULL,
        .yield = NULL,
        .request = NULL,
        .put = NULL,
        .userdata = NULL,
    };
    pm_wasmmod_io_set(&ops);
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[32];
    int32_t st = pm_wasmmod_io_fetch("http://unused", &buf, &len, err, sizeof(err));
    pm_wasmmod_io_set(NULL);
    if (st != 0 || buf == NULL || len != 6 || memcmp(buf, "ops-ok", 6) != 0) {
        if (buf != NULL) {
            MICROPY_WASM_FREE(buf);
        }
        return fail("ops");
    }
    MICROPY_WASM_FREE(buf);
    return 0;
}

static int32_t case_ops_decline_falls_to_file(void) {
    static const pm_wasmmod_io_ops_t ops = {
        .version = PM_WASMMOD_IO_OPS_VERSION,
        .fetch = stub_decline,
        .probe = NULL,
        .yield = NULL,
        .request = NULL,
        .put = NULL,
        .userdata = NULL,
    };
    char path[80];
    snprintf(path, sizeof(path), "/tmp/pm_wasmmod_io_d_%d.bin", (int)getpid());
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return fail("open");
    }
    fputc('Z', f);
    fclose(f);

    pm_wasmmod_io_set(&ops);
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[32];
    int32_t st = pm_wasmmod_io_fetch(path, &buf, &len, err, sizeof(err));
    pm_wasmmod_io_set(NULL);
    unlink(path);
    if (st != 0 || buf == NULL || len != 1 || buf[0] != 'Z') {
        if (buf != NULL) {
            MICROPY_WASM_FREE(buf);
        }
        return fail("decline");
    }
    MICROPY_WASM_FREE(buf);
    return 0;
}

static int32_t case_https_errors(void) {
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[80];
    /* Closed port: fail fast (no public DNS). Native TLS or no-TLS both error. */
    int32_t st = pm_wasmmod_io_fetch("https://127.0.0.1:1/x", &buf, &len, err, sizeof(err));
    if (st == 0) {
        if (buf != NULL) {
            MICROPY_WASM_FREE(buf);
        }
        return fail("https ok");
    }
    if (pm_wasmmod_io_probe("https://127.0.0.1:1/x") != 0) {
        return fail("https probe");
    }
    return 0;
}

static int32_t case_empty_uri(void) {
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[32];
    if (pm_wasmmod_io_fetch("", &buf, &len, err, sizeof(err)) == 0) {
        return fail("empty");
    }
    if (pm_wasmmod_io_probe("/tmp/nope") != 0) {
        return fail("probe file");
    }
    return 0;
}

static int32_t case_bearer_and_yield(void) {
    pm_wasmmod_io_set_auth_bearer("tok");
    if (strcmp(pm_wasmmod_io_auth_bearer(), "tok") != 0) {
        return fail("bearer");
    }
    pm_wasmmod_io_set_auth_bearer(NULL);
    if (pm_wasmmod_io_auth_bearer()[0] != '\0') {
        return fail("clear");
    }
    pm_wasmmod_io_yield();
    if (pm_wasmmod_io_get() == NULL) {
        return fail("get");
    }
    return 0;
}

static pm_wasmmod_io_result_t stub_probe_hello(const char *uri) {
    return (uri != NULL && strstr(uri, "hello.wasm") != NULL) ? PM_WASMMOD_IO_OK : PM_WASMMOD_IO_ERR;
}

static int32_t case_join_then_probe(void) {
    static const pm_wasmmod_io_ops_t ops = {
        .version = PM_WASMMOD_IO_OPS_VERSION,
        .fetch = stub_decline,
        .probe = stub_probe_hello,
        .yield = NULL,
        .request = NULL,
        .put = NULL,
        .userdata = NULL,
    };
    char uri[128];
    pm_wasmmod_io_set(&ops);
    pm_wasmmod_io_join_uri("http://cdn.example/packs", "hello.wasm", uri, sizeof(uri));
    int32_t hit = pm_wasmmod_io_probe(uri);
    int32_t miss = pm_wasmmod_io_probe("http://cdn.example/packs/nope.wasm");
    pm_wasmmod_io_set(NULL);
    if (strcmp(uri, "http://cdn.example/packs/hello.wasm") != 0) {
        return fail("join");
    }
    if (hit != 1 || miss != 0) {
        return fail("probe");
    }
    return 0;
}

static pm_wasmmod_io_result_t stub_request_ok(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len, const char *content_type, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    (void)method;
    (void)uri;
    (void)body;
    (void)body_len;
    (void)content_type;
    (void)errbuf;
    (void)errbuf_len;
    if (out_bytes != NULL) {
        *out_bytes = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    return PM_WASMMOD_IO_OK;
}

static pm_wasmmod_io_result_t stub_request_decline(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len, const char *content_type, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    (void)method;
    (void)uri;
    (void)body;
    (void)body_len;
    (void)content_type;
    (void)out_bytes;
    (void)out_len;
    (void)errbuf;
    (void)errbuf_len;
    return PM_WASMMOD_IO_DECLINE;
}

static int32_t case_request_ops_ok(void) {
    static const pm_wasmmod_io_ops_t ops = {
        .version = PM_WASMMOD_IO_OPS_VERSION,
        .fetch = stub_decline,
        .probe = NULL,
        .yield = NULL,
        .request = stub_request_ok,
        .put = NULL,
        .userdata = NULL,
    };
    pm_wasmmod_io_set(&ops);
    char err[32];
    int32_t st = pm_wasmmod_io_request("POST", "http://unused", (const uint8_t *)"x", 1,
        "application/octet-stream", NULL, NULL, err, sizeof(err));
    pm_wasmmod_io_set(NULL);
    if (st != 0) {
        return fail("request ops");
    }
    return 0;
}

static int32_t case_request_https_errors(void) {
    char err[80];
    int32_t st = pm_wasmmod_io_request("POST", "https://127.0.0.1:1/x", (const uint8_t *)"x", 1,
        NULL, NULL, NULL, err, sizeof(err));
    if (st == 0) {
        return fail("https ok");
    }
    return 0;
}

static int32_t case_request_empty(void) {
    char err[32];
    if (pm_wasmmod_io_request("", "http://x", NULL, 0, NULL, NULL, NULL, err, sizeof(err)) == 0) {
        return fail("empty method");
    }
    if (pm_wasmmod_io_request("POST", "", NULL, 0, NULL, NULL, NULL, err, sizeof(err)) == 0) {
        return fail("empty uri");
    }
    return 0;
}

static int32_t case_request_decline_needs_http(void) {
    static const pm_wasmmod_io_ops_t ops = {
        .version = PM_WASMMOD_IO_OPS_VERSION,
        .fetch = stub_decline,
        .probe = NULL,
        .yield = NULL,
        .request = stub_request_decline,
        .put = NULL,
        .userdata = NULL,
    };
    pm_wasmmod_io_set(&ops);
    char err[64];
    int32_t st = pm_wasmmod_io_request("PUT", "/tmp/nope", (const uint8_t *)"x", 1, NULL, NULL, NULL,
        err, sizeof(err));
    pm_wasmmod_io_set(NULL);
    if (st == 0) {
        return fail("file put");
    }
    if (strstr(err, "requires http") == NULL) {
        return fail("requires http");
    }
    return 0;
}

PM_MOD_TEST_C(pymergetic.wasmmod.io, uri_is_http, case_uri_is_http);
PM_MOD_TEST_C(pymergetic.wasmmod.io, join_uri, case_join_uri);
PM_MOD_TEST_C(pymergetic.wasmmod.io, file_roundtrip, case_file_roundtrip);
PM_MOD_TEST_C(pymergetic.wasmmod.io, ops_ok_short_circuits, case_ops_ok_short_circuits);
PM_MOD_TEST_C(pymergetic.wasmmod.io, ops_decline_falls_to_file, case_ops_decline_falls_to_file);
PM_MOD_TEST_C(pymergetic.wasmmod.io, https_errors, case_https_errors);
PM_MOD_TEST_C(pymergetic.wasmmod.io, empty_uri, case_empty_uri);
PM_MOD_TEST_C(pymergetic.wasmmod.io, bearer_and_yield, case_bearer_and_yield);
PM_MOD_TEST_C(pymergetic.wasmmod.io, join_then_probe, case_join_then_probe);
PM_MOD_TEST_C(pymergetic.wasmmod.io, request_ops_ok, case_request_ops_ok);
PM_MOD_TEST_C(pymergetic.wasmmod.io, request_https_errors, case_request_https_errors);
PM_MOD_TEST_C(pymergetic.wasmmod.io, request_empty, case_request_empty);
PM_MOD_TEST_C(pymergetic.wasmmod.io, request_decline_needs_http, case_request_decline_needs_http);
