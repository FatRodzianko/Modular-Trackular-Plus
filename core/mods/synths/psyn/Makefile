# Modular-Trackular mods. SPDX-License-Identifier: MIT
#
# WAVE and PHASE as .psyn plugins, built as CE builds its CHIP (synth-plugin-sdk/Makefile, whose
# compiler and linker flags this takes). The root Makefile's psyn target runs this and copies
# build/*.psyn to build/Synths with CE's engines.
#
# Each engine is one translation unit (its .cpp includes ../psyn/synthsSdk.inc): the SDK's
# bridge header defines the plugin's allocator and fmt::format, so it goes in once.
#
#   make -C core/mods/synths/psyn TOOLCHAIN=<prefix>

MT_PSYN_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
MT_REPO       := $(abspath $(MT_PSYN_DIR)/../../../..)
MT_BUILD      ?= $(MT_PSYN_DIR)/build

all-mt: $(MT_BUILD)/WAVE.psyn $(MT_BUILD)/PHASE.psyn

# CE's SDK flags, unchanged (its own targets are not built from here)
include $(MT_REPO)/synth-plugin-sdk/Makefile

MT_CXXFLAGS   := $(CXXFLAGS) -I$(MT_PSYN_DIR)/.. -I$(MT_PSYN_DIR)/../..
MT_DEPS       := $(SDK_DIR)/include/psyn/psyn_plugin.h $(SDK_DIR)/include/psyn/psyn_cpp_bridge.h \
                 $(SDK_DIR)/tools/psyn.ld $(MT_PSYN_DIR)/synthsSdk.inc $(wildcard $(MT_PSYN_DIR)/../*.h) \
                 $(MT_PSYN_DIR)/../../synthfx/fxDsp.h $(MT_PSYN_DIR)/../../synthfx/synthFx.h

$(MT_BUILD)/WAVE.elf: $(MT_PSYN_DIR)/waveSynth.cpp $(MT_DEPS)
	@mkdir -p $(MT_BUILD)
	$(CXX) $(MT_CXXFLAGS) $< $(LDFLAGS) -o $@

$(MT_BUILD)/PHASE.elf: $(MT_PSYN_DIR)/phaseSynth.cpp $(MT_DEPS)
	@mkdir -p $(MT_BUILD)
	$(CXX) $(MT_CXXFLAGS) $< $(LDFLAGS) -o $@

# The ids are the firmware's (../synthsMod.h, ../synthsSdk.h): projects and patches store them.
# On the Polyend Synth they would be dynamic (0).
$(MT_BUILD)/WAVE.psyn: $(MT_BUILD)/WAVE.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py --elf $< --out $@ \
		--code-name "WAVE" --display-name "WAVE" --patch-folder "WAVE" \
		--type-id-wave 0 --type-id-play-tracker 64 \
		--default-voices 4 --max-voices 8 --output-channels 2 --renders-aux 0 --has-sample-slots 0

$(MT_BUILD)/PHASE.psyn: $(MT_BUILD)/PHASE.elf $(SDK_DIR)/tools/psyn_builder.py
	$(PYTHON) $(SDK_DIR)/tools/psyn_builder.py --elf $< --out $@ \
		--code-name "PHASE" --display-name "PHASE" --patch-folder "PHASE" \
		--type-id-wave 0 --type-id-play-tracker 65 \
		--default-voices 4 --max-voices 8 --output-channels 2 --renders-aux 0 --has-sample-slots 0

clean-mt:
	rm -rf $(MT_BUILD)

.PHONY: all-mt clean-mt
