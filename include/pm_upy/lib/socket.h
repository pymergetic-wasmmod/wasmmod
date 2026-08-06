/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LIB_SOCKET_H_
#define PM_PM_UPY_LIB_SOCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_socket_available(void);

#include <stddef.h>
#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_socket_create(int af, int type, int proto);
int pm_upy_socket_connect(pm_upy_obj_t sock, const char *host, int port);
int pm_upy_socket_bind(pm_upy_obj_t sock, const char *host, int port);
int pm_upy_socket_listen(pm_upy_obj_t sock, int backlog);
pm_upy_obj_t pm_upy_socket_accept(pm_upy_obj_t sock);
int pm_upy_socket_send(pm_upy_obj_t sock, const void *buf, size_t len);
int pm_upy_socket_recv(pm_upy_obj_t sock, void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_SOCKET_H_ */
