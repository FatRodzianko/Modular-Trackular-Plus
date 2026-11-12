// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Synth FX's rows on the patch editor (synthfxMod.h): FX1, its Placement, its plugin's knobs; the
// same for FX2. A slot's row picks its plugin from the catalog (not the one in the part's other
// slot); Placement (Pre or Post) is there on a WAVE part for a plugin that allows Pre; a knob's
// row has the plugin's name, steps and texts. Main loop only.

#include "modsBuild.h"

#if MT_HAS_SYNTHFX

#include "synthfxMod.h"

#include "synthfxPlugins.h"

#include "config.h"
#include <cr_section_macros.h>
#include <cstdio>
#include <cstring>

namespace mods
{
namespace synthfx
{

namespace
{

constexpr int kNone = -1;      // a choice: no plugin
constexpr int kOwnFile = -2;   // a choice: the slot's file, not listed now (missing, damaged)

// locate's knob for a slot's own rows
constexpr int kSlotRow = -1;
constexpr int kPlacementRow = -2;
const char* const kPlacements[2] = { "Pre", "Post" }; // the old effects' order

// a slot's choices, from the last choices() (main loop; written before it is read)
__NOINIT(EXTERNAL_RAM) int choiceList[kMaxChoices];

bool validPart(int part)
{
    return part >= 0 && part < kParts;
}

int knobsOf(int part, int slot)
{
    const PtsfxHeader* h = plugins::header(slotInstance(part, slot));
    if (h == nullptr)
    {
        return 0;
    }
    return h->num_params < kKnobs ? h->num_params : static_cast<int>(kKnobs);
}

// The slot has a Placement row: its plugin allows Pre, on a WAVE part
bool hasPlacement(int part, int slot)
{
    const PtsfxHeader* h = plugins::header(slotInstance(part, slot));
    return h != nullptr && (h->flags & PTSFX_FLAG_PRE) != 0 && partIsWave(part);
}

// row -> slot and knob (kSlotRow, kPlacementRow, or the knob); false if there is no such row
bool locate(int part, int row, int* slot, int* knob)
{
    if (!validPart(part) || row < 0)
    {
        return false;
    }
    for (int s = 0; s < kSlots; s++)
    {
        const int extra = hasPlacement(part, s) ? 1 : 0;
        const int rows = 1 + extra + knobsOf(part, s);
        if (row < rows)
        {
            *slot = s;
            *knob = row == 0 ? kSlotRow : (extra != 0 && row == 1 ? kPlacementRow : row - 1 - extra);
            return true;
        }
        row -= rows;
    }
    return false;
}

// The slot's choices into choiceList: None, every usable plugin but the other slot's, and the
// slot's own file if it isn't one of those. Returns how many.
int choices(int part, int slot)
{
    int n = 0;
    choiceList[n++] = kNone;
    const char* own = slots[part][slot].file;
    bool ownListed = own[0] == 0;
    for (size_t i = 0; i < plugins::catalogCount() && n < kMaxChoices - 1; i++)
    {
        const plugins::CatalogEntry* e = plugins::catalogAt(i);
        if (e == nullptr || e->status != plugins::Status::ok || usedByOtherSlot(part, slot, e->file))
        {
            continue;
        }
        if (!ownListed && plugins::catalogFind(own) == static_cast<int>(i))
        {
            ownListed = true;
        }
        choiceList[n++] = static_cast<int>(i);
    }
    if (!ownListed)
    {
        choiceList[n++] = kOwnFile;
    }
    return n;
}

int currentChoice(int part, int slot, int count)
{
    const char* own = slots[part][slot].file;
    if (own[0] == 0)
    {
        return 0;
    }
    const int listed = plugins::catalogFind(own);
    for (int i = 1; i < count; i++)
    {
        if (choiceList[i] == kOwnFile || (listed >= 0 && choiceList[i] == listed))
        {
            return i;
        }
    }
    return 0;
}

// The file name without .ptsfx
void stem(const char* file, char* out, size_t size)
{
    snprintf(out, size, "%s", file);
    char* dot = strrchr(out, '.');
    if (dot != nullptr && dot != out)
    {
        *dot = 0;
    }
}

void pluginName(const char* file, char* out, size_t size)
{
    const int listed = plugins::catalogFind(file);
    const plugins::CatalogEntry* e = listed >= 0 ? plugins::catalogAt(static_cast<size_t>(listed)) : nullptr;
    if (e != nullptr && e->status == plugins::Status::ok && e->name[0] != 0)
    {
        snprintf(out, size, "%s", e->name);
    }
    else
    {
        stem(file, out, size);
    }
}

const char* whyText(uint8_t status)
{
    switch (static_cast<plugins::Status>(status))
    {
    case plugins::Status::missing: return "Missing";
    case plugins::Status::badFile: return "Bad file";
    case plugins::Status::tooBig: return "Too big";
    case plugins::Status::noCodeRoom:
    case plugins::Status::noStateRoom:
    case plugins::Status::tooMany: return "No room";
    case plugins::Status::skipped: return "Safe mode";
    case plugins::Status::readFailed: return "Read error";
    default: return "Not loaded";
    }
}

void slotText(int part, int slot, char* out, size_t size)
{
    char name[kFileSize];
    pluginName(slots[part][slot].file, name, sizeof name);
    switch (slotState(part, slot))
    {
    case SlotState::empty: snprintf(out, size, "None"); break;
    case SlotState::loading: snprintf(out, size, "Loading"); break;
    case SlotState::running: snprintf(out, size, "%s", name); break;
    case SlotState::bypassed: snprintf(out, size, "Slow: %s", name); break;
    case SlotState::failed: snprintf(out, size, "%s: %s", whyText(slotStatus(part, slot)), name); break;
    }
}

const PtsfxParam* paramOf(int part, int slot, int knob)
{
    const PtsfxHeader* h = plugins::header(slotInstance(part, slot));
    return h != nullptr && knob >= 0 && knob < knobsOf(part, slot) ? &h->params[knob] : nullptr;
}

} // namespace

int rowCount(int part)
{
    if (!validPart(part))
    {
        return 0;
    }
    int rows = 0;
    for (int s = 0; s < kSlots; s++)
    {
        rows += 1 + (hasPlacement(part, s) ? 1 : 0) + knobsOf(part, s);
    }
    return rows;
}

void rowName(int part, int row, char* out, size_t size)
{
    int slot = 0;
    int knob = 0;
    if (out == nullptr || size == 0)
    {
        return;
    }
    out[0] = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return;
    }
    if (knob == kSlotRow)
    {
        snprintf(out, size, "FX%d", slot + 1);
        return;
    }
    if (knob == kPlacementRow)
    {
        snprintf(out, size, "Placement");
        return;
    }
    const PtsfxParam* p = paramOf(part, slot, knob);
    snprintf(out, size, "%s", p != nullptr ? p->label : "");
}

bool rowIsList(int part, int row)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return false;
    }
    if (knob < 0) // a slot, or its Placement
    {
        return true;
    }
    const PtsfxParam* p = paramOf(part, slot, knob);
    return p != nullptr && p->display_type == PTSFX_PARAM_LIST &&
           plugins::knobSteps(slotInstance(part, slot), static_cast<uint32_t>(knob)) <= kListKnobSteps;
}

