# Metal freestanding WAMR (wasmmod OWN)

wasmmod owns the freestanding WAMR **engine** recipe for Metal.

| Piece | Owner |
|-------|--------|
| Source list + `-D` / target flags | `wamr_freestanding.mk` (this dir) |
| WAMR tree | **`third_party/wamr` only** |
| Platform GLUE (`metal_platform.c`, TLSF, ticks) | Metal |
| Stub faces (`runtime_glue.c`) | Metal |
| Final link of `libwasmmod_wamr_freestanding.a` | Metal `wasm/.pm/build.rs` |

Unix host WAMR remains `ports/micropython/micropython.mk` (cmake `vmlib`).

```bash
make -f ports/metal/wamr_freestanding.mk \
  OUT_DIR=/tmp/wamr-fs \
  METAL_PLAT_INC=... METAL_PORT_INC=... \
  METAL_LIBC_INC=... METAL_SRC_INC=... METAL_INCLUDE_INC=... \
  UEFI=0
```
