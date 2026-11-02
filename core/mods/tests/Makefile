# Modular-Trackular mods: host tests (Linux or WSL, any recent g++).
#   make -C core/mods/tests

CXX      ?= g++
CXXFLAGS := -std=gnu++20 -Wall -Wextra -Werror -g -DMT_HOST_TEST -I..
SRCS     := ../modsStore.cpp ../modsConfig.cpp ../modsProject.cpp test_mods.cpp
BUILD    := build

.PHONY: all clean

all: $(BUILD)/test_mods
	./$(BUILD)/test_mods

$(BUILD)/test_mods: $(SRCS) $(wildcard ../*.h)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -rf $(BUILD)
