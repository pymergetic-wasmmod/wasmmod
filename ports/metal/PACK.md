# Metal × packs (constellation)

**Sole loader:** wasmmod (`pm_wasmmod_pack_*` / `import_wasm` / finder / CDN).
WAMR engine is owned by **wasmmod**. Metal only supplies platform GLUE (TLSF, ticks,
io_ops park) and early floor.

See Metal [CONSTELLATION.md](../../../../metal/docs/CONSTELLATION.md) (path from
Metal tree: `packages/metal/docs/CONSTELLATION.md`).

## Legacy path (migrating — do not grow)

| Path | Role | Status |
|------|------|--------|
| Metal `forge pack` → `pm_metal_imports` → `pm_metal_wasm_*` | Old product guests via Metal reg | **Legacy** |
| wasmmod pack / CDN / `import_wasm` | µPy guest packs | **Product path** |

Target: `forge pack` emits **wasmmod-format** packs; bringup/load only via wasmmod APIs.
Do not dual-load the same guest on both paths while migrating.

## Wire-up (M1) — demo path (OK)

1. io_ops + HTTP park + `proof_io_fetch`
2. `mp_pack_load` (hello / ticks)
3. `HOST_FACES=1`
4. finder + `wasm.path`
5. `proof_import_wasm` / module / `install_hook`

Tree: `wasm → upy ok` / `selfcheck ok`.

## Status (constellation)

1. **Done (strangler):** Metal `forge pack` emits `wasmmod.pack` / `wasmmod.imports` (+ legacy `pm_metal_imports` dual-write)
2. **Product load:** wasmmod proofs in Metal bringup (`mp_pack_load` / `import_wasm`); Metal `pm_metal_wasm_*` = `EXP2_STRESS` only
3. **In progress:** AbstractNIC adapter (`pm_metal_net_upy_nic_register`); wasmmod consumes NICs
4. Keep upstream `metalpython` `wasmmod` branch vanilla
5. See also: `HOST_ASYNC.md`, `NET.md`, Metal `docs/CONSTELLATION.md`
