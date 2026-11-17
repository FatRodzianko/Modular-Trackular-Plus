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
#include "mods.h" // cardIsLent
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

// Read and written in the main loop only (the interrupt plays what apply() hands the plugins),
// so SDRAM, not DTCM: init() empties it at boot, as startup doesn't zero SDRAM.
MFX_SDRAM Settings settings;

namespace
{

namespace pl = plugins;
using pluginkit::copyText;
using pluginkit::sameText;

static_assert(kReverb == pl::kReverb && kDelay == pl::kDelay && kDynamics == pl::kDynamics && kSlots == pl::kSlots);
static_assert(kFileSize == pl::kFileNameSize && kOwnKnobs == PTMFX_MAX_PARAMS);
static_assert(sizeof(Settings) == 208, "the record's size");

constexpr size_t kV1Size = 2;
// Version 2 (masterfx-2.0): two own knobs a slot. 168 bytes: before the ducks were added (never
// released).
constexpr size_t kV2OwnKnobs = 2;
constexpr size_t kV2Slot = kFileSize + kV2OwnKnobs * sizeof(float);
constexpr size_t kV2Size = kSlots * kV2Slot + 4;
constexpr size_t kV2SlotsOnly = kSlots * kV2Slot;
static_assert(kV2Size == 172 && kV2SlotsOnly == 168);

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
    s.ducks = 0;
    s.reserved[0] = 0;
    s.reserved[1] = 0;
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

// Version 2's record: each slot's file and two knobs, then the ducks. The other knobs unset.
Settings fromV2(const uint8_t* data, size_t length)
{
    Settings s = allStock();
    for (size_t i = 0; i < kSlots; i++)
    {
        memcpy(s.slot[i].file, data + i * kV2Slot, kFileSize);
        memcpy(s.slot[i].knob, data + i * kV2Slot + kFileSize, kV2OwnKnobs * sizeof(float));
    }
    if (length == kV2Size)
    {
        memcpy(&s.ducks, data + kSlots * kV2Slot, sizeof(s.ducks));
    }
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
    Settings s = allStock();
    if (length == kV2Size || length == kV2SlotsOnly)
    {
        s = fromV2(data, length);
    }
    else if (length == sizeof(Settings))
    {
        memcpy(&s, data, length);
    }
    else
    {
        return s;
    }
    s.ducks &= kDuckAll;
    s.reserved[0] = 0;
    s.reserved[1] = 0;
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
// The list's line and the Master page's text under the column for a project's file that can't
// play: the column is 94 px wide, so the file's name is in the slot's page title only
constexpr char kMissingLine[] = "Missing";

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
void applyDucks(); // what plays ducks as settings say (the firmware's)

} // namespace

void init()
{
    memset(&settings, 0, sizeof(settings)); // Stock in every slot, no ducks: as a project with no record
}

void load(const Store& store)
{
    settings = fromRecord(store);
    pl::scan(); // a project's open (main loop): files copied to /MasterFX since the last list
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
            names[s][n++] = kMissingLine;
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

const char* choiceColumnName(uint8_t slot)
{
    build();
    if (slot >= kSlots || chosen[slot] == 0 || names[slot][chosen[slot]] == kMissingLine)
    {
        return choiceName(slot); // Stock, Missing
    }
    // a catalog entry's name (32 bytes): its short name follows the name's 0, if it has one
    const char* name = names[slot][chosen[slot]];
    const size_t at = pluginkit::textLength(name, 32) + 1u;
    return at < 32 && name[at] != 0 ? name + at : name;
}

bool missing(uint8_t slot)
{
    build();
    return slot < kSlots && settings.slot[slot].file[0] != 0 && names[slot][chosen[slot]] == kMissingLine;
}

void choose(uint8_t slot, uint8_t index)
{
    build();
    if (slot >= kSlots || index >= counts[slot] || index == chosen[slot])
    {
        return;
    }
    if (names[slot][index] == kMissingLine) // the Missing line: it stays as it is
    {
        return;
    }
    SlotSetting& s = settings.slot[slot];
    stock(s);
    if (slot == kDynamics)
    {
        settings.ducks = 0; // another plugin's set: back to the master
    }
    if (index > 0)
    {
        copyText(s.file, sizeof(s.file), pl::slotAt(static_cast<pl::Slot>(slot), index - 1u)->file);
    }
    picked[slot] = true;
    refreshChoices();
}

// ---- the slot's own page --------------------------------------------------------------

namespace
{

MFX_SDRAM char titleText[16 + sizeof(kMissing) + kFileSize]; // main loop; written before use

bool ours(uint8_t slot)
{
    return slot < kSlots && on(Mod::masterfx) && pl::installed(static_cast<pl::Slot>(slot));
}

} // namespace

const char* pageTitle(uint8_t slot, const char* word)
{
    if (slot >= kSlots || !on(Mod::masterfx))
    {
        return nullptr;
    }
    if (ours(slot))
    {
        // a dynamics plugin isn't the stock limiter: its page has its name alone
        if (slot == kDynamics)
        {
            snprintf(titleText, sizeof(titleText), "%s%s", choiceName(slot), bypassed(slot) ? " (bypassed)" : "");
        }
        else
        {
            snprintf(titleText, sizeof(titleText), "%s: %s%s", word, choiceName(slot),
                     bypassed(slot) ? " (bypassed)" : "");
        }
        return titleText;
    }
    if (missing(slot))
    {
        snprintf(titleText, sizeof(titleText), "%s: %s", word, missingText[slot]);
        return titleText;
    }
    return nullptr;
}

bool bypassed(uint8_t slot)
{
    return slot < kSlots && pl::bypassed(static_cast<pl::Slot>(slot));
}

int takeBypassed()
{
    return pl::takeBypassed();
}

uint8_t ownKnobs(uint8_t slot)
{
    if (!ours(slot))
    {
        return 0;
    }
    const uint32_t n = pl::knobs(static_cast<pl::Slot>(slot));
    const uint32_t most = ownDynamics(slot) ? kOwnKnobs : kPageKnobs;
    return static_cast<uint8_t>(n < most ? n : most);
}

bool ownDynamics(uint8_t slot)
{
    return slot == kDynamics && ours(slot) && pl::ownDynamics(pl::kDynamics);
}

uint8_t ownKnobColumn(uint8_t slot, uint8_t knob)
{
    if (ownDynamics(slot))
    {
        return static_cast<uint8_t>(knob < 3 ? knob : knob + 2); // 0 to 2, then 5 and 6
    }
    return static_cast<uint8_t>(5 + knob);
}

int ownKnobAt(uint8_t slot, uint8_t column)
{
    const uint8_t n = ownKnobs(slot);
    for (uint8_t k = 0; k < n; k++)
    {
        if (ownKnobColumn(slot, k) == column)
        {
            return k;
        }
    }
    return -1;
}

const char* ownKnobLabel(uint8_t slot, uint8_t knob)
{
    return knob < ownKnobs(slot) ? pl::knobLabel(static_cast<pl::Slot>(slot), knob) : "";
}

void ownKnobText(uint8_t slot, uint8_t knob, char* out, size_t size)
{
    if (size == 0)
    {
        return;
    }
    out[0] = 0;
    if (knob < ownKnobs(slot))
    {
        pl::knobText(static_cast<pl::Slot>(slot), knob, out, static_cast<uint32_t>(size));
    }
}

uint8_t ownKnobPercent(uint8_t slot, uint8_t knob)
{
    if (knob >= ownKnobs(slot))
    {
        return 0;
    }
    const pl::Slot s = static_cast<pl::Slot>(slot);
    const uint32_t steps = pl::knobSteps(s, knob);
    return steps > 1 ? static_cast<uint8_t>(pl::knobStep(s, knob) * 100u / (steps - 1u)) : 0;
}

void stepOwnKnob(uint8_t slot, uint8_t knob, int steps)
{
    if (knob >= ownKnobs(slot))
    {
        return;
    }
    const pl::Slot s = static_cast<pl::Slot>(slot);
    pl::stepKnob(s, knob, steps);
    settings.slot[slot].knob[knob] = pl::knob(s, knob);
}

void defaultOwnKnob(uint8_t slot, uint8_t knob)
{
    if (knob >= ownKnobs(slot))
    {
        return;
    }
    const pl::Slot s = static_cast<pl::Slot>(slot);
    pl::resetKnob(s, knob);
    settings.slot[slot].knob[knob] = pl::knob(s, knob);
}

// ---- the Ducks list ------------------------------------------------------------------

namespace
{

const char* const kDuckNames[kDuckItems] = {
    "Master", "Track 1", "Track 2", "Track 3", "Track 4", "Track 5", "Track 6",
    "Track 7", "Track 8", "Synth 1", "Synth 2", "Synth 3",
};

MFX_SDRAM char ducksLine[16]; // main loop; written before use

} // namespace

bool canDuck()
{
#if defined(MT_HOST_TEST) || defined(PLATFORM_IMX)
    return ours(kDynamics) && pl::ducks(pl::kDynamics);
#else
    return false;
#endif
}

bool canBus()
{
#if defined(MT_HOST_TEST) || defined(PLATFORM_IMX)
    return ours(kDynamics) && pl::bus(pl::kDynamics);
#else
    return false;
#endif
}

const char* const* duckNames()
{
    return kDuckNames;
}

bool ducked(uint8_t item)
{
    if (item == 0)
    {
        return settings.ducks == 0;
    }
    return item < kDuckItems && ((settings.ducks >> (item - 1)) & 1u) != 0;
}

void toggleDuck(uint8_t item)
{
    if (item == 0)
    {
        settings.ducks = 0;
    }
    else if (item < kDuckItems)
    {
        settings.ducks = static_cast<uint16_t>(settings.ducks ^ (1u << (item - 1)));
    }
    applyDucks();
}

const char* ducksText()
{
    uint8_t count = 0;
    uint8_t one = 0;
    for (uint8_t item = 1; item < kDuckItems; item++)
    {
        if (ducked(item))
        {
            count++;
            one = item;
        }
    }
    if (count <= 1)
    {
        return kDuckNames[one]; // "Master" when none
    }
    snprintf(ducksLine, sizeof(ducksLine), "%u tracks", static_cast<unsigned>(count));
    return ducksLine;
}

#ifdef MT_HOST_TEST

namespace
{
void refreshMenus() {}
void applyDucks() {}
} // namespace

#else

namespace
{

// The Config menu's two lists: the choice in the list and the lists themselves. Until the first
// refreshChoices (names is SDRAM, not zeroed) they show Stock alone.
const char* const kStockOnly[] = { "Stock" };
uint8_t menuChoice[kSlots] = {};
void reverbMenuEdited();
void delayMenuEdited();
void dynamicsMenuEdited();
strItemTypeListTextWithAction slotMenus[kSlots] = {
    { &menuChoice[kReverb], 1, kStockOnly, reverbMenuEdited },
    { &menuChoice[kDelay], 1, kStockOnly, delayMenuEdited },
    { &menuChoice[kDynamics], 1, kStockOnly, dynamicsMenuEdited },
};

void refreshMenus()
{
    for (uint8_t s = 0; s < kSlots; s++)
    {
        slotMenus[s].ptrText = names[s];
        slotMenus[s].count = counts[s];
        menuChoice[s] = chosen[s];
    }
}

void menuEdited(uint8_t slot, uint8_t index)
{
    choose(slot, index);
    refreshMenus();         // the Missing line can't be chosen: back to the project's
    projectSettingEdited(); // stored, then applied
}

void reverbMenuEdited()
{
    menuEdited(kReverb, menuChoice[kReverb]);
}

void delayMenuEdited()
{
    menuEdited(kDelay, menuChoice[kDelay]);
}

void dynamicsMenuEdited()
{
    menuEdited(kDynamics, menuChoice[kDynamics]);
}

constexpr uint32_t kApplyWaitMs = 150;

bool hasStarted = false;
bool applyPending = false;
uint32_t applyAskedAt = 0;

uint32_t nowMs()
{
    return Tracker::getInstance()->getPlatform()->getTimer()->millis();
}

// Nothing of ours runs in the slot from here. The reverb and delay objects are stopped (CE's clear
// lets them run again). The limiter isn't, since the dry master goes through it: its hook passes
// the master through untouched instead.
void stop(uint8_t slot)
{
    if (slot == kReverb)
    {
        engine.getMixerAndFx()->getPolyverb()->blockUpdate();
        activeReverb = 0;
    }
    else if (slot == kDelay)
    {
        engine.getMixerAndFx()->getDelay()->blockUpdate();
        activeDelay = 0;
    }
    else
    {
        activeDynamics = kDynamicsDry;
        duckMask = 0; // applyDucks sets them again for the plugin that plays next
        busMask = 0;
    }
}

// After stop(): the slot's installed plugin plays, in cleared memory
void runPlugin(uint8_t slot)
{
    if (slot == kReverb)
    {
        activeReverb = 1;
        engine.getMixerAndFx()->getPolyverb()->reset(); // calls reverbCleared, unblocks
    }
    else if (slot == kDelay)
    {
        activeDelay = 1;
        engine.getMixerAndFx()->getDelay()->clear(); // calls delayCleared, unblocks
    }
    else
    {
        clearDynamicsMemory();
        pl::cleared(pl::kDynamics);
        activeDynamics = 1;
    }
}

// After stop(): CE's own plays, in buffers it lays out again itself (the limiter's state was kept
// as at power-up while the plugin played)
void runCe(uint8_t slot)
{
    if (slot == kReverb)
    {
        engine.getMixerAndFx()->getPolyverb()->reset();
    }
    else if (slot == kDelay)
    {
        engine.getMixerAndFx()->getDelay()->clear();
    }
    else
    {
        activeDynamics = 0;
    }
}

// The settings' own knobs on the slot's plugin: a saved value, else the plugin's default. A knob
// already there isn't sent again (setKnob), so this is safe while the plugin plays.
void applyKnobs(pl::Slot s, const SlotSetting& want)
{
    for (uint32_t k = 0; k < kOwnKnobs && k < pl::knobs(s); k++)
    {
        if (pluginkit::finite(want.knob[k]))
        {
            pl::setKnob(s, k, want.knob[k]); // held on the plugin's steps; settings keep the value
        }
        else
        {
            pl::resetKnob(s, k);
        }
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
        applyKnobs(s, want); // it plays on, with the last project's knobs until these are set
        return;
    }
    pl::Status status = pl::Status::missing;
    if (wantPlugin)
    {
        status = pl::read(s, want.file, fromProject); // the card, while what plays keeps playing
    }
    if (status != pl::Status::ok)
    {
        if (pl::installed(s))
        {
            stop(slot);
            pl::remove(s);
            runCe(slot);
        }
        return;
    }
    stop(slot);
    pl::install(s);
    applyKnobs(s, want);
    runPlugin(slot);
}

void applyDucks()
{
    duckMask = (canDuck() ? settings.ducks : 0u) & kDuckAll;
    busMask = (canBus() ? settings.ducks : 0u) & kDuckAll;
}

} // namespace

void dynamicsReset()
{
    if (activeDynamics != 1)
    {
        return;
    }
    activeDynamics = kDynamicsDry;
    clearDynamicsMemory();
    pl::cleared(pl::kDynamics);
    activeDynamics = 1;
}

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
    applySlot(kDynamics, isOn);
    applyDucks();
    refreshChoices();
}

const void* menuSetup(uint8_t slot)
{
    return &slotMenus[slot < kSlots ? slot : 0];
}

void pageOpened()
{
    if (!pl::started() || cardIsLent())
    {
        return;
    }
    pl::scan(); // files copied to /MasterFX since the last list
    if (hasStarted)
    {
        apply(); // a project's file that is there now plays; what plays already is kept
    }
    else
    {
        refreshChoices();
    }
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
