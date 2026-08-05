"""Stage a cleaned wasmmod checkout into the wheel via force-include."""

from __future__ import annotations

import shutil
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface

SKIP_DIRS = {".keys", "target", "__pycache__", "packs", ".git"}
SKIP_SUFFIXES = {".wasm", ".zlib", ".sig", ".wat", ".o", ".a"}


def _skip_file(path: Path) -> bool:
    if path.suffix in SKIP_SUFFIXES:
        return True
    if path.suffix.startswith(".aot"):
        return True
    if path.suffix == ".pem" and "key" in path.name:
        return True
    return False


def _copy_filtered(src: Path, dst: Path) -> None:
    if src.name in SKIP_DIRS:
        return
    if src.is_symlink():
        return
    if src.is_file():
        if _skip_file(src):
            return
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        return
    for child in sorted(src.iterdir()):
        _copy_filtered(child, dst / child.name)


class CustomBuildHook(BuildHookInterface):
    PLUGIN_NAME = "custom"

    def initialize(self, version: str, build_data: dict) -> None:
        root = Path(self.root)
        share = root / "python" / "pymergetic" / "wasmmod" / "rt" / "share"
        if share.exists():
            shutil.rmtree(share)
        share.mkdir(parents=True)

        for name in ("loader.c", "wasmmod.c"):
            shutil.copy2(root / name, share / name)
        for name in ("crates", "format", "ports", "examples"):
            _copy_filtered(root / name, share / name)
        (share / "tools").mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / "tools" / "wasmmod.py", share / "tools" / "wasmmod.py")

        python_root = root / "python"
        force: dict[str, str] = {}
        for path in share.rglob("*"):
            if path.is_file():
                force[str(path)] = str(path.relative_to(python_root))
        build_data["force_include"] = force

    def finalize(self, version: str, build_data: dict, artifact_path: str) -> None:
        share = Path(self.root) / "python" / "pymergetic" / "wasmmod" / "rt" / "share"
        if share.exists():
            shutil.rmtree(share)
