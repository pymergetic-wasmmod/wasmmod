/* pymergetic.wasmmod.io — impl. Consumer face is generated __exports__.h.
 *
 * Host I/O table: fetch / probe / yield. Wait class lives on the face
 * (SOURCETREE.md): fetch/probe/request = async; yield = facade; uri/join/set/get
 * = sync. upywm may block inside the fill; Metal later parks in the same slot.
 */
#include "pymergetic/wasmmod/io/__types__.h"

#include "pymergetic/wasmmod/pack/alloc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MICROPY_WASM_HTTP_NATIVE
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define MICROPY_WASM_HTTP_NATIVE (1)
#else
#define MICROPY_WASM_HTTP_NATIVE (0)
#endif
#endif

static void err_set(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, "%s", msg);
}

static pm_wasmmod_io_result_t io_decline_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)uri;
    (void)out_bytes;
    (void)out_len;
    (void)errbuf;
    (void)errbuf_len;
    return PM_WASMMOD_IO_DECLINE;
}

static pm_wasmmod_io_result_t io_decline_probe(const char *uri) {
    (void)uri;
    return PM_WASMMOD_IO_DECLINE;
}

#if defined(__GNUC__)
__attribute__((unused))
#endif
static const pm_wasmmod_io_ops_t pm_wasmmod_io_builtin = {
    .version = PM_WASMMOD_IO_OPS_VERSION,
    .fetch = io_decline_fetch,
    .probe = io_decline_probe,
    .yield = NULL,
    .request = NULL,
    .put = NULL,
    .userdata = NULL,
};

#ifdef MICROPY_WASM_IO_OPS
#define PM_WASMMOD_IO_DEFAULT (&(MICROPY_WASM_IO_OPS))
#else
#define PM_WASMMOD_IO_DEFAULT (&pm_wasmmod_io_builtin)
#endif

static const pm_wasmmod_io_ops_t *pm_wasmmod_io_cur;
static char g_auth_bearer[256];

const char *pm_wasmmod_net_cdn_session_id(void) __attribute__((weak));

void pm_wasmmod_io_set(const pm_wasmmod_io_ops_t *ops) {
    pm_wasmmod_io_cur = (ops != NULL) ? ops : PM_WASMMOD_IO_DEFAULT;
}

int pm_wasmmod_io_is_set(void) {
    return pm_wasmmod_io_cur != NULL;
}

const pm_wasmmod_io_ops_t *pm_wasmmod_io_get(void) {
    if (pm_wasmmod_io_cur == NULL) {
        pm_wasmmod_io_cur = PM_WASMMOD_IO_DEFAULT;
    }
    return pm_wasmmod_io_cur;
}

void pm_wasmmod_io_yield(void) {
    const pm_wasmmod_io_ops_t *ops = pm_wasmmod_io_get();
    if (ops->yield != NULL) {
        ops->yield();
    }
}

int32_t pm_wasmmod_io_uri_is_http(const char *uri) {
    return (uri != NULL && (memcmp(uri, "http://", 7) == 0 || memcmp(uri, "https://", 8) == 0))
        ? 1
        : 0;
}

