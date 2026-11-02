# Polyend Tracker Community Edition (CE) Centralized Version Configuration

# MT: Modular-Trackular build of CE (Config shows "1.0.0 MT"; files TrackerPlus_MT_*.ptf)
EDITION_NAME := MT
VER_ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_TAG    ?= $(shell git -C "$(VER_ROOT_DIR)" rev-parse --short=7 HEAD 2>/dev/null || date +"%Y%m%d")

# Original Tracker (MK66)
ORIGINAL_VER_MAJOR := 1
ORIGINAL_VER_MINOR := 0
ORIGINAL_VER_PATCH := 0
ORIGINAL_VER_BETA  := 255

# Tracker+ (IMX)
PLUS_VER_MAJOR     := 1
PLUS_VER_MINOR     := 0
PLUS_VER_PATCH     := 0
PLUS_VER_BETA      := 255

# Tracker Mini (IMX)
MINI_VER_MAJOR     := 1
MINI_VER_MINOR     := 0
MINI_VER_PATCH     := 0
MINI_VER_BETA      := 255

ifeq ($(PRODUCT), MINI)
  FW_VER_1    := $(MINI_VER_MAJOR)
  FW_VER_2    := $(MINI_VER_MINOR)
  FW_VER_3    := $(MINI_VER_PATCH)
  FW_BETA     := $(MINI_VER_BETA)
else ifeq ($(PRODUCT), PLUS)
  FW_VER_1    := $(PLUS_VER_MAJOR)
  FW_VER_2    := $(PLUS_VER_MINOR)
  FW_VER_3    := $(PLUS_VER_PATCH)
  FW_BETA     := $(PLUS_VER_BETA)
else
  FW_VER_1    := $(ORIGINAL_VER_MAJOR)
  FW_VER_2    := $(ORIGINAL_VER_MINOR)
  FW_VER_3    := $(ORIGINAL_VER_PATCH)
  FW_BETA     := $(ORIGINAL_VER_BETA)
endif

FW_VERSION_DEFINES := -DFW_EDITION_NAME=\"$(EDITION_NAME)\" \
                      -DFW_VER_MAJOR=$(FW_VER_1) \
                      -DFW_VER_MINOR=$(FW_VER_2) \
                      -DFW_VER_PATCH=$(FW_VER_3) \
                      -DFW_VER_BETA=$(FW_BETA)

BUILD_ID_STR           := $(BUILD_TAG)
DEFAULT_TARGET_POSTFIX := $(EDITION_NAME)_$(FW_VER_1).$(FW_VER_2).$(FW_VER_3)
