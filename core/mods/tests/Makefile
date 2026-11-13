# Modular-Trackular mods: host tests (Linux or WSL, any recent g++).
#   make -C core/mods/tests

CXX      ?= g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DMT_HOST_TEST -I..
COMMON   := ../modsStore.cpp ../modsSwitches.cpp ../modsProject.cpp ../humanize.cpp ../swing.cpp ../colorsong.cpp ../masterfx.cpp ../tracklen.cpp ../copypattern.cpp ../songloop.cpp ../synthfx/synthfxSettings.cpp
BUILD    := build
TESTS    := test_mods test_registry test_registry_partial test_humanize test_swing test_colorsong test_masterfx test_tracklen test_copypattern test_songloop test_synthfx test_synthfx_plugins test_cardguard
# The registry again with some mods left out of the build.
PARTIAL  := -DMT_HAS_SWING=0 -DMT_HAS_COLORSONG=0

.PHONY: all clean

all: $(addprefix $(BUILD)/,$(TESTS))
	@for t in $(TESTS); do echo "$$t:"; ./$(BUILD)/$$t || exit 1; done

$(BUILD)/test_mods: $(COMMON) ../modsConfig.cpp test_mods.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_registry: $(COMMON) ../modsRegistry.cpp test_registry.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_registry_partial: $(COMMON) ../modsRegistry.cpp test_registry.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(PARTIAL) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_humanize: $(COMMON) test_humanize.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_swing: $(COMMON) test_swing.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_colorsong: $(COMMON) test_colorsong.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_masterfx: $(COMMON) test_masterfx.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_tracklen: $(COMMON) test_tracklen.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_cardguard: test_cardguard.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_copypattern: $(COMMON) test_copypattern.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_songloop: $(COMMON) test_songloop.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_synthfx: $(COMMON) test_synthfx.cpp $(wildcard ../*.h) $(wildcard ../synthfx/*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_synthfx_plugins: ../synthfx/synthfxPlugins.cpp ../synthfx/synthfxPack.cpp test_synthfx_plugins.cpp ../modsFileIo.h $(wildcard ../synthfx/*.h) ../synthfx/synthfxPack.inc
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

clean:
	rm -rf $(BUILD)