void pm_wasmmod_io_join_uri(const char *root, const char *rel, char *out, uint32_t out_cap) {
    if (out == NULL || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (root == NULL) {
        root = "";
    }
    if (rel == NULL) {
        rel = "";
    }
    size_t rlen = strlen(root);
    if (rel[0] == '/') {
        rel++;
    }
    int n;
    if (rlen > 0 && root[rlen - 1] != '/') {
        n = snprintf(out, out_cap, "%s/%s", root, rel);
    } else {
        n = snprintf(out, out_cap, "%s%s", root, rel);
    }
    (void)n;
}

void pm_wasmmod_io_set_auth_bearer(const char *token) {
    g_auth_bearer[0] = '\0';
    if (token != NULL && token[0] != '\0') {
        size_t n = strlen(token);
        if (n >= sizeof(g_auth_bearer)) {
            n = sizeof(g_auth_bearer) - 1;
        }
        memcpy(g_auth_bearer, token, n);
        g_auth_bearer[n] = '\0';
    }
}

const char *pm_wasmmod_io_auth_bearer(void) {
    return g_auth_bearer;
}

typedef struct {
    uint8_t *p;
    uint32_t n;
    uint32_t cap;
} pm_io_buf_t;

#if MICROPY_WASM_HTTP_NATIVE
static int buf_append(pm_io_buf_t *b, const void *src, size_t n) {
    if (n == 0) {
        return 0;
    }
    if (n > 0xffffffffu - b->n) {
        return -1;
    }
    uint32_t need = b->n + (uint32_t)n;
    if (need > b->cap) {
        uint32_t cap = b->cap ? b->cap : 256u;
        while (cap < need) {
            uint32_t next = cap * 2u;
            if (next < cap) {
                return -1;
            }
            cap = next;
        }
        uint8_t *np = (uint8_t *)MICROPY_WASM_MALLOC(cap);
        if (np == NULL) {
            return -1;
        }
        if (b->n > 0 && b->p != NULL) {
            memcpy(np, b->p, b->n);
        }
        if (b->p != NULL) {
            MICROPY_WASM_FREE(b->p);
        }
        b->p = np;
        b->cap = cap;
    }
    memcpy(b->p + b->n, src, n);
    b->n += (uint32_t)n;
    return 0;
}

static void buf_clear(pm_io_buf_t *b) {
    if (b->p != NULL) {
        MICROPY_WASM_FREE(b->p);
    }
    b->p = NULL;
    b->n = 0;
    b->cap = 0;
}
#endif

#if !defined(PM_METAL_FIRMWARE)
static pm_wasmmod_io_result_t fetch_file(const char *path, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    pm_wasmmod_io_yield();
    if (memcmp(path, "file://", 7) == 0) {
        path += 7;
    }
    if (path[0] == '\0') {
        err_set(errbuf, errbuf_len, "empty path");
        return PM_WASMMOD_IO_ERR;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        err_set(errbuf, errbuf_len, "file open failed");
        return PM_WASMMOD_IO_ERR;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        err_set(errbuf, errbuf_len, "file seek failed");
        return PM_WASMMOD_IO_ERR;
    }
    long sz = ftell(f);
    if (sz < 0 || (unsigned long long)sz > 0xffffffffull) {
        fclose(f);
        err_set(errbuf, errbuf_len, "file too large");
        return PM_WASMMOD_IO_ERR;
    }
    rewind(f);
    uint32_t n = (uint32_t)sz;
    uint8_t *buf = (uint8_t *)MICROPY_WASM_MALLOC(n ? n : 1u);
    if (buf == NULL) {
        fclose(f);
        err_set(errbuf, errbuf_len, "oom");
        return PM_WASMMOD_IO_ERR;
    }
    size_t got = n ? fread(buf, 1, n, f) : 0;
    fclose(f);
    if (got != (size_t)n) {
        MICROPY_WASM_FREE(buf);
        err_set(errbuf, errbuf_len, "file read failed");
        return PM_WASMMOD_IO_ERR;
    }
    *out_bytes = buf;
    *out_len = n;
    return PM_WASMMOD_IO_OK;
}
#endif /* !PM_METAL_FIRMWARE */

#if MICROPY_WASM_HTTP_NATIVE

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MICROPY_SSL_MBEDTLS
#define MICROPY_SSL_MBEDTLS (0)
#endif

#if MICROPY_SSL_MBEDTLS
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#endif

typedef struct {
    int https;
    char host[256];
    char path[1024];
    int port;
} parsed_url_t;

static int parse_http_url(const char *uri, parsed_url_t *u, char *errbuf, size_t errbuf_len) {
    memset(u, 0, sizeof(*u));
    const char *p = uri;
    if (memcmp(p, "https://", 8) == 0) {
        u->https = 1;
        u->port = 443;
        p += 8;
    } else if (memcmp(p, "http://", 7) == 0) {
        u->https = 0;
        u->port = 80;
        p += 7;
    } else {
        err_set(errbuf, errbuf_len, "bad scheme");
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    size_t host_len = (size_t)(host_end - p);
    if (host_len == 0 || host_len >= sizeof(u->host)) {
        err_set(errbuf, errbuf_len, "bad host");
        return -1;
    }
    memcpy(u->host, p, host_len);
    u->host[host_len] = '\0';

    char *colon = strchr(u->host, ':');
    if (colon != NULL) {
        *colon = '\0';
        u->port = atoi(colon + 1);
        if (u->port <= 0) {
            err_set(errbuf, errbuf_len, "bad port");
            return -1;
        }
    }

    if (slash != NULL) {
        size_t plen = strlen(slash);
        if (plen >= sizeof(u->path)) {
            err_set(errbuf, errbuf_len, "path too long");
            return -1;
        }
        memcpy(u->path, slash, plen + 1);
    } else {
        strcpy(u->path, "/");
    }
    return 0;
}

typedef struct {
    int fd;
#if MICROPY_SSL_MBEDTLS
    int tls;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
#endif
} http_conn_t;

#if MICROPY_SSL_MBEDTLS
static int wasm_net_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n = send(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return (int)n;
}

static int wasm_net_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = *(int *)ctx;
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    return (int)n;
}
#endif

static int conn_write(http_conn_t *c, const void *buf, size_t len) {
#if MICROPY_SSL_MBEDTLS
    if (c->tls) {
        size_t off = 0;
        while (off < len) {
            pm_wasmmod_io_yield();
            int n = mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf + off, len - off);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (n <= 0) {
                return -1;
            }
            off += (size_t)n;
        }
        return 0;
    }
#endif
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(c->fd, (const char *)buf + off, len - off, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int conn_read(http_conn_t *c, void *buf, size_t len) {
#if MICROPY_SSL_MBEDTLS
    if (c->tls) {
        for (;;) {
            int n = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, len);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                return 0;
            }
            return n;
        }
    }
#endif
    for (;;) {
        ssize_t n = recv(c->fd, buf, len, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return (int)n;
    }
}

static void conn_close(http_conn_t *c) {
#if MICROPY_SSL_MBEDTLS
    if (c->tls) {
        mbedtls_ssl_close_notify(&c->ssl);
        mbedtls_ssl_free(&c->ssl);
        mbedtls_ssl_config_free(&c->conf);
        mbedtls_ctr_drbg_free(&c->ctr_drbg);
        mbedtls_entropy_free(&c->entropy);
        c->tls = 0;
    }
#endif
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

static int conn_open(http_conn_t *c, const parsed_url_t *u, char *errbuf, size_t errbuf_len) {
    memset(c, 0, sizeof(*c));
    c->fd = -1;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", u->port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    struct addrinfo *res = NULL;
    if (getaddrinfo(u->host, portstr, &hints, &res) != 0 || res == NULL) {
        err_set(errbuf, errbuf_len, "DNS failed");
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        err_set(errbuf, errbuf_len, "connect failed");
        return -1;
    }
    c->fd = fd;

    if (!u->https) {
        return 0;
    }

#if MICROPY_SSL_MBEDTLS
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->ctr_drbg);
    c->tls = 1;

    const char *pers = "wasmmod-fetch";
    if (mbedtls_ctr_drbg_seed(&c->ctr_drbg, mbedtls_entropy_func, &c->entropy,
            (const unsigned char *)pers, strlen(pers))
            != 0
        || mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)
            != 0) {
        err_set(errbuf, errbuf_len, "TLS init failed");
        conn_close(c);
        return -1;
    }
    /* Match micropython-lib requests on embedded: no CA verify. */
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);
    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0
        || mbedtls_ssl_set_hostname(&c->ssl, u->host) != 0) {
        err_set(errbuf, errbuf_len, "TLS setup failed");
        conn_close(c);
        return -1;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, wasm_net_send, wasm_net_recv, NULL);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        pm_wasmmod_io_yield();
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            err_set(errbuf, errbuf_len, "TLS handshake failed");
            conn_close(c);
            return -1;
        }
    }
    return 0;
