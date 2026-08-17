/* pymergetic.wasmmod.net.cdn — module tests (`__tests__.c`). */
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/wasmmod/io.h"
#include "pymergetic/wasmmod/net/cdn.h"
#include "pymergetic/wasmmod/pack/alloc.h"

#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    (void)why;
    return 1;
}

static const uint8_t k_wasm[8] = {0x00, 'a', 's', 'm', 0x01, 0x00, 0x00, 0x00};

static pm_wasmmod_io_result_t stub_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)errbuf;
    (void)errbuf_len;
    if (uri == NULL) {
        return PM_WASMMOD_IO_ERR;
    }
    const uint8_t *src = NULL;
    uint32_t n = 0;
    if (strstr(uri, "/artifacts/lead/hello.wasm") != NULL
        || strstr(uri, "/artifacts/pin/1.0.0/hello.wasm") != NULL) {
        src = k_wasm;
        n = sizeof(k_wasm);
    } else if (strstr(uri, "/index/lead") != NULL || strstr(uri, "/index/pin/1.0.0") != NULL) {
        src = (const uint8_t *)"{\"ok\":1}";
        n = 8;
    } else if (strstr(uri, "/artifacts/lead/html.wasm") != NULL) {
        src = (const uint8_t *)"<html>nope</html>";
        n = 17;
    } else {
        return PM_WASMMOD_IO_ERR;
    }
    uint8_t *p = (uint8_t *)MICROPY_WASM_MALLOC(n);
    if (p == NULL) {
        return PM_WASMMOD_IO_ERR;
    }
    memcpy(p, src, n);
    *out_bytes = p;
    *out_len = n;
    return PM_WASMMOD_IO_OK;
}

static const pm_wasmmod_io_ops_t g_stub_ops = {
    .version = PM_WASMMOD_IO_OPS_VERSION,
    .fetch = stub_fetch,
    .probe = NULL,
    .yield = NULL,
    .request = NULL,
    .put = NULL,
    .userdata = NULL,
};

static void install_stub(void) {
    pm_wasmmod_net_cdn_reset();
    pm_wasmmod_io_set(&g_stub_ops);
}

static void teardown(void) {
    pm_wasmmod_net_cdn_reset();
    pm_wasmmod_io_set(NULL);
}

static int32_t case_configure_bases(void) {
    install_stub();
    if (pm_wasmmod_net_cdn_driver() != PM_WASMMOD_NET_CDN_DRIVER_PATH) {
        teardown();
        return fail("path");
    }
    pm_wasmmod_net_cdn_configure("http://cdn.example/", "tok");
    if (pm_wasmmod_net_cdn_driver() != PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS) {
        teardown();
        return fail("artifacts");
    }
    if (strcmp(pm_wasmmod_net_cdn_base(), "http://cdn.example") != 0) {
        teardown();
        return fail("slash");
    }
    if (pm_wasmmod_net_cdn_url_is_base("http://cdn.example/") != 1) {
        teardown();
        return fail("is_base");
    }
    if (strcmp(pm_wasmmod_io_auth_bearer(), "tok") != 0) {
        teardown();
        return fail("bearer");
    }
    if (pm_wasmmod_net_cdn_add("http://cdn.example", NULL) != 0) {
        teardown();
        return fail("dup");
    }
    if (pm_wasmmod_net_cdn_prepend("http://site.example", NULL) != 1) {
        teardown();
        return fail("prepend");
    }
    if (pm_wasmmod_net_cdn_base_count() != 2) {
        teardown();
        return fail("count");
    }
    if (strcmp(pm_wasmmod_net_cdn_base_at(0), "http://site.example") != 0) {
        teardown();
        return fail("order");
    }
    if (strcmp(pm_wasmmod_net_cdn_driver_name(), "artifacts") != 0) {
        teardown();
        return fail("name");
    }
    pm_wasmmod_net_cdn_set_session_id("sess");
    if (strcmp(pm_wasmmod_net_cdn_session_id(), "sess") != 0) {
        teardown();
        return fail("session");
    }
    teardown();
    return 0;
}