bool rowBipolar(int part, int row)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob) || knob < 0)
    {
        return false;
    }
    const PtsfxParam* p = paramOf(part, slot, knob);
    return p != nullptr && p->min_val < 0.0f && p->max_val == -p->min_val;
}

int rowSteps(int part, int row)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return 1;
    }
    if (knob == kPlacementRow)
    {
        return 2;
    }
    if (knob < 0)
    {
        return choices(part, slot);
    }
    const uint32_t n = plugins::knobSteps(slotInstance(part, slot), static_cast<uint32_t>(knob));
    return n > 0 ? static_cast<int>(n) : 1;
}

int rowStep(int part, int row)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return 0;
    }
    if (knob == kPlacementRow)
    {
        return slots[part][slot].pre != 0 ? 0 : 1;
    }
    if (knob < 0)
    {
        return currentChoice(part, slot, choices(part, slot));
    }
    return static_cast<int>(plugins::knobStep(slotInstance(part, slot), static_cast<uint32_t>(knob)));
}

void rowText(int part, int row, char* out, size_t size)
{
    int slot = 0;
    int knob = 0;
    if (out == nullptr || size == 0)
    {
        return;
    }
    out[0] = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return;
    }
    if (knob == kPlacementRow)
    {
        snprintf(out, size, "%s", kPlacements[slots[part][slot].pre != 0 ? 0 : 1]);
        return;
    }
    if (knob < 0)
    {
        slotText(part, slot, out, size);
        return;
    }
    plugins::knobText(slotInstance(part, slot), static_cast<uint32_t>(knob), out, static_cast<uint32_t>(size));
}

