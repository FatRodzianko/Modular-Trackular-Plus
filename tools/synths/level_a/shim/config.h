// Modular-Trackular mods. SPDX-License-Identifier: MIT
// Level A only: what synthsFx.cpp takes from CE's config.h (the same section as CE's).
#pragma once
#define AUDIO_CORE_RAM __attribute__((section(".audio_ram_functions"), used))
