# Modular-Trackular mods: host tests (Linux or WSL, any recent g++).
#   make -C core/mods/tests

CXX      ?= g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -g -fsanitize=address,undefined -DMT_HOST_TEST -I..
COMMON   := ../modsStore.cpp ../modsSwitches.cpp ../modsProject.cpp ../humanize.cpp ../swing.cpp ../colorsong.cpp
BUILD    := build
TESTS    := test_mods test_humanize test_swing test_colorsong

.PHONY: all clean

all: $(addprefix $(BUILD)/,$(TESTS))
	@for t in $(TESTS); do echo "$$t:"; ./$(BUILD)/$$t || exit 1; done

$(BUILD)/test_mods: $(COMMON) ../modsConfig.cpp test_mods.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_humanize: $(COMMON) test_humanize.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_swing: $(COMMON) test_swing.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_colorsong: $(COMMON) test_colorsong.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

clean:
	rm -rf $(BUILD)
