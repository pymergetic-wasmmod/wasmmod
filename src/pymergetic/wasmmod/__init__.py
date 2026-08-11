"""pymergetic.wasmmod — complete, single-distribution package: everything
under this path is defined right here in the wasmmod project, nothing
external ever contributes a child. That's what makes this a **regular**
package (`impl = "py"`, real `__init__.py`) rather than `pep420 = true`
like `pymergetic`/`pymergetic.util` — those two genuinely can gain
siblings from other distributions; this one can't and shouldn't pretend
to.
"""
