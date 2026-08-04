/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/nlr.h"
#include "py/qstr.h"
#include "py/reader.h"

#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/io.h"

#include "extmod/wasmmod/alloc.h"

// Native HTTP(S) on POSIX-like hosts. Ports override via mp_wasm_io_ops_t.
#ifndef MICROPY_WASM_HTTP_NATIVE
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#define MICROPY_WASM_HTTP_NATIVE (1)
#else
#define MICROPY_WASM_HTTP_NATIVE (0)
#endif
#endif

// --- I/O ops (Metal / port replaceable) ------------------------------------

static mp_wasm_io_result_t io_decline_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    (void)uri;
    (void)out_bytes;
    (void)out_len;
    (void)errbuf;
    (void)errbuf_len;
    return MP_WASM_IO_DECLINE;
}

static mp_wasm_io_result_t io_decline_probe(const char *uri) {
    (void)uri;
    return MP_WASM_IO_DECLINE;
}

static const mp_wasm_io_ops_t mp_wasm_io_builtin = {
    .version = MP_WASM_IO_OPS_VERSION,
    .fetch = io_decline_fetch,
    .probe = io_decline_probe,
    .yield = NULL,
    .reserved0 = NULL,
    .reserved1 = NULL,
    .userdata = NULL,
};

#ifdef MICROPY_WASM_IO_OPS
#define MP_WASM_IO_DEFAULT (&(MICROPY_WASM_IO_OPS))
#else
#define MP_WASM_IO_DEFAULT (&mp_wasm_io_builtin)
#endif

static const mp_wasm_io_ops_t *mp_wasm_io_cur;

void mp_wasm_io_set(const mp_wasm_io_ops_t *ops) {
    mp_wasm_io_cur = (ops != NULL) ? ops : MP_WASM_IO_DEFAULT;
}

const mp_wasm_io_ops_t *mp_wasm_io_get(void) {
    if (mp_wasm_io_cur == NULL) {
        mp_wasm_io_cur = MP_WASM_IO_DEFAULT;
    }
    return mp_wasm_io_cur;
}

void mp_wasm_io_yield(void) {
    const mp_wasm_io_ops_t *ops = mp_wasm_io_get();
    if (ops->yield != NULL) {
        ops->yield();
    }
}

bool mp_wasm_uri_is_http(const char *uri) {
    return uri != NULL
        && (memcmp(uri, "http://", 7) == 0 || memcmp(uri, "https://", 8) == 0);
}

void mp_wasm_join_uri(const char *root, const char *rel, vstr_t *out) {
    vstr_init(out, strlen(root) + strlen(rel) + 2);
    vstr_add_str(out, root);
    if (out->len > 0 && out->buf[out->len - 1] != '/') {
        vstr_add_char(out, '/');
    }
    if (rel[0] == '/') {
        rel++;
    }
    vstr_add_str(out, rel);
}

static bool fetch_file(const char *path, vstr_t *out) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, qstr_from_str(path));
    vstr_init(out, 256);
    for (;;) {
        mp_uint_t b = reader.readbyte(reader.data);
        if (b == MP_READER_EOF) {
            break;
        }
        vstr_add_byte(out, (byte)b);
    }
    reader.close(reader.data);
    return true;
}

#if MICROPY_WASM_HTTP_NATIVE

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#if MICROPY_SSL_MBEDTLS
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h" // MBEDTLS_ERR_NET_* for bio callbacks
#include "mbedtls/ssl.h"
#endif

typedef struct {
    bool https;
    char host[256];
    char path[1024];
    int port;
} parsed_url_t;

static bool parse_http_url(const char *uri, parsed_url_t *u, char *errbuf, size_t errbuf_len) {
    memset(u, 0, sizeof(*u));
    const char *p = uri;
    if (memcmp(p, "https://", 8) == 0) {
        u->https = true;
        u->port = 443;
        p += 8;
    } else if (memcmp(p, "http://", 7) == 0) {
        u->https = false;
        u->port = 80;
        p += 7;
    } else {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "bad scheme");
        }
        return false;
    }

    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    size_t host_len = (size_t)(host_end - p);
    if (host_len == 0 || host_len >= sizeof(u->host)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "bad host");
        }
        return false;
    }
    memcpy(u->host, p, host_len);
    u->host[host_len] = '\0';

    char *colon = strchr(u->host, ':');
    if (colon != NULL) {
        *colon = '\0';
        u->port = atoi(colon + 1);
        if (u->port <= 0) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "bad port");
            }
            return false;
        }
    }

    if (slash != NULL) {
        size_t plen = strlen(slash);
        if (plen >= sizeof(u->path)) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "path too long");
            }
            return false;
        }
        memcpy(u->path, slash, plen + 1);
    } else {
        strcpy(u->path, "/");
    }
    return true;
}

