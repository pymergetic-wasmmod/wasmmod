#!/usr/bin/env bash
# Fail if ports/micropython/*.c use MP_QSTR_* missing from build-wasm genhdr.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GENHDR="${ROOT}/../../ports/unix/build-wasm/genhdr/qstrdefs.generated.h"
PORT="${ROOT}/ports/micropython"
if [[ ! -f "$GENHDR" ]]; then
  echo "missing $GENHDR — build BUILD=build-wasm genhdr first" >&2
  exit 1
fi
miss=0
while IFS= read -r q; do
  [[ -z "$q" ]] && continue
  case "$q" in
    MP_QSTR___name__|MP_QSTR___init__|MP_QSTR___import__|MP_QSTR___file__|MP_QSTR_) continue ;;
  esac
  if ! grep -F -q "$q" "$GENHDR"; then
    echo "MISSING in build-wasm genhdr: $q (add Q(...) to qstrdefs.wasmmod + rebuild genhdr)" >&2
    miss=1
  fi
done < <(rg -oN --no-filename 'MP_QSTR_[A-Za-z0-9_]+' "$PORT" -g '*.c' -g '*.h' | sort -u)
if [[ "$miss" -ne 0 ]]; then
  exit 1
fi
echo "ok: all port MP_QSTR_* present in build-wasm genhdr"