#else
    err_set(errbuf, errbuf_len, "https requires host io_ops or MICROPY_SSL_MBEDTLS");
    conn_close(c);
    return -1;
#endif
}

/* HTTP/1.0 exchange. resp_body NULL → discard payload. req_body may be NULL.
 * success_any_2xx: POST/PUT accept 200–299; GET/HEAD still require 200. */
static int http_exchange(const char *method, const char *uri, const uint8_t *req_body,
    uint32_t req_len, const char *content_type, pm_io_buf_t *resp_body, int *status_out,
    int success_any_2xx, char *errbuf, size_t errbuf_len) {
    parsed_url_t u;
    if (parse_http_url(uri, &u, errbuf, errbuf_len) != 0) {
        return -1;
    }

    http_conn_t conn;
    if (conn_open(&conn, &u, errbuf, errbuf_len) != 0) {
        return -1;
    }

    if (req_body != NULL && content_type == NULL) {
        content_type = "application/octet-stream";
    }

    char req[1792];
    const char *sid = pm_wasmmod_net_cdn_session_id ? pm_wasmmod_net_cdn_session_id() : NULL;
    int nreq = snprintf(req, sizeof(req), "%s %s HTTP/1.0\r\nHost: %s\r\n", method, u.path, u.host);
    if (nreq < 0 || (size_t)nreq >= sizeof(req)) {
        err_set(errbuf, errbuf_len, "HTTP write failed");
        conn_close(&conn);
        return -1;
    }
    if (g_auth_bearer[0] != '\0') {
        int k = snprintf(req + nreq, sizeof(req) - (size_t)nreq, "Authorization: Bearer %s\r\n",
            g_auth_bearer);
        if (k < 0 || (size_t)k >= sizeof(req) - (size_t)nreq) {
            err_set(errbuf, errbuf_len, "HTTP write failed");
            conn_close(&conn);
            return -1;
        }
        nreq += k;
    }
    if (sid != NULL && sid[0] != '\0') {
        int k = snprintf(req + nreq, sizeof(req) - (size_t)nreq, "X-Shell-Session-Id: %s\r\n", sid);
        if (k < 0 || (size_t)k >= sizeof(req) - (size_t)nreq) {
            err_set(errbuf, errbuf_len, "HTTP write failed");
            conn_close(&conn);
            return -1;
        }
        nreq += k;
    }
    if (req_body != NULL) {
        int k = snprintf(req + nreq, sizeof(req) - (size_t)nreq,
            "Content-Type: %s\r\nContent-Length: %u\r\n", content_type, (unsigned)req_len);
        if (k < 0 || (size_t)k >= sizeof(req) - (size_t)nreq) {
            err_set(errbuf, errbuf_len, "HTTP write failed");
            conn_close(&conn);
            return -1;
        }
        nreq += k;
    }
    {
        int k = snprintf(req + nreq, sizeof(req) - (size_t)nreq,
            "Connection: close\r\nUser-Agent: wasmmod\r\n\r\n");
        if (k < 0 || (size_t)k >= sizeof(req) - (size_t)nreq) {
            err_set(errbuf, errbuf_len, "HTTP write failed");
            conn_close(&conn);
            return -1;
        }
        nreq += k;
    }
    if (nreq <= 0 || (size_t)nreq >= sizeof(req) || conn_write(&conn, req, (size_t)nreq) < 0) {
        err_set(errbuf, errbuf_len, "HTTP write failed");
        conn_close(&conn);
        return -1;
    }
    if (req_body != NULL && req_len > 0 && conn_write(&conn, req_body, req_len) < 0) {
        err_set(errbuf, errbuf_len, "HTTP write failed");
        conn_close(&conn);
        return -1;
    }

    pm_io_buf_t hdr = {0};
    char chunk[1024];
    int status = 0;
    int headers_done = 0;
    size_t header_scan = 0;

    for (;;) {
        pm_wasmmod_io_yield();
        int n = conn_read(&conn, chunk, sizeof(chunk));
        if (n < 0) {
            err_set(errbuf, errbuf_len, "HTTP read failed");
            buf_clear(&hdr);
            if (resp_body != NULL) {
                buf_clear(resp_body);
            }
            conn_close(&conn);
            return -1;
        }
        if (n == 0) {
            break;
        }
        if (!headers_done) {
            if (buf_append(&hdr, chunk, (size_t)n) != 0) {
                err_set(errbuf, errbuf_len, "oom");
                buf_clear(&hdr);
                if (resp_body != NULL) {
                    buf_clear(resp_body);
                }
                conn_close(&conn);
                return -1;
            }
            while (header_scan + 3 < hdr.n) {
                if (hdr.p[header_scan] == '\r' && hdr.p[header_scan + 1] == '\n'
                    && hdr.p[header_scan + 2] == '\r' && hdr.p[header_scan + 3] == '\n') {
                    headers_done = 1;
                    size_t body_off = header_scan + 4;
                    uint8_t *line_end = (uint8_t *)memchr(hdr.p, '\n', hdr.n);
                    if (line_end != NULL) {
                        *line_end = '\0';
                        if (line_end > hdr.p && line_end[-1] == '\r') {
                            line_end[-1] = '\0';
                        }
                        const char *sp = strchr((const char *)hdr.p, ' ');
                        if (sp != NULL) {
                            status = atoi(sp + 1);
                        }
                    }
                    if (resp_body != NULL && body_off < hdr.n) {
                        if (buf_append(resp_body, hdr.p + body_off, hdr.n - body_off) != 0) {
                            err_set(errbuf, errbuf_len, "oom");
                            buf_clear(&hdr);
                            buf_clear(resp_body);
                            conn_close(&conn);
                            return -1;
                        }
                    }
                    break;
                }
                header_scan++;
            }
        } else if (resp_body != NULL) {
            if (buf_append(resp_body, chunk, (size_t)n) != 0) {
                err_set(errbuf, errbuf_len, "oom");
                buf_clear(&hdr);
                buf_clear(resp_body);
                conn_close(&conn);
                return -1;
            }
        }
    }

    buf_clear(&hdr);
    conn_close(&conn);

    if (status == 0) {
        err_set(errbuf, errbuf_len, "HTTP bad response");
        if (resp_body != NULL) {
            buf_clear(resp_body);
        }
        return -1;
    }
    if (status_out != NULL) {
        *status_out = status;
    }
    int ok = success_any_2xx ? (status >= 200 && status < 300) : (status == 200);
    if (!ok) {
        char msg[32];
        snprintf(msg, sizeof(msg), "HTTP %d", status);
        err_set(errbuf, errbuf_len, msg);
        if (resp_body != NULL) {
            buf_clear(resp_body);
        }
        return -1;
    }
    return 0;
}

