// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the mod registry: every mod in the build has exactly one descriptor, ids
// are unique, records are the mods' own, and the walks call the mods in the order mods.cpp
// called them before the registry. The Makefile builds this twice: every mod in, and some
// left out with their MT_HAS_<NAME> flags.

#include "modsRegistry.h"

#include "colorsong.h"
#include "copypattern.h"
#include "humanize.h"
#include "masterfx.h"
#include "modsProject.h"
#include "modsStore.h"
#include "songloop.h"
#include "swing.h"
#include "synths/synthsMod.h"
#include "tracklen.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mods;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const char* name)
{
    checks++;
    if (!condition)
    {
        failures++;
        printf("FAIL %s\n", name);
    }
}

std::vector<std::string> calls;

} // namespace

// The firmware-only calls the table points at. On the host they log, so the walks' order
// can be checked.
namespace mods
{
#if MT_HAS_MASTERFX
namespace masterfx
{
void apply()
{
    calls.push_back("masterfx::apply");
}
} // namespace masterfx
#endif
#if MT_HAS_SWING
namespace swing
{
void amountEdited()
{
    calls.push_back("swing::amountEdited");
}
} // namespace swing
#endif
#if MT_HAS_SONGLOOP
namespace songloop
{
void apply()
{
    calls.push_back("songloop::apply");
}
} // namespace songloop
#endif
} // namespace mods