static int32_t case_fetch_lead_and_pin(void) {
    install_stub();
    pm_wasmmod_net_cdn_configure("http://cdn.example", NULL);
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char origin[160];
    char err[64];
    if (pm_wasmmod_net_cdn_fetch_pack_ex("hello", NULL, &buf, &len, origin, sizeof(origin), err,
            sizeof(err))
        != 0) {
        teardown();
        return fail("lead");
    }
    if (len != sizeof(k_wasm) || memcmp(buf, k_wasm, len) != 0) {
        MICROPY_WASM_FREE(buf);
        teardown();
        return fail("magic");
    }
    if (strstr(origin, "/artifacts/lead/hello.wasm") == NULL) {
        MICROPY_WASM_FREE(buf);
        teardown();
        return fail("origin");
    }
    MICROPY_WASM_FREE(buf);
    buf = NULL;
    if (pm_wasmmod_net_cdn_fetch_pack("hello", "1.0.0", &buf, &len, err, sizeof(err)) != 0) {
        teardown();
        return fail("pin");
    }
    MICROPY_WASM_FREE(buf);
    teardown();
    return 0;
}

static int32_t case_reject_html_and_index(void) {
    install_stub();
    pm_wasmmod_net_cdn_configure("http://cdn.example", NULL);
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[80];
    if (pm_wasmmod_net_cdn_fetch_pack("html", NULL, &buf, &len, err, sizeof(err)) == 0) {
        MICROPY_WASM_FREE(buf);
        teardown();
        return fail("html");
    }
    if (pm_wasmmod_net_cdn_fetch_index("lead", &buf, &len, err, sizeof(err)) != 0) {
        teardown();
        return fail("index");
    }
    if (len != 8 || memcmp(buf, "{\"ok\":1}", 8) != 0) {
        MICROPY_WASM_FREE(buf);
        teardown();
        return fail("index body");
    }
    MICROPY_WASM_FREE(buf);
    buf = NULL;
    if (pm_wasmmod_net_cdn_fetch_index("@1.0.0", &buf, &len, err, sizeof(err)) != 0) {
        teardown();
        return fail("index pin");
    }
    MICROPY_WASM_FREE(buf);
    teardown();
    return 0;
}

static int32_t case_needs_configure(void) {
    teardown();
    uint8_t *buf = NULL;
    uint32_t len = 0;
    char err[64];
    if (pm_wasmmod_net_cdn_fetch_pack("hello", NULL, &buf, &len, err, sizeof(err)) == 0) {
        MICROPY_WASM_FREE(buf);
        return fail("noconf");
    }
    if (pm_wasmmod_net_cdn_require_explicit_deps() != 0) {
        return fail("deps");
    }
    char perr[64];
    if (pm_wasmmod_net_cdn_publish("hello", "1.0.0", (const uint8_t *)"x", 1, 1, 1, NULL, perr,
            sizeof(perr))
        == 0) {
        return fail("publish");
    }
    return 0;
}

static char g_post_method[8];
static char g_post_uris[2][192];
static uint8_t g_post_body[16];
static uint32_t g_post_len;
static unsigned g_n_posts;
static char g_post_ct[48];

static pm_wasmmod_io_result_t stub_request(const char *method, const char *uri, const uint8_t *body,
    uint32_t body_len, const char *content_type, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)errbuf;
    (void)errbuf_len;
    if (g_n_posts < 2 && uri != NULL) {
        snprintf(g_post_uris[g_n_posts], sizeof(g_post_uris[g_n_posts]), "%s", uri);
    }
    if (method != NULL) {
        snprintf(g_post_method, sizeof(g_post_method), "%s", method);
    }
    g_post_len = body_len < sizeof(g_post_body) ? body_len : (uint32_t)sizeof(g_post_body);
    if (body != NULL && g_post_len > 0) {
        memcpy(g_post_body, body, g_post_len);
    }
    if (content_type != NULL) {
        snprintf(g_post_ct, sizeof(g_post_ct), "%s", content_type);
    } else {
        g_post_ct[0] = '\0';
    }
    g_n_posts++;
    if (out_bytes != NULL) {
        *out_bytes = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    return PM_WASMMOD_IO_OK;
}

