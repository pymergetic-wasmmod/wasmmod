# Compat shim — prefer ports/micropython/micropython.mk
include $(dir $(lastword $(MAKEFILE_LIST)))ports/micropython/micropython.mk
