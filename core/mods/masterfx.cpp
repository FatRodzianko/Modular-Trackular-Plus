// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "masterfx.h"

#if MT_HAS_MASTERFX

#include "masterfx/masterfxPlugins.h"
#include "modsProject.h"
#include "plugins/pluginKit.h"

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include "Platform.h"
#include "Tracker.h"
#include "masterfxDsp.h"
#include "configEditor/configMenuDefs.h"
#include "mtAudioEngine.h"
#include <cr_section_macros.h>
#define MFX_SDRAM __NOINIT(EXTERNAL_RAM)
#else
#define MFX_SDRAM
#endif

namespace mods
{
namespace masterfx
{

const char* const kV1Reverbs[kV1ReverbCount] = {
    "", "Freeverb.ptmfx", "Plate.ptmfx", "Spring.ptmfx", "Shimmer.ptmfx", "Gated.ptmfx", "Reverse.ptmfx",
};
const char* const kV1Delays[kV1DelayCount] = { "", "Tape.ptmfx", "BBD.ptmfx" };

Settings settings;

namespace
{

namespace pl = plugins;
using pluginkit::copyText;
using pluginkit::sameText;

static_assert(kReverb == pl::kReverb && kDelay == pl::kDelay && kDynamics == pl::kDynamics && kSlots == pl::kSlots);
static_assert(kFileSize == pl::kFileNameSize && kOwnKnobs == PTMFX_MAX_PARAMS);
static_assert(sizeof(Settings) == 168, "the record's size");

constexpr size_t kV1Size = 2;

// masterfx.bin from the 1.2.0 patch: magic, reverb, delay, two zero bytes, a checksum over
// those four. Delay 3 was "Gated" before it moved to the reverbs: that plays the stock delay
// and keeps the reverb.
constexpr char kSideFileName[] = "masterfx.bin";
constexpr char kSideMagic[] = "HMF1";
constexpr size_t kSideSize = 12;
constexpr size_t kSideData = 4;
constexpr uint8_t kOldDelayCount = 4;

// A knob not set yet: the plugin's default, filled in when it is put in
inline float unset()
{
    return __builtin_nanf("");
}

void stock(SlotSetting& s)
{
    memset(&s, 0, sizeof(s));
    for (float& k : s.knob)
    {
        k = unset();
    }
}

Settings allStock()
{
    Settings s;
    for (SlotSetting& slot : s.slot)
    {
        stock(slot);
    }
    return s;
}

// Version 1's two indices: a value out of range plays Stock, and the other one stays.
Settings fromV1(uint8_t reverb, uint8_t delay)
{
    Settings s = allStock();
    copyText(s.slot[kReverb].file, kFileSize, kV1Reverbs[reverb < kV1ReverbCount ? reverb : kStock]);
    copyText(s.slot[kDelay].file, kFileSize, kV1Delays[delay < kV1DelayCount ? delay : kStock]);
    return s;
}

// The record as settings, Stock for a slot whose name isn't ended
Settings fromRecord(const Store& store)
{
    uint8_t version = 0;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, &version, &length);
    if (data == nullptr)
    {
        return allStock();
    }
    if (length == kV1Size)
    {
        return fromV1(data[0], data[1]);
    }
    if (length != sizeof(Settings))
    {
        return allStock();
    }
    Settings s;
    memcpy(&s, data, sizeof(s));
    for (SlotSetting& slot : s.slot)
    {
        if (!pluginkit::endedText(slot.file))
        {
            stock(slot);
        }
    }
    return s;
}

bool sameSettings(const Settings& a, const Settings& b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

uint32_t sideSum(const uint8_t* data, size_t length)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < length; i++)
    {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

uint32_t get32(const uint8_t* b)
{
    return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
}

// ---- the lists --------------------------------------------------------------------------

constexpr size_t kMaxChoices = 1 + pl::kCatalogSize + 1; // Stock, the plugins, Missing
constexpr char kMissing[] = "Missing: ";

// Main loop only, rebuilt before use: SDRAM in the firmware
MFX_SDRAM const char* names[kSlots][kMaxChoices];
MFX_SDRAM char missingText[kSlots][sizeof(kMissing) + kFileSize];
MFX_SDRAM uint8_t counts[kSlots];
MFX_SDRAM uint8_t chosen[kSlots];
bool built = false;                // zeroed at startup
bool picked[kSlots] = {};          // the user chose the slot's plugin since the last apply

void build()
{
    if (!built)
    {
        refreshChoices();
    }
}

void refreshMenus();

} // namespace

void load(const Store& store)
{
    settings = fromRecord(store);
    refreshChoices();
}

bool storeIfEdited(Store& store)
{
    if (sameSettings(fromRecord(store), settings))
    {
        return false;
    }
    Settings copy = settings;
    return store.put(kRecord, kRecordVersion, &copy, sizeof(copy));
}

bool importSideFile(FileIo& io, const char* projectFolder, Store& store)
{
    char path[project::kPathSize];
    const int n = snprintf(path, sizeof(path), "%s/%s", projectFolder, kSideFileName);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path))
    {
        return false;
    }