static const pm_wasmmod_io_ops_t g_pub_ops = {
    .version = PM_WASMMOD_IO_OPS_VERSION,
    .fetch = stub_fetch,
    .probe = NULL,
    .yield = NULL,
    .request = stub_request,
    .put = NULL,
    .userdata = NULL,
};

static void install_pub(void) {
    g_n_posts = 0;
    g_post_method[0] = '\0';
    g_post_uris[0][0] = '\0';
    g_post_uris[1][0] = '\0';
    g_post_len = 0;
    g_post_ct[0] = '\0';
    pm_wasmmod_net_cdn_reset();
    pm_wasmmod_io_set(&g_pub_ops);
}

static int32_t case_publish_lead(void) {
    install_pub();
    pm_wasmmod_net_cdn_configure("http://cdn.example", NULL);
    char err[64];
    if (pm_wasmmod_net_cdn_publish("hello", "1.0.0", k_wasm, (uint32_t)sizeof(k_wasm), 1, 0, NULL,
            err, sizeof(err))
        != 0) {
        teardown();
        return fail("lead");
    }
    if (g_n_posts != 1 || strcmp(g_post_method, "POST") != 0) {
        teardown();
        return fail("one post");
    }
    if (strstr(g_post_uris[0], "/artifacts/lead/hello.wasm") == NULL) {
        teardown();
        return fail("lead uri");
    }
    if (g_post_len != sizeof(k_wasm) || memcmp(g_post_body, k_wasm, sizeof(k_wasm)) != 0) {
        teardown();
        return fail("body");
    }
    if (strstr(g_post_ct, "octet-stream") == NULL) {
        teardown();
        return fail("ct");
    }
    teardown();
    return 0;
}

static int32_t case_publish_pin(void) {
    install_pub();
    pm_wasmmod_net_cdn_configure("http://cdn.example", NULL);
    char err[64];
    if (pm_wasmmod_net_cdn_publish("hello", "1.0.0", k_wasm, (uint32_t)sizeof(k_wasm), 0, 1, NULL,
            err, sizeof(err))
        != 0) {
        teardown();
        return fail("pin");
    }
    if (g_n_posts != 1 || strstr(g_post_uris[0], "/artifacts/pin/1.0.0/hello.wasm") == NULL) {
        teardown();
        return fail("pin uri");
    }
    teardown();
    return 0;
}

static int32_t case_publish_both(void) {
    install_pub();
    pm_wasmmod_net_cdn_configure("http://cdn.example", NULL);
    char err[64];
    if (pm_wasmmod_net_cdn_publish("hello", "1.0.0", k_wasm, (uint32_t)sizeof(k_wasm), 1, 1, NULL, err,
            sizeof(err))
        != 0) {
        teardown();
        return fail("both");
    }
    if (g_n_posts != 2) {
        teardown();
        return fail("two posts");
    }
    if (strstr(g_post_uris[0], "/artifacts/lead/hello.wasm") == NULL
        || strstr(g_post_uris[1], "/artifacts/pin/1.0.0/hello.wasm") == NULL) {
        teardown();
        return fail("order");
    }
    teardown();
    return 0;
}

PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, configure_bases, case_configure_bases);
PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, fetch_lead_and_pin, case_fetch_lead_and_pin);
PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, reject_html_and_index, case_reject_html_and_index);
PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, needs_configure, case_needs_configure);
PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, publish_lead, case_publish_lead);
PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, publish_pin, case_publish_pin);
PM_MOD_TEST_C(pymergetic.wasmmod.net.cdn, publish_both, case_publish_both);
