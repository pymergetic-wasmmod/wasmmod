# This file is part of wasmmod, https://github.com/pymergetic/wasmmod
#
# Dotted module-tree e2e (unix):
#   Sibling: from test_a.test_b import test_c   (+ deps test_a.test_d)
#   Nested:  from test_a2.test_b2 import test_c2 (+ deps test_a2.test_d2)
#
# Offline (default): packs/ on wasm.path
# CDN: --cdn
# Suite: --suite=a | a2 | both (default both)

import sys

try:
    import wasm  # type: ignore[import-not-found]
except ImportError:
    print("FAIL: wasm module missing — rebuild with MICROPY_PY_WASM=1")
    sys.exit(1)

try:
    HERE = __file__.rsplit("/", 1)[0]
    if HERE == __file__ or not HERE:
        HERE = "."
except AttributeError:
    HERE = "."

CDN = False
CDN_URL = "http://127.0.0.1:8000/cdn"
TOKEN = None
SUITE = "both"
for a in sys.argv[1:]:
    if a == "--cdn":
        CDN = True
    elif a.startswith("--cdn-url="):
        CDN = True
        CDN_URL = a.split("=", 1)[1]
    elif a.startswith("--token="):
        TOKEN = a.split("=", 1)[1]
    elif a.startswith("--suite="):
        SUITE = a.split("=", 1)[1]


def tree_dump(prefix):
    keys = sorted(k for k in sys.modules if str(k).startswith(prefix))
    print("module tree (sys.modules):")
    for k in keys:
        print(" ", k)
    return keys


print("wasm", getattr(wasm, "version", "?"))

if CDN:
    print("wasm.cdn", CDN_URL)
    name = wasm.cdn(CDN_URL, TOKEN) if TOKEN else wasm.cdn(CDN_URL)
    print("driver", name)
    wasm.install_hook()
else:
    packs = HERE + "/packs"
    print("wasm.path.append", packs)
    wasm.path.append(packs)
    wasm.install_hook()

if SUITE in ("a", "both"):
    print("--- suite test_a (sibling flat packs) ---")
    import test_a  # type: ignore[import-not-found]

    ping = test_a.a_ping()
    print("test_a.a_ping() →", ping, "(want 11)")
    if ping != 11:
        raise SystemExit(1)
    from test_a.test_b import test_c  # type: ignore[import-not-found]

    got = test_c.c_answer()
    print("test_c.c_answer() →", got, "(want 42)")
    if got != 42:
        raise SystemExit(1)
    import test_a.test_d as test_d  # type: ignore[import-not-found]

    dv = test_d.d_value()
    print("test_a.test_d.d_value() →", dv, "(want 37)")
    if dv != 37:
        raise SystemExit(1)
    tree_dump("test_a")

if SUITE in ("a2", "both"):
    print("--- suite test_a2 (nested monorepo pack-tree) ---")
    import test_a2  # type: ignore[import-not-found]

    ping = test_a2.a2_ping()
    print("test_a2.a2_ping() →", ping, "(want 21)")
    if ping != 21:
        raise SystemExit(1)
    from test_a2.test_b2 import test_c2  # type: ignore[import-not-found]

    got = test_c2.c2_answer()
    print("test_c2.c2_answer() →", got, "(want 42)")
    if got != 42:
        raise SystemExit(1)
    import test_a2.test_d2 as test_d2  # type: ignore[import-not-found]

    dv = test_d2.d2_value()
    print("test_a2.test_d2.d2_value() →", dv, "(want 37)")
    if dv != 37:
        raise SystemExit(1)
    tree_dump("test_a2")

print("OK dotted tree e2e suite=%s" % SUITE)
