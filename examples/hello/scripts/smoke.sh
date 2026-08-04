#!/usr/bin/env bash
# Tiny smoke helper (also a syntax-highlight sample for .sh in the package viewer).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "hello pack root: $ROOT"

if [[ -f "$ROOT/hello.wasm" ]]; then
  ls -lh "$ROOT/hello.wasm"
else
  echo "no hello.wasm yet — build with: make -C $ROOT"
fi
