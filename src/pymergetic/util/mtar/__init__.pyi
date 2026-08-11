# pymergetic.util.mtar — hand-written stand-in for what the facegen tool
# should emit from __exports__.h's pm_wasmmod_pyexport_export_py*-routed symbols (see
# SOURCETREE.md "Python face"). Native pm_util_mtar_first/pm_util_mtar_next is a manual
# cursor over raw pointers (see __exports__.h) — not something to expose
# 1:1, that's not a Python shape. The `obj`-shape marshaling instead
# surfaces the idiomatic iterator a Python caller actually wants; the
# native cursor stays an implementation detail behind it. Nothing at
# runtime reads this file — type-checker sugar only.

from typing import Iterator

class Entry:
    name: bytes
    data: bytes
    is_dir: bool

def iter_entries(buf: bytes) -> Iterator[Entry]: ...
