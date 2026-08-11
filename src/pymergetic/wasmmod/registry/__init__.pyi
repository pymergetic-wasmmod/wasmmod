# pymergetic.wasmmod.registry — hand-written stand-in for what the
# facegen tool should emit from __exports__.h's pm_wasmmod_pyexport_export_py*-routed
# symbols (see SOURCETREE.md "Python face"). None of them are here: this
# is internal plumbing Python reaches through `sys.modules`/`import`, not
# a surface Python code calls directly. Generated (near-)empty on
# purpose, not a missing face.
#
# Nothing at runtime reads this file — type-checker sugar only.
