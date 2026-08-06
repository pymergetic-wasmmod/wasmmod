# Network border — wasmmod consumes NICs

```
Metal virtio/bge/...   IMPLEMENT   AbstractNIC / nic_protocol (L2 ops today)
wasmmod                CONSUMES    NICs; GIVES lwIP / socket / network / pm_upy_*
metalpython            OWNS        protocol clients + boot orchestration
Metal                  OWNS        early floor + optional servers (sshd/httpd)
```

## Metal today

- L2: `pm_metal_net_ip_l2_ops_t` + `dev/net/_virtio_net.c` / bge
- Adapter start: `pm_metal_net_upy_nic_register()` records ops for µPy hand-off
- Product lwIP still Metal-owned short-term; migrate face to wasmmod without a second stack

## Do not

- Add Metal-only Python net bindings (`pm_metal_py_net_*`)
- Run a second µPy-lwIP engine beside Metal’s stack during migration
- Put Metal virtio policy into the autonomous metalpython `wasmmod` PR

## Status

1. **Done:** `MICROPY_PY_NETWORK` + `network.LAN` (`network_metal_nic.c`) + `mod_network_register_nic` after `mp_init`
2. **Done:** Metal TCP socket face on nic_protocol (connect/send/recv/close)
3. **Next:** `MICROPY_PY_SOCKET` / wasmmod consume path; DNS; metalpython HTTP clients on shared face
