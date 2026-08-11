# pymergetic.util.lz4 — hand-written stand-in for what the facegen tool
# should emit from __exports__.h's pm_wasmmod_pyexport_export_py*-routed symbols (see
# SOURCETREE.md "Python face"). Same `mem`-shape marshaling as zlib: native
# side is raw pointers/caps, Python side is plain bytes in/out. Nothing at
# runtime reads this file — type-checker sugar only.

def compress(src: bytes, dst_cap: int) -> bytes: ...
def decompress(src: bytes, dst_len: int) -> bytes: ...
