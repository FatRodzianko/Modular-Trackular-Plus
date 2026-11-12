// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's slots in the main loop (synthfxMod.h): each slot's plugin made from its setting,
// handed to the audio interrupt, and let go. Main loop only: the plugins' files are read here.
//
// A slot changes in steps, one main-loop pass apart, so nothing waits on the interrupt: the
// running plugin is retired (the interrupt fades it out over its next block), then freed once
// it has faded (or after kRetireMs, if the interrupt isn't running the part), then the new one
// is made and published (it fades in over its first block).
//
// A slot with Placement Pre and a plugin that allows it (PRE) is made with kVoices instances, one
// for each of a WAVE part's voices; the first also runs on the part when it plays another engine.
// Its knobs are set on all of them alike.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthfxPlugins.h"

namespace mods
{
namespace synthfx
{

namespace
{

static_assert(kKnobs == PTSFX_MAX_PARAMS && kFileSize == plugins::kFileNameSize, "the plugins' sizes");
static_assert(kParts * kSlots * kVoices <= static_cast<int>(plugins::kInstanceCount),
              "every slot Pre on every part fits the instances");

constexpr uint32_t kRetireMs = 50; // a part not rendered (no voices) never fades its slot out

bool changedSince = false;
bool soon[kParts] = { false, false, false };

struct Live
{
    int8_t id[kVoices];     // the instances made for this slot, published to the interrupt
    int8_t old[kVoices];    // the ones fading out, to be freed
    uint8_t count;          // of id: 0, 1, or kVoices (Pre)
    uint8_t oldCount;
    bool pending;           // the slot's plugin to be made once old is freed
    bool fromProject;
    bool picked;            // the page's pick, made once the encoder rests (remakeWhenStill)
    bool storeWhenMade;     // a pick: the project's record again once made, its knobs then on its steps
    uint8_t status;         // plugins::Status of the last try
    uint32_t oldSince;
    uint32_t pickedAt;

