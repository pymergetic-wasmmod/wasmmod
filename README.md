# wasmmod

Runtime for signed wasm/AOT/ELF packs: registry, loader, `pymergetic.wasmmod.io`, CDN client, `pymergetic.util.mem`. Nested at `extmod/wasmmod` on **upywm** and **mp**.

| Pill | Repo | Role |
|------|------|------|
| **upy** | [micropython/micropython](https://github.com/micropython/micropython) | vanilla µPy. CDN engine only. |
| **upywm** | [pymergetic-wasmmod/micropython-wasmmod](https://github.com/pymergetic-wasmmod/micropython-wasmmod) | upy + wasmmod. No metal. |
| **mp** | [pymergetic/metalpython](https://github.com/pymergetic/metalpython) | upywm + metal. |
| **wasmmod** | **this repo** | packs, loader, io, cdn client, `util.mem`, gen. |
| **metal** | [pymergetic/metal](https://github.com/pymergetic/metal) | `pymergetic.metal` on mp only. `main` = cards; `preview` = standalone (doom). |
| **cdn** | [pymergetic-wasmmod/wasmmod-cdn](https://github.com/pymergetic-wasmmod/wasmmod-cdn) | catalog / inspect / shells. |
| **doom** | [pymergetic/metal-doom](https://github.com/pymergetic/metal-doom) | gfx proof vs metal **preview**. |
| **os-sdk** | [pymergetic/os-sdk](https://github.com/pymergetic/os-sdk) | pin hub. |

One defining lang per card. Faces from `wasmmod-gen`. Heap is `pymergetic.util.mem` (`impl = c`). Docs: `docs/SOURCETREE.md`, `docs/CELLBUILD.md`.
