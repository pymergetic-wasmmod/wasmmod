"""pymergetic.wasmmod.guest — µPy face of PM_MOD_BOOT_* (modguest.c)."""

from collections.abc import Callable
from typing import Optional

def PM_MOD_BOOT(
    fqn: str,
    init: Callable[[], int],
    deinit: Callable[[], None],
    ready: Optional[Callable[[], int]] = None,
) -> None: ...
def PM_MOD_BOOTDEP(mod: str, dep: str) -> None: ...
def PM_MOD_BOOT_CHILD(mod: str, child: str) -> None: ...
