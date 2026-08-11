# pymergetic.util.zlib — hand-written stand-in for what the facegen tool
# should emit from __exports__.h's pm_wasmmod_pyexport_export_py*-routed symbols (see
# SOURCETREE.md "Python face"). Native side still takes raw pointers/caps
# (see __exports__.h); the `mem`-shape marshaling wraps that into plain
# bytes in/out here, same as the taxonomy in "Py export face" read
# backwards. Nothing at runtime reads this file — type-checker sugar only.

def inflate(src: bytes, dst_cap: int) -> bytes: ...
def deflate(src: bytes, dst_cap: int, hist_scratch_len: int) -> bytes: ...
