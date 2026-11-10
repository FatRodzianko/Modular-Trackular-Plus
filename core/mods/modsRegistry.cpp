// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The table of mods in this build, and the walks over it. A new mod adds its descriptor
// here under its MT_HAS_<NAME> flag, at the place in the order its events need.

#include "modsBuild.h"

#if MT_MODS

#include "modsRegistry.h"

#include "colorsong.h"
#include "copypattern.h"
#include "humanize.h"
#include "masterfx.h"
#include "modsProject.h"
#include "songloop.h"
#include "swing.h"
#include "synthfx/synthfxMod.h"
#include "synths/synthsMod.h"
#include "tracklen.h"

namespace mods
{
namespace registry
{

constexpr std::array<ModDescriptor, kBuiltModCount> kMods{ {
#if MT_HAS_HUMANIZE
  {
    .id = Mod::humanize,
    .name = "Humanize",
    .record = humanize::kRecord,
    .recordVersion = humanize::kRecordVersion,
    .init = nullptr,
    .load = humanize::load,
    .apply = nullptr,
    .reset = nullptr,
    .storeIfEdited = humanize::storeIfEdited,
    .stored = nullptr,
  },
#endif
#if MT_HAS_SWING
  {
    .id = Mod::swing,
    .name = "Global Swing",
    .record = swing::kRecord,
    .recordVersion = swing::kRecordVersion,
    .init = nullptr,
    .load = swing::load,
    .apply = nullptr,
    .reset = nullptr,
    .storeIfEdited = swing::storeIfEdited,
    .stored = swing::amountEdited,
  },
#endif
#if MT_HAS_MASTERFX
  {
    .id = Mod::masterfx,
    .name = "Master FX",
    .record = masterfx::kRecord,
    .recordVersion = masterfx::kRecordVersion,
    .init = nullptr,
    .load = masterfx::load,
    .apply = masterfx::apply,
    .reset = nullptr,
    .storeIfEdited = masterfx::storeIfEdited,
    .stored = masterfx::apply,
  },
#endif
#if MT_HAS_COLORSONG
  {
    .id = Mod::colorsong,
    .name = "Color Song",
    .record = colorsong::kRecord,
    .recordVersion = colorsong::kRecordVersion,
    .init = colorsong::init,
    .load = nullptr,
    .apply = nullptr,
    .reset = colorsong::reset,
    .storeIfEdited = nullptr,
    .stored = nullptr,
  },
#endif
#if MT_HAS_COPYPATTERN
  {
    .id = Mod::copypattern,
    .name = "Copy to New",
    .record = copypattern::kRecord,
    .recordVersion = copypattern::kRecordVersion,
    .init = copypattern::init,
    .load = nullptr,
    .apply = nullptr,
    .reset = copypattern::cancel, // a copy started in the last project
    .storeIfEdited = nullptr,
    .stored = nullptr,
  },
#endif
#if MT_HAS_TRACKLEN
  {
    .id = Mod::tracklen,
    .name = "Track Length",
    .record = tracklen::kRecord,
    .recordVersion = tracklen::kRecordVersion,
    .init = tracklen::init,
    .load = tracklen::load,
    .apply = nullptr,
    .reset = nullptr,
    .storeIfEdited = nullptr,
    .stored = nullptr,
  },
#endif
#if MT_HAS_SONGLOOP
  {
    .id = Mod::songloop,
    .name = "Song Loop",
    .record = songloop::kRecord,
    .recordVersion = songloop::kRecordVersion,
    .init = songloop::init,
    .load = nullptr,
    .apply = songloop::apply,    // the status bar's glyphs on, or back to CE's
    .reset = songloop::reset,    // a selection or loop from the last project
    .storeIfEdited = nullptr,
    .stored = nullptr,
  },
#endif
#if MT_HAS_SYNTHS
  {
    .id = Mod::synths,
    .name = "Synths",            // WAVE and PHASE: the switch only shows them in the picker
    .record = synths::kRecord,
    .recordVersion = synths::kRecordVersion,
    .init = nullptr,
    .load = nullptr,
    .apply = nullptr,
    .reset = nullptr,
    .storeIfEdited = nullptr,
    .stored = nullptr,
  },
#endif
#if MT_HAS_SYNTHFX
  {
    .id = Mod::synthfx,
    .name = "Synth FX",          // the audio interrupt reads the switch: Off bypasses the insert
    .record = synthfx::kRecord,
    .recordVersion = synthfx::kRecordVersion,
    .init = nullptr,
    .load = nullptr,
    .apply = nullptr,
    .reset = nullptr,
    .storeIfEdited = nullptr,
    .stored = nullptr,
  },
#endif
} };

namespace
{

static_assert(kModCount <= 32, "the check below keeps one bit per id");

// Each entry filled in (a missing one would be zeros: Color Song's id with no name), each
// id once, and each record its id + 1, as mods.bin has them.
constexpr bool wellFormed()
{
    uint32_t seen = 0;
    for (const ModDescriptor& mod : kMods)
    {
        const size_t id = static_cast<size_t>(mod.id);
        const uint32_t bit = 1u << id;
        if (id >= kModCount || mod.name == nullptr || static_cast<size_t>(mod.record) != id + 1 || (seen & bit) != 0)
        {
            return false;
        }
        seen |= bit;
    }
    return true;
}

static_assert(wellFormed(), "registry: an entry is missing, an id repeats or a record isn't id + 1");

} // namespace

void init()
{
    for (const ModDescriptor& mod : kMods)
    {
        if (mod.init != nullptr)
        {
            mod.init();
        }
    }
}

void workspaceLoaded(const Store& projectStore)
{
    for (const ModDescriptor& mod : kMods)
    {
        if (mod.load != nullptr)
        {
            mod.load(projectStore);
        }
        if (mod.apply != nullptr)
        {
            mod.apply();
        }
        if (mod.reset != nullptr)
        {
            mod.reset();
        }
    }
}

void settingEdited(Store& projectStore)
{
    for (const ModDescriptor& mod : kMods)
    {
        if (mod.storeIfEdited != nullptr && mod.storeIfEdited(projectStore))
        {
            project::markChanged();
            if (mod.stored != nullptr)
            {
                mod.stored();
            }
        }
    }
}

} // namespace registry
} // namespace mods

#endif // MT_MODS