static int native_http_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len, char *errbuf,
    size_t errbuf_len) {
    pm_io_buf_t body = {0};
    if (http_exchange("GET", uri, NULL, 0, NULL, &body, NULL, 0, errbuf, errbuf_len) != 0) {
        return -1;
    }
    if (body.p == NULL) {
        body.p = (uint8_t *)MICROPY_WASM_MALLOC(1);
        if (body.p == NULL) {
            err_set(errbuf, errbuf_len, "oom");
            return -1;
        }
        body.n = 0;
    }
    *out_bytes = body.p;
    *out_len = body.n;
    return 0;
}

static int native_http_probe(const char *uri) {
    char err[64];
    int status = 0;
    if (http_exchange("HEAD", uri, NULL, 0, NULL, NULL, &status, 0, err, sizeof(err)) == 0) {
        return 1;
    }
    if (status != 405 && status != 501) {
        return 0;
    }
    pm_io_buf_t body = {0};
    if (http_exchange("GET", uri, NULL, 0, NULL, &body, &status, 0, err, sizeof(err)) == 0) {
        buf_clear(&body);
        return 1;
    }
    return 0;
}

static int native_http_request(const char *method, const char *uri, const uint8_t *req_body,
    uint32_t req_len, const char *content_type, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    int any_2xx = (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0);
    pm_io_buf_t body = {0};
    pm_io_buf_t *resp = (out_bytes != NULL) ? &body : NULL;
    if (http_exchange(method, uri, req_body, req_len, content_type, resp, NULL, any_2xx, errbuf,
            errbuf_len)
        != 0) {
        return -1;
    }
    if (out_bytes == NULL) {
        return 0;
    }
    if (body.p == NULL) {
        body.p = (uint8_t *)MICROPY_WASM_MALLOC(1);
        if (body.p == NULL) {
            err_set(errbuf, errbuf_len, "oom");
            return -1;
        }
        body.n = 0;
    }
    *out_bytes = body.p;
    if (out_len != NULL) {
        *out_len = body.n;
    }
    return 0;
}

