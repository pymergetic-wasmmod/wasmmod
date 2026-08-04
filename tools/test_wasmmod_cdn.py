#!/usr/bin/env python3
"""Unit tests for wasmmod cdn artifact picking (no network)."""

from __future__ import annotations

import importlib.util
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def _load():
    path = TOOLS / "wasmmod_cdn.py"
    spec = importlib.util.spec_from_file_location("wasmmod_cdn", path)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_pick_prefers_zlib() -> None:
    mod = _load()
    entry = {
        "artifacts": [
            {"path": "hello.wasm", "kind": "wasm", "encoding": "raw"},
            {"path": "hello.wasm.zlib", "kind": "wasm", "encoding": "mpzl"},
            {"path": "hello.aot6", "kind": "aot", "encoding": "raw"},
            {"path": "hello.aot6.zlib", "kind": "aot", "encoding": "mpzl"},
        ]
    }
    names = mod._pick_artifacts(entry, prefer_zlib=True, aot_only=False, wasm_only=False)
    assert names == ["hello.wasm.zlib", "hello.aot6.zlib"]


def test_pick_aot_only_raw() -> None:
    mod = _load()
    entry = {
        "artifacts": [
            {"path": "hello.wasm.zlib"},
            {"path": "hello.aot6"},
            {"path": "hello.aot6.zlib"},
        ]
    }
    names = mod._pick_artifacts(entry, prefer_zlib=False, aot_only=True, wasm_only=False)
    assert names == ["hello.aot6", "hello.aot6.zlib"]
