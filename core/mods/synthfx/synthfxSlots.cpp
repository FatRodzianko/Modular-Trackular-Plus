// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's slots in the main loop (synthfxMod.h): each slot's plugin made from its setting,
// handed to the audio interrupt, and let go. Main loop only: the plugins' files are read here.
//
// A slot changes in steps, one main-loop pass apart, so nothing waits on the interrupt: the
// running plugin is retired (the interrupt fades it out over its next block), then freed once
// it has faded (or after kRetireMs, if the interrupt isn't running the part), then the new one
// is made and published (it fades in over its first block).

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

constexpr uint32_t kRetireMs = 50; // a part not rendered (no voices) never fades its slot out

bool changedSince = false;

struct Live
{
    int8_t id;          // the plugin made for this slot, published to the interrupt
    int8_t old;         // the one fading out, to be freed
    bool pending;       // the slot's plugin to be made once old is freed
    bool fromProject;
    uint8_t status;     // plugins::Status of the last try
    uint32_t oldSince;
};

Live live[kParts][kSlots] = {
    { { -1, -1, false, false, 0, 0 }, { -1, -1, false, false, 0, 0 } },
    { { -1, -1, false, false, 0, 0 }, { -1, -1, false, false, 0, 0 } },
    { { -1, -1, false, false, 0, 0 }, { -1, -1, false, false, 0, 0 } },
};

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
    if (l.old >= 0)
    {
        if (!retired(part, slot) && plugins::now() - l.oldSince < kRetireMs)
        {
            return false;
        }
        publish(part, slot, -1, false);
        plugins::destroy(l.old);
        l.old = -1;
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
    const int id = plugins::create(want.file, l.fromProject, &status);
    l.status = static_cast<uint8_t>(status);
    if (id < 0)
    {
        return true;
    }
    const PtsfxHeader* h = plugins::header(id);
    const uint32_t knobs = h->num_params < kKnobs ? h->num_params : kKnobs;
    for (uint32_t k = 0; k < knobs; k++)
    {
        if (isFinite(want.knobs[k]))
        {
            plugins::setKnob(id, k, want.knobs[k]);
        }
        want.knobs[k] = plugins::knob(id, k); // on the plugin's steps, as it will be saved
    }
    l.id = static_cast<int8_t>(id);
    publish(part, slot, id, (h->flags & PTSFX_FLAG_WIDENS) != 0);
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
    if (l.id >= 0) // only once the last old one is freed (step makes the new one after)
    {
        retire(part, slot);
        l.old = l.id;
        l.id = -1;
        l.oldSince = plugins::now();
    }
    l.pending = true;
    l.fromProject = fromProject;
    step(part, slot);
}

bool updateSlots()
{
    if (!plugins::started())
    {
        return false;
    }
    bool changed = false;
    if (takeLoaded())
    {
        for (int part = 0; part < kParts; part++)
        {
            for (int slot = 0; slot < kSlots; slot++)
            {
                remake(part, slot, true);
            }
        }
        changed = true;
    }
    for (int part = 0; part < kParts; part++)
    {
        for (int slot = 0; slot < kSlots; slot++)
        {
            changed = step(part, slot) || changed;
        }
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
    if (l.id >= 0)
    {
        return plugins::bypassed(l.id) ? SlotState::bypassed : SlotState::running;
    }
    if (l.pending || l.old >= 0)
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
    return validSlot(part, slot) ? live[part][slot].id : -1;
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