    constexpr Live()
        : id{ -1, -1, -1, -1, -1, -1, -1, -1 }, old{ -1, -1, -1, -1, -1, -1, -1, -1 }, count(0), oldCount(0),
          pending(false), fromProject(false), picked(false), storeWhenMade(false), status(0), oldSince(0), pickedAt(0)
    {
    }
};

Live live[kParts][kSlots];

bool validSlot(int part, int slot)
{
    return part >= 0 && part < kParts && slot >= 0 && slot < kSlots;
}

bool isFinite(float f)
{
    return f == f && f - f == 0.0f;
}

// One step towards the slot's setting; whether anything changed
bool step(int part, int slot)
{
    Live& l = live[part][slot];
    if (l.oldCount > 0)
    {
        if (!retired(part, slot) && plugins::now() - l.oldSince < kRetireMs)
        {
            return false;
        }
        publish(part, slot, nullptr, 0, false);
        for (int v = 0; v < l.oldCount; v++)
        {
            plugins::destroy(l.old[v]);
            l.old[v] = -1;
        }
        l.oldCount = 0;
    }
    if (!l.pending)
    {
        return false;
    }
    l.pending = false;
    Slot& want = slots[part][slot];
    if (want.file[0] == 0)
    {
        l.status = static_cast<uint8_t>(plugins::Status::ok);
        return true;
    }
    plugins::Status status = plugins::Status::ok;
    int8_t ids[kVoices];
    const int first = plugins::create(want.file, l.fromProject, &status);
    l.status = static_cast<uint8_t>(status);
    if (first < 0)
    {
        return true;
    }
    ids[0] = static_cast<int8_t>(first);
    const PtsfxHeader* h = plugins::header(first);
    const int count = want.pre != 0 && (h->flags & PTSFX_FLAG_PRE) != 0 ? kVoices : 1;
    for (int v = 1; v < count; v++)
    {
        const int id = plugins::create(want.file, l.fromProject, &status);
        if (id < 0) // no room for every voice's: none
        {
            for (int u = 0; u < v; u++)
            {
                plugins::destroy(ids[u]);
            }
            l.status = static_cast<uint8_t>(status);
            return true;
        }
        ids[v] = static_cast<int8_t>(id);
    }
    const uint32_t knobs = h->num_params < kKnobs ? h->num_params : kKnobs;
    for (uint32_t k = 0; k < knobs; k++)
    {
        if (isFinite(want.knobs[k]))
        {
            for (int v = 0; v < count; v++)
            {
                plugins::setKnob(ids[v], k, want.knobs[k]);
            }
        }
        want.knobs[k] = plugins::knob(first, k); // on the plugin's steps, as it will be saved
    }
    for (int v = 0; v < count; v++)
    {
        l.id[v] = ids[v];
    }
    l.count = static_cast<uint8_t>(count);
    publish(part, slot, ids, count, (h->flags & PTSFX_FLAG_WIDENS) != 0);
    return true;
}

} // namespace

void remake(int part, int slot, bool fromProject)
{
    if (!validSlot(part, slot))
    {
        return;
    }
    Live& l = live[part][slot];
    l.picked = false;
    l.storeWhenMade = false;
    if (l.count > 0) // only once the last old ones are freed (step makes the new one after)
    {
        retire(part, slot);
        for (int v = 0; v < l.count; v++)
        {
            l.old[v] = l.id[v];
            l.id[v] = -1;
        }
        l.oldCount = l.count;
        l.count = 0;
        l.oldSince = plugins::now();
    }
    l.pending = true;
    l.fromProject = fromProject;
    step(part, slot);
}

void remakeWhenStill(int part, int slot)
{
    if (!validSlot(part, slot))
    {
        return;
    }
    Live& l = live[part][slot];
    l.picked = true;
    l.pickedAt = plugins::now();
    changedSince = true;
}

void remakeSoon(int part)
{
    if (part >= 0 && part < kParts)
    {
        soon[part] = true;
    }
}

bool updateSlots()
{
    if (!plugins::started())
    {
        return false;
    }
    bool changed = false;
    bool picksMade = false;
    const bool loaded = takeLoaded();
    for (int part = 0; part < kParts; part++)
    {
        if (loaded || soon[part])
        {
            soon[part] = false;
            for (int slot = 0; slot < kSlots; slot++)
            {
                remake(part, slot, true);
            }
            changed = true;
        }
    }
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            Live& l = live[part][slot];
            if (l.picked && plugins::now() - l.pickedAt >= kPickRestMs)
            {
                remake(part, slot, false);
                l.storeWhenMade = true;
                changed = true;
            }
            changed = step(part, slot) || changed;
            if (l.storeWhenMade && !l.pending) // made (or failed), the old one let go first
            {
                l.storeWhenMade = false;
                picksMade = true;
            }
        }
    }
    if (picksMade)
    {
        // the record again: the edit stored it before the plugin was made and put its knobs on its steps
        mods::projectSettingEdited();
    }
    if (plugins::takeBypassed() >= 0)
    {
        changed = true;
    }
    changedSince = changedSince || changed;
    return changed;
}

bool takeSlotsChanged()
{
    const bool was = changedSince;
    changedSince = false;
    return was;
}

void retryFailed()
{
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            if (slotState(part, slot) == SlotState::failed)
            {
                remake(part, slot, true);
            }
        }
    }
}

SlotState slotState(int part, int slot)
{
    if (!validSlot(part, slot) || slots[part][slot].file[0] == 0)
    {
        return SlotState::empty;
    }
    const Live& l = live[part][slot];
    if (l.picked) // the old one plays on until the pick is made
    {
        return SlotState::loading;
    }
    if (l.count > 0)
    {
        return plugins::bypassed(l.id[0]) ? SlotState::bypassed : SlotState::running;
    }
    if (l.pending || l.oldCount > 0)
    {
        return SlotState::loading;
    }
    return SlotState::failed;
}

uint8_t slotStatus(int part, int slot)
{
    return validSlot(part, slot) ? live[part][slot].status : 0;
}

int slotInstance(int part, int slot)
{
    // none while a pick waits: the one playing is the old plugin, not the slot's setting
    return validSlot(part, slot) && live[part][slot].count > 0 && !live[part][slot].picked ? live[part][slot].id[0]
                                                                                         : -1;
}

bool slotRunsPre(int part, int slot)
{
    return validSlot(part, slot) && live[part][slot].count > 1;
}

void knobEdited(int part, int slot, uint32_t knob)
{
    if (!validSlot(part, slot))
    {
        return;
    }
    const Live& l = live[part][slot];
    if (l.count == 0)
    {
        return;
    }
    const float v = plugins::knob(l.id[0], knob);
    for (int i = 1; i < l.count; i++)
    {
        plugins::setKnob(l.id[i], knob, v);
    }
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