typedef struct {
    int fd;
    #if MICROPY_SSL_MBEDTLS
    bool tls;
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
            int n = mbedtls_ssl_write(&c->ssl, (const unsigned char *)buf + off, len - off);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
            if (n <= 0) {
                return -1;
            }
            off += (size_t)n;
        }
        return (int)len;
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
    return (int)len;
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
        c->tls = false;
    }
    #endif
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
}

static bool conn_open(http_conn_t *c, const parsed_url_t *u, char *errbuf, size_t errbuf_len) {
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
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "DNS failed: %s", u->host);
        }
        return false;
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
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "connect failed: %s", u->host);
        }
        return false;
    }
    c->fd = fd;

    if (!u->https) {
        return true;
    }

    #if MICROPY_SSL_MBEDTLS
    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->ctr_drbg);
    c->tls = true;

    const char *pers = "wasmmod-fetch";
    if (mbedtls_ctr_drbg_seed(&c->ctr_drbg, mbedtls_entropy_func, &c->entropy,
            (const unsigned char *)pers, strlen(pers)) != 0
        || mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "TLS init failed");
        }
        conn_close(c);
        return false;
    }
    // Match micropython-lib requests default for embedded: no CA verify.
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);
    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0
        || mbedtls_ssl_set_hostname(&c->ssl, u->host) != 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "TLS setup failed");
        }
        conn_close(c);
        return false;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->fd, wasm_net_send, wasm_net_recv, NULL);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "TLS handshake failed");
            }
            conn_close(c);
            return false;
        }
    }
    return true;
    #else
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "HTTPS requires MICROPY_SSL_MBEDTLS");
    }
    conn_close(c);
    return false;
    #endif
}

// method = "GET" or "HEAD". If body_out non-NULL and GET, appends response body.
static char g_auth_bearer[256];

void mp_wasm_fetch_set_auth_bearer(const char *token) {
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

static bool http_exchange(const char *method, const char *uri, vstr_t *body_out,
    int *status_out, char *errbuf, size_t errbuf_len) {
    parsed_url_t u;
    if (!parse_http_url(uri, &u, errbuf, errbuf_len)) {
        return false;
    }

    http_conn_t conn;
    if (!conn_open(&conn, &u, errbuf, errbuf_len)) {
        return false;
    }

    char req[1792];
    int nreq;
    if (g_auth_bearer[0] != '\0') {
        nreq = snprintf(req, sizeof(req),
            "%s %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "Authorization: Bearer %s\r\n"
            "Connection: close\r\n"
            "User-Agent: wasmmod\r\n"
            "\r\n",
            method, u.path, u.host, g_auth_bearer);
    } else {
        nreq = snprintf(req, sizeof(req),
            "%s %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "Connection: close\r\n"
            "User-Agent: wasmmod\r\n"
            "\r\n",
            method, u.path, u.host);
    }
    if (nreq <= 0 || (size_t)nreq >= sizeof(req)
        || conn_write(&conn, req, (size_t)nreq) < 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "HTTP write failed");
        }
        conn_close(&conn);
        return false;
    }

    vstr_t hdr;
    vstr_init(&hdr, 512);
    char buf[1024];
    int status = 0;
    bool headers_done = false;
    size_t header_scan = 0;

    if (body_out != NULL) {
        vstr_init(body_out, 256);
    }

    for (;;) {
        mp_wasm_io_yield();
        int n = conn_read(&conn, buf, sizeof(buf));
        if (n < 0) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "HTTP read failed");
            }
            vstr_clear(&hdr);
            if (body_out) {
                vstr_clear(body_out);
            }
            conn_close(&conn);
            return false;
        }
        if (n == 0) {
            break;
        }
        if (!headers_done) {
            vstr_add_strn(&hdr, buf, (size_t)n);
            // Look for end of headers.
            while (header_scan + 3 < hdr.len) {
                if (hdr.buf[header_scan] == '\r' && hdr.buf[header_scan + 1] == '\n'
                    && hdr.buf[header_scan + 2] == '\r' && hdr.buf[header_scan + 3] == '\n') {
                    headers_done = true;
                    size_t body_off = header_scan + 4;
                    // Parse status from first line.
                    char *line_end = memchr(hdr.buf, '\n', hdr.len);
                    if (line_end != NULL) {
                        *line_end = '\0';
                        if (line_end > hdr.buf && line_end[-1] == '\r') {
                            line_end[-1] = '\0';
                        }
                        const char *sp = strchr(hdr.buf, ' ');
                        if (sp != NULL) {
                            status = atoi(sp + 1);
                        }
                    }
                    if (body_out != NULL && body_off < hdr.len) {
                        vstr_add_strn(body_out, hdr.buf + body_off, hdr.len - body_off);
                    }
                    break;
                }
                header_scan++;
            }
        } else if (body_out != NULL) {
            vstr_add_strn(body_out, buf, (size_t)n);
        } else {
            // HEAD / discard body after headers.
        }
    }

    vstr_clear(&hdr);
    conn_close(&conn);

    if (status == 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "HTTP bad response");
        }
        if (body_out) {
            vstr_clear(body_out);
        }
        return false;
    }
    if (status_out) {
        *status_out = status;
    }
    if (status != 200) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "HTTP %d", status);
        }
        if (body_out) {
            vstr_clear(body_out);
        }
        return false;
    }
    return true;
}

