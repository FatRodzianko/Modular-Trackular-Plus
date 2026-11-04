# Polyend Tracker Community Edition (CE) - Top-Level Monorepo Makefile

ROOT_DIR      := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ifneq ($(wildcard $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-gcc),)
  TOOLCHAIN   ?= $(ROOT_DIR)/agt/10.3/bin/arm-none-eabi-
else
  TOOLCHAIN   ?= arm-none-eabi-
endif

NPROC         ?= $(shell nproc 2>/dev/null || echo 4)
CORE_DIR      := $(ROOT_DIR)/core
MK66_DIR      := $(ROOT_DIR)/platforms/mk66
IMX_DIR       := $(ROOT_DIR)/platforms/imx
SYNTH_DIR     := $(ROOT_DIR)/synth
UTIL_DIR      := $(ROOT_DIR)/util
OUT_DIR       := $(ROOT_DIR)/build

include $(ROOT_DIR)/version.mk

ORIGINAL_POSTFIX ?= $(EDITION_NAME)_$(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH)
PLUS_POSTFIX     ?= $(EDITION_NAME)_$(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH)
MINI_POSTFIX     ?= $(EDITION_NAME)_$(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH)

.DEFAULT_GOAL := all

.PHONY: all original plus mini ptfx clean version bump-patch bump-minor

all: original plus mini

original:
	@echo "=================================================================="
	@echo " Building Original Tracker (MK66) - $(EDITION_NAME) $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(MK66_DIR)/build/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=MK66 PRODUCT=ORIGINAL \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(MK66_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		PRODUCT=ORIGINAL \
		TARGET_POSTFIX="$(ORIGINAL_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(OUT_DIR)"
	@cp "$(MK66_DIR)/build/PolyendTracker_$(ORIGINAL_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/PolyendTracker_$(ORIGINAL_POSTFIX).ptf"

plus:
	@echo "=================================================================="
	@echo " Building Tracker+ (IMX) - $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-PLUS/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=PLUS \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=PLUS \
		TARGET_POSTFIX="$(PLUS_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(OUT_DIR)"
	@cp "$(IMX_DIR)/build-PLUS/TrackerPlus_$(PLUS_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/TrackerPlus_$(PLUS_POSTFIX).ptf"

mini:
	@echo "=================================================================="
	@echo " Building Tracker Mini (IMX) - $(EDITION_NAME) $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH) ($(BUILD_TAG))"
	@echo "=================================================================="
	@rm -f "$(IMX_DIR)/build-MINI/obj/source/PlatformImpl.o"
	$(MAKE) -C "$(CORE_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		PLATFORM=IMX PRODUCT=MINI \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)"
	$(MAKE) -C "$(IMX_DIR)" -j$(NPROC) build \
		TOOLCHAIN="$(TOOLCHAIN)" \
		TRACKER_DIR="$(CORE_DIR)" \
		POLYUTIL_DIR="$(UTIL_DIR)" \
		POLYSYNTH_DIR="$(SYNTH_DIR)" \
		PRODUCT=MINI \
		TARGET_POSTFIX="$(MINI_POSTFIX)" \
		BUILD_ID_STR="$(BUILD_ID_STR)"
	@mkdir -p "$(OUT_DIR)"
	@cp "$(IMX_DIR)/build-MINI/TrackerMini_$(MINI_POSTFIX).ptf" "$(OUT_DIR)/"
	@echo "-> Output: $(OUT_DIR)/TrackerMini_$(MINI_POSTFIX).ptf"

ptfx:
	@mkdir -p "$(ROOT_DIR)/editor-fx-sdk/build/Effects"
	@for src in "$(ROOT_DIR)"/editor-fx-sdk/effects/*.c; do \
		base=$$(basename "$$src" .c); \
		python3 "$(ROOT_DIR)/editor-fx-sdk/ptfx_builder.py" "$$src" -o "$(ROOT_DIR)/editor-fx-sdk/build/Effects/$$base.ptfx" --toolchain "$(TOOLCHAIN)"; \
	done

clean:
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=MK66 PRODUCT=ORIGINAL
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=PLUS
	$(MAKE) -C "$(CORE_DIR)" clean PLATFORM=IMX PRODUCT=MINI
	$(MAKE) -C "$(MK66_DIR)" clean PRODUCT=ORIGINAL
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=PLUS
	$(MAKE) -C "$(IMX_DIR)" clean PRODUCT=MINI
	rm -rf "$(OUT_DIR)" "$(ROOT_DIR)/editor-fx-sdk/build"

version:
	@echo "Edition      : $(EDITION_NAME) (Build $(BUILD_TAG))"
	@echo "Original (MK66): $(EDITION_NAME) $(ORIGINAL_VER_MAJOR).$(ORIGINAL_VER_MINOR).$(ORIGINAL_VER_PATCH) -> PolyendTracker_$(ORIGINAL_POSTFIX).ptf"
	@echo "Tracker+ (IMX) : $(EDITION_NAME) $(PLUS_VER_MAJOR).$(PLUS_VER_MINOR).$(PLUS_VER_PATCH) -> TrackerPlus_$(PLUS_POSTFIX).ptf"
	@echo "Mini     (IMX) : $(EDITION_NAME) $(MINI_VER_MAJOR).$(MINI_VER_MINOR).$(MINI_VER_PATCH) -> TrackerMini_$(MINI_POSTFIX).ptf"

TARGET ?= all

bump-patch:
	@python3 -c 'import re, sys; t="$(TARGET)".upper(); p="version.mk"; s=open(p).read(); \
	targets = ["ORIGINAL", "PLUS", "MINI"] if t == "ALL" else [t]; \
	for k in targets: \
	    m = re.search(rf"{k}_VER_PATCH\s*:=\s*(\d+)", s); \
	    assert m, f"Unknown TARGET {t}"; \
	    v = int(m.group(1)) + 1; \
	    s = re.sub(rf"{k}_VER_PATCH\s*:=\s*\d+", f"{k}_VER_PATCH := {v}", s); \
	open(p, "w").write(s); print(f"Bumped patch version for {targets}")'

bump-minor:
	@python3 -c 'import re, sys; t="$(TARGET)".upper(); p="version.mk"; s=open(p).read(); \
	targets = ["ORIGINAL", "PLUS", "MINI"] if t == "ALL" else [t]; \
	for k in targets: \
	    m = re.search(rf"{k}_VER_MINOR\s*:=\s*(\d+)", s); \
	    assert m, f"Unknown TARGET {t}"; \
	    v = int(m.group(1)) + 1; \
	    s = re.sub(rf"{k}_VER_MINOR\s*:=\s*\d+", f"{k}_VER_MINOR := {v}", s); \
	    s = re.sub(rf"{k}_VER_PATCH\s*:=\s*\d+", f"{k}_VER_PATCH := 0", s); \
	open(p, "w").write(s); print(f"Bumped minor version for {targets} (patch reset to 0)")'