void rowItemText(int part, int row, int step, char* out, size_t size)
{
    int slot = 0;
    int knob = 0;
    if (out == nullptr || size == 0)
    {
        return;
    }
    out[0] = 0;
    if (!locate(part, row, &slot, &knob) || step < 0)
    {
        return;
    }
    if (knob >= 0)
    {
        plugins::knobTextAt(slotInstance(part, slot), static_cast<uint32_t>(knob), static_cast<uint32_t>(step), out,
                            static_cast<uint32_t>(size));
        return;
    }
    if (knob == kPlacementRow)
    {
        if (step < 2)
        {
            snprintf(out, size, "%s", kPlacements[step]);
        }
        return;
    }
    const int count = choices(part, slot);
    if (step >= count)
    {
        return;
    }
    const int c = choiceList[step];
    if (c == kNone)
    {
        snprintf(out, size, "None");
    }
    else if (c == kOwnFile)
    {
        slotText(part, slot, out, size);
    }
    else
    {
        const plugins::CatalogEntry* e = plugins::catalogAt(static_cast<size_t>(c));
        pluginName(e != nullptr ? e->file : "", out, size);
    }
}

int rowEncoderSteps(int part, int row, int ticks)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob) || knob < 0)
    {
        return ticks;
    }
    const int per = (rowSteps(part, row) - 1) / kStepsPerTurn;
    return ticks * (per > 1 ? per : 1);
}

bool setRowStep(int part, int row, int step)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return false;
    }
    if (knob >= 0)
    {
        const int id = slotInstance(part, slot);
        const int last = rowSteps(part, row) - 1;
        step = step < 0 ? 0 : (step > last ? last : step);
        plugins::setKnobStep(id, static_cast<uint32_t>(knob), static_cast<uint32_t>(step));
        knobEdited(part, slot, static_cast<uint32_t>(knob));
        slots[part][slot].knobs[knob] = plugins::knob(id, static_cast<uint32_t>(knob));
        return false;
    }
    if (knob == kPlacementRow)
    {
        const bool pre = step <= 0;
        if (pre == (slots[part][slot].pre != 0))
        {
            return false;
        }
        setSlotPre(part, slot, pre);
        remake(part, slot, false); // a Pre plugin is one instance per voice: made again
        return true;
    }
    const int count = choices(part, slot);
    step = step < 0 ? 0 : (step >= count ? count - 1 : step);
    const int c = choiceList[step];
    if (step == currentChoice(part, slot, count) || c == kOwnFile)
    {
        return false;
    }
    if (c == kNone)
    {
        clearSlot(part, slot);
    }
    else
    {
        const plugins::CatalogEntry* e = plugins::catalogAt(static_cast<size_t>(c));
        if (e == nullptr)
        {
            return false;
        }
        setSlotFile(part, slot, e->file);
    }
    remake(part, slot, false);
    return true;
}

bool defaultRow(int part, int row)
{
    int slot = 0;
    int knob = 0;
    if (!locate(part, row, &slot, &knob))
    {
        return false;
    }
    if (knob == kPlacementRow)
    {
        return setRowStep(part, row, 1); // Post
    }
    if (knob < 0)
    {
        return setRowStep(part, row, 0); // None
    }
    const PtsfxParam* p = paramOf(part, slot, knob);
    if (p != nullptr)
    {
        const int id = slotInstance(part, slot);
        plugins::setKnob(id, static_cast<uint32_t>(knob), p->default_val);
        knobEdited(part, slot, static_cast<uint32_t>(knob));
        slots[part][slot].knobs[knob] = plugins::knob(id, static_cast<uint32_t>(knob));
    }
    return false;
}

void pageOpened()
{
    if (plugins::started())
    {
        plugins::scan(); // the card may have new files
        retryFailed();
    }
}

} // namespace synthfx
} // namespace mods

#endif // MT_HAS_SYNTHFX
