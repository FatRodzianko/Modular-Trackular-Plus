// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Which builds carry the mods. They target the Tracker+ only: Original Tracker and
// Tracker Mini builds stay plain CE. Host tests define MT_HOST_TEST.
//
// Each mod also has its own flag, MT_HAS_<NAME>, on by default in a mods build. A
// build can leave one out with CUSTOM_DEFINES=-DMT_HAS_<NAME>=0, or every mod with
// -DMT_NO_MODS (the plain-CE reference build the tests compare against).

#ifndef MODS_MODSBUILD_H_
#define MODS_MODSBUILD_H_

#if !defined(MT_NO_MODS) &&                                                                \
  (defined(MT_HOST_TEST) || (defined(PLATFORM_IMX) && !defined(TRACKER_MINI)))
#define MT_MODS 1
#else
#define MT_MODS 0
#endif

#ifndef MT_HAS_COLORSONG
#define MT_HAS_COLORSONG MT_MODS
#elif !MT_MODS
#undef MT_HAS_COLORSONG
#define MT_HAS_COLORSONG 0
#endif

#ifndef MT_HAS_HUMANIZE
#define MT_HAS_HUMANIZE MT_MODS
#elif !MT_MODS
#undef MT_HAS_HUMANIZE
#define MT_HAS_HUMANIZE 0
#endif

#ifndef MT_HAS_SWING
#define MT_HAS_SWING MT_MODS
#elif !MT_MODS
#undef MT_HAS_SWING
#define MT_HAS_SWING 0
#endif

#endif /* MODS_MODSBUILD_H_ */