static bool native_http_fetch(const char *uri, vstr_t *out, char *errbuf, size_t errbuf_len) {
    return http_exchange("GET", uri, out, NULL, errbuf, errbuf_len);
}

static bool native_http_probe(const char *uri) {
    char err[64];
    int status = 0;
    if (http_exchange("HEAD", uri, NULL, &status, err, sizeof(err))) {
        return true;
    }
    // Only fall back when HEAD is unsupported — not on 404 / other misses.
    if (status != 405 && status != 501) {
        return false;
    }
    vstr_t body;
    if (http_exchange("GET", uri, &body, &status, err, sizeof(err))) {
        vstr_clear(&body);
        return true;
    }
    return false;
}

#else // !MICROPY_WASM_HTTP_NATIVE

void mp_wasm_fetch_set_auth_bearer(const char *token) {
    (void)token;
}

static bool native_http_fetch(const char *uri, vstr_t *out, char *errbuf, size_t errbuf_len) {
    (void)uri;
    (void)out;
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "HTTP fetch requires port I/O ops");
    }
    return false;
}

static bool native_http_probe(const char *uri) {
    (void)uri;
    return false;
}

#endif // MICROPY_WASM_HTTP_NATIVE

static bool take_port_fetch(const char *uri, vstr_t *out, char *errbuf, size_t errbuf_len,
    bool *hard_fail) {
    *hard_fail = false;
    const mp_wasm_io_ops_t *ops = mp_wasm_io_get();
    if (ops->fetch == NULL) {
        return false;
    }
    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (errbuf && errbuf_len) {
        errbuf[0] = '\0';
    }
    mp_wasm_io_result_t r = ops->fetch(uri, &buf, &len, errbuf, errbuf_len);
    if (r == MP_WASM_IO_OK) {
        vstr_init(out, len + 1);
        if (len > 0 && buf != NULL) {
            vstr_add_strn(out, (const char *)buf, len);
        }
        MICROPY_WASM_FREE(buf);
        return true;
    }
    if (r == MP_WASM_IO_ERR) {
        *hard_fail = true;
        return false;
    }
    return false; // DECLINE
}

bool mp_wasm_http_probe(const char *uri) {
    if (!mp_wasm_uri_is_http(uri)) {
        return false;
    }

    const mp_wasm_io_ops_t *ops = mp_wasm_io_get();
    if (ops->probe != NULL) {
        mp_wasm_io_result_t r = ops->probe(uri);
        if (r == MP_WASM_IO_OK) {
            return true;
        }
        if (r == MP_WASM_IO_ERR) {
            return false;
        }
        // DECLINE → default
    } else if (ops->fetch != NULL && ops->fetch != io_decline_fetch) {
        // Port provided fetch only: probe = successful GET (discard body).
        uint8_t *buf = NULL;
        uint32_t len = 0;
        char err[64];
        mp_wasm_io_result_t r = ops->fetch(uri, &buf, &len, err, sizeof(err));
        if (r == MP_WASM_IO_OK) {
            MICROPY_WASM_FREE(buf);
            return true;
        }
        if (r == MP_WASM_IO_ERR) {
            return false;
        }
    }

    return native_http_probe(uri);
}

bool mp_wasm_fetch(const char *uri, vstr_t *out, char *errbuf, size_t errbuf_len) {
    if (uri == NULL || uri[0] == '\0') {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "empty uri");
        }
        return false;
    }

    // 1) Port I/O ops (Metal async→sync, custom HTTP, …)
    bool hard = false;
    if (take_port_fetch(uri, out, errbuf, errbuf_len, &hard)) {
        return true;
    }
    if (hard) {
        return false;
    }

    // 2) Legacy compile-time macro (prefer MICROPY_WASM_IO_OPS)
    #ifdef MICROPY_WASM_FETCH
    {
        uint8_t *hook_buf = NULL;
        uint32_t hook_len = 0;
        if (errbuf && errbuf_len) {
            errbuf[0] = '\0';
        }
        if (MICROPY_WASM_FETCH(uri, &hook_buf, &hook_len, errbuf, errbuf_len)) {
            vstr_init(out, hook_len + 1);
            vstr_add_strn(out, (const char *)hook_buf, hook_len);
            MICROPY_WASM_FREE(hook_buf);
            return true;
        }
        // Non-empty errbuf ⇒ hard failure from the hook.
        if (errbuf && errbuf_len && errbuf[0] != '\0') {
            return false;
        }
    }
    #endif

    // 3) Default backends
    if (mp_wasm_uri_is_http(uri)) {
        return native_http_fetch(uri, out, errbuf, errbuf_len);
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        fetch_file(uri, out);
        nlr_pop();
        return true;
    }
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "fetch failed: %s", uri);
    }
    return false;
}

#endif // MICROPY_PY_WASM
