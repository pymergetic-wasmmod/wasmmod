# pymergetic.wasmmod.loader — hand-written stand-in for what the facegen
# tool should emit. None of this module's exports are Python-facing
# (see SOURCETREE.md "Python face"): it's internal plumbing that turns
# `.wasm` bytes into registry entries, called by whatever embeds
# wasmmod (upy's import hook, later), not by Python code directly.
# Generated (near-)empty on purpose, not a missing face.
#
# Nothing at runtime reads this file — type-checker sugar only.