#else /* !MICROPY_WASM_HTTP_NATIVE */

static int native_http_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len, char *errbuf,
    size_t errbuf_len) {
    (void)uri;
    (void)out_bytes;
    (void)out_len;
    err_set(errbuf, errbuf_len, "HTTP fetch requires host io_ops");
    return -1;
}

static int native_http_probe(const char *uri) {
    (void)uri;
    return 0;
}

static int native_http_request(const char *method, const char *uri, const uint8_t *req_body,
    uint32_t req_len, const char *content_type, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)method;
    (void)uri;
    (void)req_body;
    (void)req_len;
    (void)content_type;
    (void)out_bytes;
    (void)out_len;
    err_set(errbuf, errbuf_len, "HTTP request requires host io_ops");
    return -1;
}

#endif /* MICROPY_WASM_HTTP_NATIVE */

int32_t pm_wasmmod_io_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len, char *errbuf,
    size_t errbuf_len) {
    if (out_bytes != NULL) {
        *out_bytes = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (uri == NULL || uri[0] == '\0') {
        err_set(errbuf, errbuf_len, "empty uri");
        return -1;
    }
    if (out_bytes == NULL || out_len == NULL) {
        err_set(errbuf, errbuf_len, "null out");
        return -1;
    }
    if (errbuf != NULL && errbuf_len > 0) {
        errbuf[0] = '\0';
    }

    const pm_wasmmod_io_ops_t *ops = pm_wasmmod_io_get();
    if (ops->fetch != NULL) {
        pm_wasmmod_io_result_t r = ops->fetch(uri, out_bytes, out_len, errbuf, errbuf_len);
        if (r == PM_WASMMOD_IO_OK) {
            return 0;
        }
        if (r == PM_WASMMOD_IO_ERR) {
            return -1;
        }
        /* DECLINE → default backends */
    }

    if (pm_wasmmod_io_uri_is_http(uri)) {
        return native_http_fetch(uri, out_bytes, out_len, errbuf, errbuf_len);
    }
#if !defined(PM_METAL_FIRMWARE)
    return (fetch_file(uri, out_bytes, out_len, errbuf, errbuf_len) == PM_WASMMOD_IO_OK) ? 0 : -1;
#else
    err_set(errbuf, errbuf_len, "file fetch requires host io_ops");
    return -1;
#endif
}

int32_t pm_wasmmod_io_probe(const char *uri) {
    if (!pm_wasmmod_io_uri_is_http(uri)) {
        return 0;
    }

    const pm_wasmmod_io_ops_t *ops = pm_wasmmod_io_get();
    if (ops->probe != NULL) {
        pm_wasmmod_io_result_t r = ops->probe(uri);
        if (r == PM_WASMMOD_IO_OK) {
            return 1;
        }
        if (r == PM_WASMMOD_IO_ERR) {
            return 0;
        }
    } else if (ops->fetch != NULL && ops->fetch != io_decline_fetch) {
        uint8_t *buf = NULL;
        uint32_t len = 0;
        char err[64];
        pm_wasmmod_io_result_t r = ops->fetch(uri, &buf, &len, err, sizeof(err));
        if (r == PM_WASMMOD_IO_OK) {
            MICROPY_WASM_FREE(buf);
            return 1;
        }
        if (r == PM_WASMMOD_IO_ERR) {
            return 0;
        }
    }

    return native_http_probe(uri);
}

int32_t pm_wasmmod_io_request(const char *method, const char *uri, const uint8_t *body,
    uint32_t body_len, const char *content_type, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    if (out_bytes != NULL) {
        *out_bytes = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (method == NULL || method[0] == '\0') {
        err_set(errbuf, errbuf_len, "empty method");
        return -1;
    }
    if (uri == NULL || uri[0] == '\0') {
        err_set(errbuf, errbuf_len, "empty uri");
        return -1;
    }
    if ((out_bytes == NULL) != (out_len == NULL)) {
        err_set(errbuf, errbuf_len, "null out");
        return -1;
    }
    if (errbuf != NULL && errbuf_len > 0) {
        errbuf[0] = '\0';
    }

    const pm_wasmmod_io_ops_t *ops = pm_wasmmod_io_get();
    if (ops->request != NULL) {
        pm_wasmmod_io_result_t r = ops->request(method, uri, body, body_len, content_type, out_bytes,
            out_len, errbuf, errbuf_len);
        if (r == PM_WASMMOD_IO_OK) {
            return 0;
        }
        if (r == PM_WASMMOD_IO_ERR) {
            return -1;
        }
        /* DECLINE → default backends */
    }

    if (pm_wasmmod_io_uri_is_http(uri)) {
        return native_http_request(method, uri, body, body_len, content_type, out_bytes, out_len,
            errbuf, errbuf_len);
    }
    err_set(errbuf, errbuf_len, "request requires http(s)");
    return -1;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_set, pm_wasmmod_io_set, void(const pm_wasmmod_io_ops_t *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_get, pm_wasmmod_io_get, const pm_wasmmod_io_ops_t *(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_yield, pm_wasmmod_io_yield, void(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_uri_is_http, pm_wasmmod_io_uri_is_http, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_join_uri, pm_wasmmod_io_join_uri, void(const char *, const char *, char *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_set_auth_bearer, pm_wasmmod_io_set_auth_bearer, void(const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_auth_bearer, pm_wasmmod_io_auth_bearer, const char *(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_fetch, pm_wasmmod_io_fetch, int32_t(const char *, uint8_t **, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_probe, pm_wasmmod_io_probe, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.io, pm_wasmmod_io_request, pm_wasmmod_io_request, int32_t(const char *, const char *, const uint8_t *, uint32_t, const char *, uint8_t **, uint32_t *, char *, size_t));
