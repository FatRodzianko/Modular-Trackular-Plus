# Modular-Trackular mods: host tests (Linux or WSL, any recent g++).
#   make -C core/mods/tests

CXX      ?= g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -g -DMT_HOST_TEST -I..
COMMON   := ../modsStore.cpp ../modsSwitches.cpp
BUILD    := build
TESTS    := test_mods test_humanize

.PHONY: all clean

all: $(addprefix $(BUILD)/,$(TESTS))
	@for t in $(TESTS); do echo "$$t:"; ./$(BUILD)/$$t || exit 1; done

$(BUILD)/test_mods: $(COMMON) ../modsConfig.cpp ../modsProject.cpp ../humanize.cpp test_mods.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

$(BUILD)/test_humanize: $(COMMON) ../modsProject.cpp ../humanize.cpp test_humanize.cpp $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(filter %.cpp,$^)

clean:
	rm -rf $(BUILD)