namespace
{

// The mods this build has, from their flags alone, in the order of the table.
struct Expected
{
    Mod id;
    uint8_t record;
    uint8_t recordVersion;
    void (*init)();
    void (*load)(const Store&);
    void (*apply)();
    void (*reset)();
    bool (*storeIfEdited)(Store&);
    void (*stored)();
};

const std::vector<Expected> kExpected = {
#if MT_HAS_HUMANIZE
    { Mod::humanize, humanize::kRecord, humanize::kRecordVersion, nullptr, humanize::load, nullptr, nullptr,
      humanize::storeIfEdited, nullptr },
#endif
#if MT_HAS_SWING
    { Mod::swing, swing::kRecord, swing::kRecordVersion, nullptr, swing::load, nullptr, nullptr,
      swing::storeIfEdited, swing::amountEdited },
#endif
#if MT_HAS_MASTERFX
    { Mod::masterfx, masterfx::kRecord, masterfx::kRecordVersion, nullptr, masterfx::load, masterfx::apply,
      nullptr, masterfx::storeIfEdited, masterfx::apply },
#endif
#if MT_HAS_COLORSONG
    { Mod::colorsong, colorsong::kRecord, colorsong::kRecordVersion, colorsong::init, nullptr, nullptr,
      colorsong::reset, nullptr, nullptr },
#endif
#if MT_HAS_COPYPATTERN
    { Mod::copypattern, copypattern::kRecord, copypattern::kRecordVersion, copypattern::init, nullptr, nullptr,
      copypattern::cancel, nullptr, nullptr },
#endif
#if MT_HAS_TRACKLEN
    { Mod::tracklen, tracklen::kRecord, tracklen::kRecordVersion, tracklen::init, tracklen::load, nullptr,
      nullptr, nullptr, nullptr },
#endif
#if MT_HAS_SONGLOOP
    { Mod::songloop, songloop::kRecord, songloop::kRecordVersion, songloop::init, nullptr, songloop::apply,
      songloop::reset, nullptr, nullptr },
#endif
#if MT_HAS_SYNTHS
    { Mod::synths, synths::kRecord, synths::kRecordVersion, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr },
#endif
};

bool built(Mod id)
{
    switch (id)
    {
    case Mod::colorsong: return MT_HAS_COLORSONG != 0;
    case Mod::copypattern: return MT_HAS_COPYPATTERN != 0;
    case Mod::humanize: return MT_HAS_HUMANIZE != 0;
    case Mod::masterfx: return MT_HAS_MASTERFX != 0;
    case Mod::swing: return MT_HAS_SWING != 0;
    case Mod::tracklen: return MT_HAS_TRACKLEN != 0;
    case Mod::songloop: return MT_HAS_SONGLOOP != 0;
    case Mod::synths: return MT_HAS_SYNTHS != 0;
    default: return false; // not ported, or retired
    }
}

void testOneDescriptorPerBuiltMod()
{
    check(registry::kMods.size() == kBuiltModCount, "table size is kBuiltModCount");

    size_t builtCount = 0;
    for (size_t id = 0; id < kModCount; id++)
    {
        const Mod mod = static_cast<Mod>(id);
        size_t entries = 0;
        for (const ModDescriptor& d : registry::kMods)
        {
            entries += d.id == mod ? 1 : 0;
        }
        char name[64];
        snprintf(name, sizeof(name), "mod %zu: %s", id, built(mod) ? "one descriptor" : "no descriptor");
        check(entries == (built(mod) ? 1u : 0u), name);
        builtCount += built(mod) ? 1 : 0;
    }
    check(builtCount == kBuiltModCount, "kBuiltModCount counts the built mods");

    for (size_t i = 0; i < registry::kMods.size(); i++)
    {
        for (size_t j = i + 1; j < registry::kMods.size(); j++)
        {
            check(registry::kMods[i].id != registry::kMods[j].id, "ids unique");
            check(registry::kMods[i].record != registry::kMods[j].record, "records unique");
        }
    }
}

void testDescriptors()
{
    check(registry::kMods.size() == kExpected.size(), "as many descriptors as expected");
    for (size_t i = 0; i < registry::kMods.size() && i < kExpected.size(); i++)
    {
        const ModDescriptor& d = registry::kMods[i];
        const Expected& e = kExpected[i];
        check(d.id == e.id, "order");
        check(d.name != nullptr && d.name[0] != 0, "name");
        check(d.record == static_cast<uint8_t>(d.id) + 1, "record is id + 1");
        check(d.record != project::kImportsRecord, "record isn't the imports record");
        check(d.record == e.record && d.recordVersion == e.recordVersion, "the mod's own record");
        check(d.init == e.init, "init");
        check(d.load == e.load, "load");
        check(d.apply == e.apply, "apply");
        check(d.reset == e.reset, "reset");
        check(d.storeIfEdited == e.storeIfEdited, "storeIfEdited");
        check(d.stored == e.stored, "stored");
    }
}

uint8_t buffer[1024];
Store store(buffer, sizeof(buffer), project::kMagic);

void testWorkspaceLoaded()
{
    store.clear();
    calls.clear();
#if MT_HAS_SWING
    const uint8_t amount = 10;
    store.put(swing::kRecord, swing::kRecordVersion, &amount, 1);
    swing::settings.amount = 0;
#endif
    registry::workspaceLoaded(store);
#if MT_HAS_SWING
    check(swing::settings.amount == amount, "Workspace loaded: Swing's record read");
#endif
    std::vector<std::string> applied;
#if MT_HAS_MASTERFX
    applied.push_back("masterfx::apply");
#endif
#if MT_HAS_SONGLOOP
    applied.push_back("songloop::apply");
#endif
    check(calls == applied, "Workspace loaded: each mod's apply once, in the table's order, nothing else");
}

void testSettingEdited()
{
    store.clear();
    registry::workspaceLoaded(store); // every mod's settings back to its defaults
    calls.clear();
    project::setChangedCallback([] { calls.push_back("changed"); });

    registry::settingEdited(store);
    check(calls.empty(), "nothing edited: nothing stored or called");

#if MT_HAS_HUMANIZE
    humanize::settings.timingRange = 20;
#endif
#if MT_HAS_SWING
    swing::settings.amount = 5;
#endif
#if MT_HAS_MASTERFX
    masterfx::settings.reverb = 1;
#endif
    registry::settingEdited(store);

    std::vector<std::string> expected;
#if MT_HAS_HUMANIZE
    expected.push_back("changed");
#endif
#if MT_HAS_SWING
    expected.push_back("changed");
    expected.push_back("swing::amountEdited");
#endif
#if MT_HAS_MASTERFX
    expected.push_back("changed");
    expected.push_back("masterfx::apply");
#endif
    check(calls == expected, "edited: each stored, project marked, then the mod's own call, in order");

    calls.clear();
    registry::settingEdited(store);
    check(calls.empty(), "edited again with nothing new: nothing called");
    project::setChangedCallback(nullptr);
}

void testInit()
{
    // Color Song's, Copy to New's and Track Length's init only clear their state; this checks the walk
    // runs without the firmware.
    calls.clear();
    registry::init();
    check(calls.empty(), "init calls no firmware-only function");
}

} // namespace

int main()
{
    testOneDescriptorPerBuiltMod();
    testDescriptors();
    testInit();
    testWorkspaceLoaded();
    testSettingEdited();
    printf("%d checks, %d failures (%zu mods built)\n", checks, failures, kBuiltModCount);
    return failures == 0 ? 0 : 1;
}
