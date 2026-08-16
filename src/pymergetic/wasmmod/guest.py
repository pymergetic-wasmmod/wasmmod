"""pymergetic.wasmmod.guest — Python face of guest.h / guest.rs.

µPy: builtin ``modguest.c`` binds ``PM_MOD_BOOT`` / ``PM_MOD_BOOTDEP`` /
``PM_MOD_BOOT_CHILD`` to ``pm_mod_boot_add``. Same names as the C macros.
"""

__all__ = ["PM_MOD_BOOT", "PM_MOD_BOOTDEP", "PM_MOD_BOOT_CHILD"]
