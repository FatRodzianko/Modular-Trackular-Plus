// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Mods' buttons on the Pattern screen. Page 3 of the bottom bar has free buttons; the
// buttons of the mods that are on take them in list order, so a button's place depends
// on which mods are on. They work in edit mode, like the other editing buttons, and are
// greyed out otherwise. A button with a hold function works in every mode, like Name.

#ifndef MODS_MODSPATTERNBUTTONS_H_
#define MODS_MODSPATTERNBUTTONS_H_

#include "mods.h"

#if MT_MODS

namespace mods
{

struct PatternButton
{
    Mod mod;
    const char* text;
    void (*press)();
    void (*hold)(uint8_t state) = nullptr; // a button that's held, instead of press
    const char* (*text2)() = nullptr;      // its second line
};

// Page 3's free buttons: CE binds 0 and 1 (Name) and 7 (More).
constexpr uint8_t kPatternButtonFirst = 2;
constexpr uint8_t kPatternButtonSlots = 5;

// The n-th button of the list whose mod is on, or nullptr past the last. An entry with
// neither function is skipped, so a list is never empty in a build without its mods.
inline const PatternButton* nthPatternButton(const PatternButton* list, size_t count, size_t n)
{
    for (size_t i = 0; i < count; i++)
    {
        if ((list[i].press != nullptr || list[i].hold != nullptr) && on(list[i].mod))
        {
            if (n == 0)
            {
                return &list[i];
            }
            n--;
        }
    }
    return nullptr;
}

// The Pattern screen's list (fPatternEdit.cpp): nthPatternButton of it.
const PatternButton* patternEditorButton(size_t n);

} // namespace mods

#endif // MT_MODS

#endif /* MODS_MODSPATTERNBUTTONS_H_ */
