// Modular-Trackular mods. SPDX-License-Identifier: MIT
// The Synth FX plugins' rig only: what synthfxPlugins.cpp takes from CE's config.h (the same section).
#pragma once
#define AUDIO_CORE_RAM __attribute__((section(".audio_ram_functions"), used))
