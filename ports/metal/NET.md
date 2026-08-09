# Network border — wasmmod consumes NICs

```
Metal virtio/bge/...   IMPLEMENT   AbstractNIC / nic_protocol (L2 ops today)
wasmmod                CONSUMES    NICs; GIVES lwIP / socket / network / pm_upy_*
metalpython            OWNS        protocol clients + boot orchestration
Metal                  OWNS        early floor + optional servers (sshd/httpd)
```

## Metal today

- **One stack:** µPy `lib/lwip` (NO_SYS) owned by Metal bring-up; `MICROPY_PY_LWIP=1` → `modlwip` / stdlib `socket` on the same netifs
- L2: virtio-net / bge → lwIP `ethN`; loopback `lo`; WireGuard `wgN` (smartalock wireguard-lwip)
- Metal async sockets + if-mgmt: `pm_metal_net_ip_*` / `cfg.h` / `sock.h` (C); `metalnet` module (Py); F7 dashboard
- L4: per-service TCP/UDP socks on real ports (ASGI `:80`, SSH `:22`, DNS/NTP/TFTP each own a DGRAM sock). Dual-slot TCP / singleton UDP faces deleted.
- `network.LAN` is status/`ifconfig`/`resolve` over the default netif (not a second IP engine)

## Do not

- Run a second lwIP instance beside Metal’s stack
- Share one listen/accepted PCB across protocols
- Revive COM1 `PM_METAL_SHELL_CMD` net/nslookup for product UI
- Put Metal virtio policy into the autonomous metalpython `wasmmod` PR

## Status

1. **Done:** lwIP + DHCP + multi-if status + Metal sockets + µPy modlwip (BIOS QEMU)
2. **Done:** WireGuard `wgN` face (C/RS/Py; slot budget = `PM_METAL_NET_IP_MAX_IFS`); in-guest lo handshake prove (`pm_metal_net_wg_handshake_smoke` uses wg0↔wg1)
3. **Done:** Normal IP stack L4 — sock listen/accept/connect per port; no dual-slot/singleton faces
4. **Next:** wasmmod consume path without a second stack; UEFI seat parity