    uint8_t file[kSideSize];
    if (io.read(path, file, sizeof(file)) != kSideSize || memcmp(file, kSideMagic, 4) != 0 ||
        get32(file + 4 + kSideData) != sideSum(file + 4, kSideData))
    {
        return false;
    }

    if (file[4] >= kV1ReverbCount || file[5] >= kOldDelayCount)
    {
        return false;
    }
    const uint8_t delay = file[5] < kV1DelayCount ? file[5] : kStock;
    Settings s = fromV1(file[4], delay);
    return store.put(kRecord, kRecordVersion, &s, sizeof(s)) && (file[4] != kStock || delay != kStock);
}

// ---- the lists --------------------------------------------------------------------------

void refreshChoices()
{
    built = true;
    for (uint8_t s = 0; s < kSlots; s++)
    {
        const pl::Slot slot = static_cast<pl::Slot>(s);
        const char* file = settings.slot[s].file;
        size_t n = 0;
        names[s][n++] = "Stock";
        chosen[s] = 0;
        const size_t plugins = pl::slotCount(slot);
        for (size_t i = 0; i < plugins && n + 1 < kMaxChoices; i++)
        {
            const pl::CatalogEntry* e = pl::slotAt(slot, i);
            if (file[0] != 0 && sameText(e->file, file))
            {
                chosen[s] = static_cast<uint8_t>(n);
            }
            names[s][n++] = e->name;
        }
        if (file[0] != 0 && chosen[s] == 0) // the project's plugin isn't usable here
        {
            copyText(missingText[s], sizeof(missingText[s]), kMissing);
            pluginkit::appendText(missingText[s], sizeof(missingText[s]), file);
            chosen[s] = static_cast<uint8_t>(n);
            names[s][n++] = missingText[s];
        }
        counts[s] = static_cast<uint8_t>(n);
    }
    refreshMenus();
}

uint8_t choiceCount(uint8_t slot)
{
    build();
    return slot < kSlots ? counts[slot] : 1;
}

const char* const* choiceNames(uint8_t slot)
{
    build();
    return names[slot < kSlots ? slot : 0];
}

uint8_t choice(uint8_t slot)
{
    build();
    return slot < kSlots ? chosen[slot] : 0;
}

const char* choiceName(uint8_t slot)
{
    build();
    return slot < kSlots ? names[slot][chosen[slot]] : "";
}

bool missing(uint8_t slot)
{
    build();
    return slot < kSlots && settings.slot[slot].file[0] != 0 && names[slot][chosen[slot]] == missingText[slot];
}

void choose(uint8_t slot, uint8_t index)
{
    build();
    if (slot >= kSlots || index >= counts[slot] || index == chosen[slot])
    {
        return;
    }
    if (names[slot][index] == missingText[slot]) // the Missing line: it stays as it is
    {
        return;
    }
    SlotSetting& s = settings.slot[slot];
    stock(s);
    if (index > 0)
    {
        copyText(s.file, sizeof(s.file), pl::slotAt(static_cast<pl::Slot>(slot), index - 1u)->file);
    }
    picked[slot] = true;
    refreshChoices();
}

#ifdef MT_HOST_TEST

namespace
{
void refreshMenus() {}
} // namespace

#else

