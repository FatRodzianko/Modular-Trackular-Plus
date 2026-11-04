// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The mod registry and the calls CE's code makes into the mods. Every hook in a CE
// file is marked "MT:" and sits behind MT_MODS, so other builds stay plain CE.

#ifndef MODS_MODS_H_
#define MODS_MODS_H_

#include "modsBuild.h"

#if MT_MODS

#include <array>
#include <cstddef>
#include <cstdint>

class FileSystem;

namespace mods
{

// Fixed ids, the index into the switches records; never reuse or renumber one. They
// follow the 1.2.0 web patcher's bit order; a mod not ported yet keeps its id unused.
enum class Mod : uint8_t
{
    colorsong = 0,
    humanize = 1,
    interpolate = 2,
    masterfx = 3,
    swing = 4,
    visualizer = 5,
    condtrig = 6,
    synths = 7,
    legato = 8,
    copypattern = 9,
    slicethru = 10,
    resample = 11,
    tracklen = 12,
    songloop = 13,
    chords = 14, // retired: CE has these chords; the id stays unused
    kCount
};

constexpr size_t kModCount = static_cast<size_t>(Mod::kCount);

// How many mods this build has (each has an MT_HAS_<NAME> flag).
constexpr size_t kBuiltModCount = 0
#if MT_HAS_COLORSONG
                                  + 1
#endif
#if MT_HAS_HUMANIZE
                                  + 1
#endif
#if MT_HAS_SWING
                                  + 1
#endif
  ;

// One byte per mod, 0 Off or 1 On, so Config list items can point at them.
extern std::array<uint8_t, kModCount> switches; // the open project's set
extern std::array<uint8_t, kModCount> defaults; // the set New Project starts with

inline bool on(Mod mod)
{
    return switches[static_cast<size_t>(mod)] != 0;
}

// Shown in Config -> Mods: how many mods the build has.
struct CountText
{
    char text[4];
};

constexpr CountText makeCountText(size_t count)
{
    CountText t{};
    size_t i = 0;
    if (count >= 100)
    {
        t.text[i++] = static_cast<char>('0' + count / 100 % 10);
    }
    if (count >= 10)
    {
        t.text[i++] = static_cast<char>('0' + count / 10 % 10);
    }
    t.text[i] = static_cast<char>('0' + count % 10);
    return t;
}

constexpr CountText kModCountText = makeCountText(kBuiltModCount);

// Boot, after the file manager: loads the defaults from mods.cfg.
void init(FileSystem* fileSystem);

// Config -> Mods action for every item: saves mods.cfg.
void defaultsChanged();

// Config -> Project Settings -> Mods action for every item: the project changed.
void projectSwitchChanged();

#if MT_HAS_SWING
// The same for Global Swing's switch, which also takes effect at once when playing.
void projectSwingSwitchChanged();
#endif

#if MT_HAS_COLORSONG
// The same for Color Song's switch, which also reads the song again when turned on.
void projectColorsongSwitchChanged();

// Song screen update, with Color Song on.
void colorsongUpdate();
#endif

// After any Config menu edit: mods' settings the menu edits directly are stored if they
// changed, which marks the project changed.
void projectSettingEdited();

// Project file hooks, called from CE's file manager.
void onProjectOpenCopy();  // ProjectOpen, after the Workspace is filled from the project
void onWorkspaceLoaded();  // WorkspaceProjectLoad finish (boot, Open, New Project)
void onWorkspaceSaved();   // WorkspaceProjectSave finish
void onProjectSaved();     // ProjectSave finish (Save and Save As)

} // namespace mods

#endif // MT_MODS

#endif /* MODS_MODS_H_ */
