# Nested monorepo layout (pack.toml markers → separate .wasm via pack-tree).
#
#   test_a2/                 type=package  → test_a2.wasm
#     test_b2/               namespace only (no pack.toml)
#       test_c2/             type=package  → test_a2.test_b2.test_c2.wasm
#     test_d2/               type=package  → test_a2.test_d2.wasm
#
# Contrast: sibling flat dirs under examples/tree/test_a* (different-repo style).

include ../../find_top.mk
OUT_DIR = ../../packs

.PHONY: all clean

all:
	python3 $(WASMMOD) pack-tree test_a2 -o $(OUT_DIR)
	rm -f $(OUT_DIR)/test_a2.wasm.zlib \
		$(OUT_DIR)/test_a2.test_d2.wasm.zlib \
		$(OUT_DIR)/test_a2.test_b2.test_c2.wasm.zlib

clean:
	rm -f $(OUT_DIR)/test_a2.wasm $(OUT_DIR)/test_a2.wasm.zlib \
		$(OUT_DIR)/test_a2.test_d2.wasm $(OUT_DIR)/test_a2.test_d2.wasm.zlib \
		$(OUT_DIR)/test_a2.test_b2.test_c2.wasm $(OUT_DIR)/test_a2.test_b2.test_c2.wasm.zlib