namespace
{

// The Config menu's two lists: the choice in the list and the lists themselves. Until the first
// refreshChoices (names is SDRAM, not zeroed) they show Stock alone.
const char* const kStockOnly[] = { "Stock" };
uint8_t menuChoice[2] = {};
void reverbMenuEdited();
void delayMenuEdited();
strItemTypeListTextWithAction reverbMenu{ &menuChoice[0], 1, kStockOnly, reverbMenuEdited };
strItemTypeListTextWithAction delayMenu{ &menuChoice[1], 1, kStockOnly, delayMenuEdited };

void refreshMenus()
{
    reverbMenu.ptrText = names[kReverb];
    delayMenu.ptrText = names[kDelay];
    reverbMenu.count = counts[kReverb];
    delayMenu.count = counts[kDelay];
    menuChoice[0] = chosen[kReverb];
    menuChoice[1] = chosen[kDelay];
}

void menuEdited(uint8_t slot, uint8_t index)
{
    choose(slot, index);
    refreshMenus();         // the Missing line can't be chosen: back to the project's
    projectSettingEdited(); // stored, then applied
}

void reverbMenuEdited()
{
    menuEdited(kReverb, menuChoice[0]);
}

void delayMenuEdited()
{
    menuEdited(kDelay, menuChoice[1]);
}

constexpr uint32_t kApplyWaitMs = 150;

bool hasStarted = false;
bool applyPending = false;
uint32_t applyAskedAt = 0;

uint32_t nowMs()
{
    return Tracker::getInstance()->getPlatform()->getTimer()->millis();
}

// CE's object for a slot: stop it running, then clear its buffers (which lets it run again)
void block(uint8_t slot)
{
    if (slot == kReverb)
    {
        engine.getMixerAndFx()->getPolyverb()->blockUpdate();
    }
    else if (slot == kDelay)
    {
        engine.getMixerAndFx()->getDelay()->blockUpdate();
    }
}

void clearAndRun(uint8_t slot)
{
    if (slot == kReverb)
    {
        engine.getMixerAndFx()->getPolyverb()->reset(); // calls reverbCleared, unblocks
    }
    else if (slot == kDelay)
    {
        engine.getMixerAndFx()->getDelay()->clear(); // calls delayCleared, unblocks
    }
}

void setActive(uint8_t slot, uint8_t on)
{
    if (slot == kReverb)
    {
        activeReverb = on;
    }
    else if (slot == kDelay)
    {
        activeDelay = on;
    }
}

// The slot's plugin as settings and the switch say. Safe mode skips a plugin the user didn't
// just pick.
void applySlot(uint8_t slot, bool isOn)
{
    const bool fromProject = !picked[slot];
    picked[slot] = false;
    const pl::Slot s = static_cast<pl::Slot>(slot);
    SlotSetting& want = settings.slot[slot];
    const bool wantPlugin = isOn && want.file[0] != 0;
    if (wantPlugin && pl::installed(s) && sameText(pl::file(s), want.file))
    {
        return;
    }
    pl::Status status = pl::Status::missing;
    if (wantPlugin)
    {
        status = pl::read(s, want.file, fromProject); // the card, while what plays keeps playing
    }
    if (status != pl::Status::ok)
    {
        if (pl::installed(s)) // CE's own plays: in buffers it lays out again itself
        {
            block(slot);
            setActive(slot, 0);
            pl::remove(s);
            clearAndRun(slot);
        }
        return;
    }
    block(slot);
    setActive(slot, 0);
    pl::install(s);
    for (uint32_t k = 0; k < kOwnKnobs; k++)
    {
        if (k >= pl::knobs(s))
        {
            continue;
        }
        if (pluginkit::finite(want.knob[k]))
        {
            pl::setKnob(s, k, want.knob[k]);
        }
        want.knob[k] = pl::knob(s, k); // on the plugin's steps, or its default
    }
    setActive(slot, 1);
    clearAndRun(slot);
}

} // namespace

void apply()
{
    applyPending = false;

    if (!hasStarted)
    {
        return;
    }
    const bool isOn = on(Mod::masterfx);
    applySlot(kReverb, isOn);
    applySlot(kDelay, isOn);
    refreshChoices();
}

const void* menuSetup(uint8_t slot)
{
    return slot == kDelay ? &delayMenu : &reverbMenu;
}

void started()
{
    if (!hasStarted)
    {
        hasStarted = true;
        apply();
    }
}

void applySoon()
{
    applyPending = true;
    applyAskedAt = nowMs();
}

void pollApply()
{
    if (applyPending && nowMs() - applyAskedAt >= kApplyWaitMs)
    {
        apply();
    }
}

void flushApply()
{
    if (applyPending)
    {
        apply();
    }
}

#endif // MT_HOST_TEST

} // namespace masterfx
} // namespace mods

#endif // MT_HAS_MASTERFX
