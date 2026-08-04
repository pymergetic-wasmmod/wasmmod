# Build dotted-tree e2e packs → ../packs/
#
# Sibling layout (flat dirs, different-repo style):
#   test_a/  test_a_test_d/  test_a_test_b_test_c/
# Nested layout (monorepo; pack.toml markers walked by pack-tree):
#   nested/test_a2/{test_b2/test_c2,test_d2}/

.PHONY: all clean sibling nested

all: sibling nested

sibling:
	$(MAKE) -C test_a
	$(MAKE) -C test_a_test_d
	$(MAKE) -C test_a_test_b_test_c

nested:
	$(MAKE) -C nested

clean:
	$(MAKE) -C test_a clean
	$(MAKE) -C test_a_test_d clean
	$(MAKE) -C test_a_test_b_test_c clean
	$(MAKE) -C nested clean
