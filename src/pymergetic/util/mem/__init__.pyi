# pymergetic.util.mem — hand-written stand-in for what the facegen tool
# should emit by reading which of __exports__.h's symbols are routed
# through pm_wasmmod_pyexport_export_py* (see SOURCETREE.md "Python face"). None of
# them are here: every export below is a raw pointer/arena handle with no
# Python marshaling at all, so the generated Python surface is correctly
# (near-)empty — this module is native-only, not a broken/missing face.
#
# Nothing at runtime reads this file; it exists purely so an editor's
# type-checker knows `pymergetic.util.mem` is a real module even though
# there is no `__init__.py` to read hints from.
