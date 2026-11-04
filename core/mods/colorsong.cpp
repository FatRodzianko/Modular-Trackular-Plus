// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "colorsong.h"

#if MT_HAS_COLORSONG

#include "modsProject.h"

#ifndef MT_HOST_TEST
#include <cr_section_macros.h>
#endif
#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include "config.h"
#include "fileManager.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <elapsedMillis.h>
#endif

namespace mods
{
namespace colorsong
{

namespace
{

// Step layout, from CE's strStep; the firmware glue below checks it against CE's struct.
constexpr size_t kNote = 0, kInstrument = 1, kFx1Type = 2, kFx1Value = 3, kFx2Type = 4, kFx2Value = 5;
constexpr uint8_t kNoteEmpty = 0xFF;
constexpr uint8_t kLastSampleInstrument = 47; // above: an instrument alone counts as content
constexpr uint8_t kFxMicromove = 2;           // m
constexpr uint8_t kFxVolume = 18;             // V

// A block's key: family << 9 | content hash. The registry below holds each family's
// full hash, so two instrument sets never share a family by accident.
constexpr unsigned kVariantBits = 9;
constexpr unsigned kVariantMask = (1u << kVariantBits) - 1;
constexpr unsigned kFamilyNone = 127; // FX only: no instruments
constexpr uint8_t kSlotNone = 0xFF;
constexpr unsigned kHueSlots = 20; // golden-angle hues at least 12.5 degrees apart
constexpr unsigned kShades = 6;
constexpr uint32_t kVariantPresent = 1u << 24; // variant entry: key | shade << 16 | present

constexpr uint32_t kFnvStart = 2166136261u;
constexpr char kFileName[] = "colorsong.bin";
constexpr char kFileMagic[] = "CSG1";

constexpr size_t kPlaylistSize = 255;

struct PatternEntry
{
    uint16_t mask; // the used-track mask the keys belong to
    volatile uint8_t valid;
    uint8_t reserved;
    uint16_t key[kTracks];
};
static_assert(sizeof(PatternEntry) == 36);

struct Colors
{
    uint32_t familyKey[kMaxFamilies];
    uint32_t variant[kMaxVariants];
    uint8_t familySlot[kFamilyNone + 1];
    uint8_t familyPresent[kFamilyNone + 1];
    uint8_t nextShade[kFamilyNone + 1];
    uint16_t familyCount;
    uint16_t variantCount;
    uint8_t hueCursor;
    uint8_t scanPosition;
    bool scanning;
    bool recordApplied;
    bool resetThisScan;
    uint32_t fingerprints;     // counts every key update
    uint32_t songHash;         // playlist and masks when last checked; 0 = never
    uint32_t laidOutHash;      // the inputs of the colours now handed out
};

// In SDRAM: main-loop tables, except for the one flag patternChanged clears. Startup doesn't
// zero this region, so init does.
#ifdef MT_HOST_TEST
PatternEntry patterns[kPatternCount];
Colors colors;
#else
__NOINIT(EXTERNAL_RAM) PatternEntry patterns[kPatternCount];
__NOINIT(EXTERNAL_RAM) Colors colors;
#endif
volatile bool patternsChanged = false;

inline PatternEntry& entry(uint8_t pattern)
{
    return patterns[pattern - 1];
}

inline uint32_t fnv(uint32_t hash, uint32_t byte)
{
    return (hash ^ (byte & 0xFFu)) * 16777619u;
}

inline uint32_t get32(const uint8_t* b)
{
    return b[0] | b[1] << 8 | b[2] << 16 | static_cast<uint32_t>(b[3]) << 24;
}

inline void put32(uint8_t* b, uint32_t value)
{
    for (int i = 0; i < 4; i++)
    {
        b[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

inline unsigned familyOf(uint32_t key)
{
    return (key & 0xFFFFu) >> kVariantBits;
}

void forgetColors()
{
    colors.familyCount = 0;
    colors.variantCount = 0;
    colors.hueCursor = 0;
    colors.recordApplied = false;
    memset(colors.nextShade, 0, sizeof(colors.nextShade));
    memset(colors.familySlot, kSlotNone, sizeof(colors.familySlot));
    for (PatternEntry& e : patterns)
    {
        e.valid = 0;
    }
    colors.laidOutHash = 0;
}

void startScan()
{
    colors.scanning = true;
    colors.scanPosition = 0;
}

// -- fingerprints ---------------------------------------------------------------------------

int findFamily(uint32_t key)
{
    for (unsigned i = 0; i < colors.familyCount; i++)
    {
        if (colors.familyKey[i] == key)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

unsigned addFamily(uint32_t key)
{
    const int found = findFamily(key);
    if (found >= 0)
    {
        return static_cast<unsigned>(found);
    }
    if (colors.familyCount >= kMaxFamilies)
    {
        return kFamilyNone; // a song with more sets than this: the rest look FX-only
    }
    colors.familyKey[colors.familyCount] = key;
    return colors.familyCount++;
}

inline uint8_t countedFx(uint8_t type)
{
    return type == kFxMicromove || type == kFxVolume ? 0 : type;
}

// The same hashes as 1.2.0, so its colorsong.bin keys still mean these blocks.
uint16_t keyOfTrack(const uint8_t* track, unsigned last)
{
    uint32_t instruments[8] = {};
    bool anyInstrument = false;
    uint32_t shade = fnv(kFnvStart, last);

    for (unsigned s = 0; s <= last; s++)
    {
        const uint8_t* step = track + 1 + s * kStepSize;
        const uint8_t note = step[kNote];
        const uint8_t instrument = step[kInstrument];
        const uint8_t fx1 = countedFx(step[kFx1Type]);
        const uint8_t fx2 = countedFx(step[kFx2Type]);
        const bool hasNote = note != kNoteEmpty;

        if (!hasNote && fx1 == 0 && fx2 == 0 && instrument <= kLastSampleInstrument)
        {
            continue;
        }
        if (hasNote || instrument > kLastSampleInstrument)
        {
            instruments[instrument >> 5] |= 1u << (instrument & 31);
            anyInstrument = true;
        }
        shade = fnv(shade, s);
        shade = fnv(shade, note);
        shade = fnv(shade, hasNote ? instrument : 0);
        shade = fnv(shade, fx1);
        shade = fnv(shade, fx1 != 0 ? step[kFx1Value] : 0);
        shade = fnv(shade, fx2);
        shade = fnv(shade, fx2 != 0 ? step[kFx2Value] : 0);
    }

    uint32_t hue = kFnvStart;
    for (uint32_t word : instruments)
    {
        for (unsigned b = 0; b < 32; b += 8)
        {
            hue = fnv(hue, word >> b);
        }
    }

    const unsigned family = anyInstrument ? addFamily(hue) : kFamilyNone;
    const unsigned content = (shade ^ (shade >> 9) ^ (shade >> 18) ^ (shade >> 27)) & kVariantMask;
    return static_cast<uint16_t>(family << kVariantBits | content);
}

// The registry never shrinks, so after many edits it can fill up. It starts again once per
// scan, and the project's record brings the colours back; a song that fills it by itself
// doesn't get here twice.
bool makeRoom()
{
    if (colors.familyCount <= kMaxFamilies - kTracks || colors.resetThisScan)
    {
        return true;
    }
    forgetColors();
    colors.resetThisScan = true;
    startScan();
    return false;
}

void fingerprint(uint8_t pattern, const uint8_t* tracks, uint16_t mask)
{
    PatternEntry& e = entry(pattern);
    e.valid = 1; // before reading: a change arriving meanwhile clears it and it's read again
    unsigned last = tracks[0];
    if (last >= kSteps)
    {
        last = kSteps - 1;
    }
    for (size_t t = 0; t < kTracks; t++)
    {
        e.key[t] = keyOfTrack(tracks + t * kTrackSize, last);
    }
    e.mask = mask;
    colors.fingerprints++;
}

// One step of the scan. False if it has to wait.
bool scanStep(Song& song)
{
    while (colors.scanPosition < kPlaylistSize)
    {
        const uint8_t pattern = song.slot(colors.scanPosition);
        if (pattern == 0)
        {
            break;
        }

        const uint16_t mask = song.mask(pattern);
        const PatternEntry& e = entry(pattern);
        if (mask == 0 || (e.valid != 0 && e.mask == mask))
        {
            colors.scanPosition++;
            continue;
        }

        if (!makeRoom())
        {
            return false;
        }

        const uint8_t* unsaved = song.unsavedTracks(pattern);
        if (unsaved != nullptr)
        {
            fingerprint(pattern, unsaved, mask);
            // The audio interrupt can move on to the next pattern at any time.
            if (song.unsavedTracks(pattern) != unsaved)
            {
                entry(pattern).valid = 0;
                return false;
            }
            colors.scanPosition++;
            continue;
        }

        if (!song.mayReadFile())
        {
            return false;
        }
        colors.scanPosition++;
        const uint8_t* tracks = song.readTracks(pattern);
        if (tracks != nullptr)
        {
            fingerprint(pattern, tracks, mask);
        }
        return false; // one file per update
    }

    colors.scanning = false;
    colors.resetThisScan = false;
    return true;
}

// -- handing out colours --------------------------------------------------------------------

int findVariant(uint32_t key)
{
    for (unsigned i = 0; i < colors.variantCount; i++)
    {
        if ((colors.variant[i] & 0xFFFFu) == key)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Runs body(key) for every filled block in the song that has a current key, top to bottom,
// tracks left to right.
template <class Body>
void forEachBlock(Song& song, Body body)
{
    for (size_t i = 0; i < kPlaylistSize; i++)
    {
        const uint8_t pattern = song.slot(i);
        if (pattern == 0)
        {
            break;
        }
        const PatternEntry& e = entry(pattern);
        const uint16_t mask = song.mask(pattern);
        if (e.valid == 0 || e.mask != mask)
        {
            continue;
        }
        for (size_t t = 0; t < kTracks; t++)
        {
            if ((mask & (1u << t)) != 0)
            {
                body(e.key[t]);
            }
        }
    }
}

// New colours come from a rotation: from the cursor, the first one nothing in the song
// uses. A colour an edit released only comes back when the rotation has gone round.
bool hueSlotUsed(unsigned slot)
{
    for (unsigned f = 0; f <= kFamilyNone; f++)
    {
        if (colors.familyPresent[f] != 0 && colors.familySlot[f] == slot)
        {
            return true;
        }
    }
    return false;
}

unsigned nextHueSlot()
{
    const unsigned start = colors.hueCursor % kHueSlots;
    unsigned slot = start;
    for (unsigned k = 0; k < kHueSlots; k++)
    {
        const unsigned candidate = (start + k) % kHueSlots;
        if (!hueSlotUsed(candidate))
        {
            slot = candidate;
            break;
        }
    }
    colors.hueCursor = static_cast<uint8_t>((slot + 1) % kHueSlots);
    return slot;
}

bool shadeUsed(unsigned family, unsigned shade)
{
    for (unsigned i = 0; i < colors.variantCount; i++)
    {
        const uint32_t v = colors.variant[i];
        if ((v & kVariantPresent) != 0 && familyOf(v) == family && ((v >> 16) & 0xFFu) == shade)
        {
            return true;
        }
    }
    return false;
}

unsigned nextShade(unsigned family)
{
    const unsigned start = colors.nextShade[family] % kShades;
    unsigned shade = start;
    for (unsigned k = 0; k < kShades; k++)
    {
        const unsigned candidate = (start + k) % kShades;
        if (!shadeUsed(family, candidate))
        {
            shade = candidate;
            break;
        }
    }
    colors.nextShade[family] = static_cast<uint8_t>((shade + 1) % kShades);
    return shade;
}

void handOutColors(Song& song)
{
    // What is in the song now? Everything still there keeps its colour.
    memset(colors.familyPresent, 0, sizeof(colors.familyPresent));
    for (unsigned i = 0; i < colors.variantCount; i++)
    {
        colors.variant[i] &= ~kVariantPresent;
    }
    forEachBlock(song, [](uint16_t key) {
        colors.familyPresent[familyOf(key)] = 1;
        const int v = findVariant(key);
        if (v >= 0)
        {
            colors.variant[v] |= kVariantPresent;
        }
    });

    // What left the song gives its colour up.
    unsigned kept = 0;
    for (unsigned i = 0; i < colors.variantCount; i++)
    {
        if ((colors.variant[i] & kVariantPresent) != 0)
        {
            colors.variant[kept++] = colors.variant[i];
        }
    }
    colors.variantCount = static_cast<uint16_t>(kept);
    for (unsigned f = 0; f <= kFamilyNone; f++)
    {
        if (colors.familyPresent[f] == 0)
        {
            colors.familySlot[f] = kSlotNone;
        }
    }

    // What's new takes a free one, in song order.
    forEachBlock(song, [](uint16_t key) {
        const unsigned family = familyOf(key);
        if (family != kFamilyNone && colors.familySlot[family] == kSlotNone)
        {
            colors.familySlot[family] = static_cast<uint8_t>(nextHueSlot());
        }
        if (findVariant(key) < 0 && colors.variantCount < kMaxVariants)
        {
            colors.variant[colors.variantCount++] =
              key | static_cast<uint32_t>(nextShade(family)) << 16 | kVariantPresent;
        }
    });
}

// -- the record -----------------------------------------------------------------------------
// The image of 1.2.0's colorsong.bin: magic "CSG1", u16 families, u16 variants, u32 checksum
// (the sum of each record's FNV-1a hash, so it doesn't depend on order), then 8-byte records:
// family {u32 set key, u8 hue slot, 3 zero}, variant {u32 set key (0: no instruments), u16
// content key, u8 shade, 1 zero}. Only what is in the song is kept.

uint32_t recordHash(const uint8_t* r)
{
    uint32_t h = kFnvStart;
    for (size_t i = 0; i < kFileRecordSize; i++)
    {
        h = fnv(h, r[i]);
    }
    return h;
}

// True if image is a well-formed colorsong.bin of this size. Empty ones don't count.
bool imageOk(const uint8_t* image, size_t size)
{
    if (size < kFileHeaderSize || memcmp(image, kFileMagic, 4) != 0)
    {
        return false;
    }
    const size_t families = image[4] | image[5] << 8;
    const size_t variants = image[6] | image[7] << 8;
    if (families > kMaxFamilies || variants > kMaxVariants || families + variants == 0 ||
        size != kFileHeaderSize + kFileRecordSize * (families + variants))
    {
        return false;
    }
    uint32_t sum = 0;
    for (size_t i = 0; i < families + variants; i++)
    {
        sum += recordHash(image + kFileHeaderSize + i * kFileRecordSize);
    }
    return sum == get32(image + 8);
}

void applyRecord(const Store& store)
{
    uint16_t length = 0;
    const uint8_t* image = store.find(kRecord, nullptr, &length);
    if (image == nullptr || !imageOk(image, length))
    {
        return;
    }
    const size_t familyRecords = image[4] | image[5] << 8;
    const size_t variantRecords = image[6] | image[7] << 8;
    const uint8_t* r = image + kFileHeaderSize;

    for (size_t i = 0; i < familyRecords; i++, r += kFileRecordSize)
    {
        const int family = findFamily(get32(r));
        if (family >= 0 && r[4] < kSlotNone)
        {
            colors.familySlot[family] = static_cast<uint8_t>(r[4] % kHueSlots);
        }
    }
    for (size_t i = 0; i < variantRecords; i++, r += kFileRecordSize)
    {
        const uint32_t setKey = get32(r);
        const int found = setKey == 0 ? static_cast<int>(kFamilyNone) : findFamily(setKey);
        if (found < 0)
        {
            continue;
        }
        const uint32_t key =
          static_cast<uint32_t>(found) << kVariantBits | ((r[4] | r[5] << 8) & kVariantMask);
        if (findVariant(key) < 0 && colors.variantCount < kMaxVariants)
        {
            colors.variant[colors.variantCount++] = key | static_cast<uint32_t>(r[6] % kShades) << 16;
        }
    }

    // The rotations go on after the saved colours, so the first edit doesn't get back one
    // the song is using.
    int top = -1;
    for (unsigned f = 0; f < kFamilyNone; f++)
    {
        if (colors.familySlot[f] != kSlotNone && colors.familySlot[f] >= top)
        {
            top = colors.familySlot[f];
        }
    }
    colors.hueCursor = static_cast<uint8_t>((top + 1) % kHueSlots);
    for (unsigned i = 0; i < colors.variantCount; i++)
    {
        const unsigned family = familyOf(colors.variant[i]);
        const unsigned shade = (colors.variant[i] >> 16) & 0xFFu;
        if (colors.nextShade[family] <= shade)
        {
            colors.nextShade[family] = static_cast<uint8_t>(shade + 1);
        }
    }
    for (uint8_t& shade : colors.nextShade)
    {
        shade %= kShades;
    }
}

// Visits every record of the colours now in the song.
template <class Visit>
void forEachRecord(Visit visit)
{
    uint8_t r[kFileRecordSize];
    for (unsigned f = 0; f < kFamilyNone; f++)
    {
        if (colors.familyPresent[f] != 0 && colors.familySlot[f] != kSlotNone)
        {
            put32(r, colors.familyKey[f]);
            r[4] = colors.familySlot[f];
            r[5] = r[6] = r[7] = 0;
            visit(r, true);
        }
    }
    for (unsigned i = 0; i < colors.variantCount; i++)
    {
        const uint32_t v = colors.variant[i];
        const unsigned family = familyOf(v);
        const unsigned content = v & kVariantMask;
        if ((v & kVariantPresent) != 0)
        {
            put32(r, family == kFamilyNone ? 0 : colors.familyKey[family]);
            r[4] = static_cast<uint8_t>(content);
            r[5] = static_cast<uint8_t>(content >> 8);
            r[6] = static_cast<uint8_t>(v >> 16);
            r[7] = 0;
            visit(r, false);
        }
    }
}

bool storeIfChanged(Store& store)
{
    uint32_t sum = 0;
    size_t families = 0, variants = 0;
    forEachRecord([&](const uint8_t* r, bool isFamily) {
        sum += recordHash(r);
        (isFamily ? families : variants)++;
    });

    uint16_t length = 0;
    const uint8_t* old = store.find(kRecord, nullptr, &length);
    if (old == nullptr && families + variants == 0)
    {
        return false;
    }
    const size_t size = kFileHeaderSize + kFileRecordSize * (families + variants);
    if (old != nullptr && length == size && imageOk(old, length) && get32(old + 8) == sum)
    {
        return false;
    }

    uint8_t* image = store.reserve(kRecord, kRecordVersion, static_cast<uint16_t>(size));
    if (image == nullptr)
    {
        return false; // no room: the colours still work, and aren't saved
    }
    memcpy(image, kFileMagic, 4);
    image[4] = static_cast<uint8_t>(families);
    image[5] = static_cast<uint8_t>(families >> 8);
    image[6] = static_cast<uint8_t>(variants);
    image[7] = static_cast<uint8_t>(variants >> 8);
    put32(image + 8, sum);
    uint8_t* out = image + kFileHeaderSize;
    forEachRecord([&out](const uint8_t* r, bool) {
        memcpy(out, r, kFileRecordSize);
        out += kFileRecordSize;
    });
    return true;
}

// -- drawing --------------------------------------------------------------------------------

// HSL (h 0..359, s and l 0..100) to 0x00RRGGBB, in integers.
uint32_t hslToRgb(unsigned h, unsigned s, unsigned l)
{
    const unsigned c = (100 - (2 * l > 100 ? 2 * l - 100 : 100 - 2 * l)) * s; // chroma x 10000
    const unsigned hp = h % 120;
    const unsigned d = hp >= 60 ? hp - 60 : 60 - hp;
    const unsigned x = c * (60 - d) / 60;
    const unsigned m = l * 100 - c / 2;
    unsigned r, g, b;
    switch (h / 60)
    {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }
    r = ((r + m) * 255 + 5000) / 10000;
    g = ((g + m) * 255 + 5000) / 10000;
    b = ((b + m) * 255 + 5000) / 10000;
    return r << 16 | g << 8 | b;
}

unsigned lightnessOf(uint32_t rgb)
{
    const unsigned r = (rgb >> 16) & 0xFFu, g = (rgb >> 8) & 0xFFu, b = rgb & 0xFFu;
    unsigned high = r, low = r;
    high = g > high ? g : high;
    high = b > high ? b : high;
    low = g < low ? g : low;
    low = b < low ? b : low;
    return (high + low) * 100 / 510;
}

// Well-separated shades, in the order a family's variants first get them.
constexpr uint8_t kShadeSaturation[kShades] = { 78, 92, 70, 42, 88, 62 };
constexpr uint8_t kShadeLightness[kShades] = { 50, 70, 34, 58, 80, 26 };
constexpr unsigned kThemeDistance = 7; // from the empty block's lightness: 14 on CE's themes

} // namespace

void reset()
{
    forgetColors();
    colors.scanning = false;
    colors.resetThisScan = false;
    colors.songHash = 0;
    patternsChanged = false;
}

bool update(Song& song, Store& store)
{
    if (patternsChanged)
    {
        patternsChanged = false; // first: a change arriving during the scan restarts it
        startScan();
    }

    uint32_t hash = kFnvStart;
    for (size_t i = 0; i < kPlaylistSize; i++)
    {
        const uint8_t pattern = song.slot(i);
        hash = fnv(hash, pattern);
        if (pattern == 0)
        {
            break;
        }
        const uint16_t mask = song.mask(pattern);
        hash = fnv(fnv(hash, mask), mask >> 8);
    }
    hash |= 1;
    if (hash != colors.songHash)
    {
        colors.songHash = hash;
        startScan();
    }

    if (colors.scanning && !scanStep(song))
    {
        return false;
    }

    // Keys and song both count: an edit can leave the song's masks as they were.
    const uint32_t inputs = fnv(fnv(hash, colors.fingerprints), colors.fingerprints >> 8) | 1;
    if (inputs == colors.laidOutHash)
    {
        return false;
    }
    colors.laidOutHash = inputs;

    if (!colors.recordApplied)
    {
        applyRecord(store);
        colors.recordApplied = true;
    }
    handOutColors(song);
    return storeIfChanged(store);
}

void init()
{
    memset(patterns, 0, sizeof(patterns));
    memset(&colors, 0, sizeof(colors));
    patternsChanged = false;
}

void patternChanged(uint8_t pattern)
{
    if (pattern != 0)
    {
        entry(pattern).valid = 0;
        patternsChanged = true;
    }
}

void switchEdited()
{
    if (on(Mod::colorsong))
    {
        for (PatternEntry& e : patterns)
        {
            e.valid = 0;
        }
        startScan();
    }
}

uint32_t blockColor(uint8_t pattern,
                    uint8_t track,
                    uint16_t mask,
                    bool muted,
                    uint32_t themeColor,
                    uint32_t emptyColor)
{
    if (pattern == 0 || track >= kTracks)
    {
        return themeColor;
    }
    const PatternEntry& e = entry(pattern);
    if (e.valid == 0 || e.mask != mask)
    {
        return themeColor;
    }

    const uint16_t key = e.key[track];
    const unsigned family = familyOf(key);
    const int variant = findVariant(key);
    const uint8_t slot = colors.familySlot[family];
    if (variant < 0 || (family != kFamilyNone && slot == kSlotNone))
    {
        return themeColor;
    }

    const unsigned shade = ((colors.variant[variant] >> 16) & 0xFFu) % kShades;
    const unsigned hue = family == kFamilyNone ? 0 : (slot % kHueSlots) * 1375u / 10u % 360u;
    unsigned saturation = family == kFamilyNone ? 6 : kShadeSaturation[shade];
    unsigned lightness = kShadeLightness[shade];
    if (muted)
    {
        saturation /= 2;
        lightness = lightness * 45 / 100;
    }

    // The theme decides which way the block has to stand out from its empty neighbours.
    const unsigned empty = lightnessOf(emptyColor);
    if (empty <= 50)
    {
        lightness = lightness < empty + kThemeDistance ? empty + kThemeDistance : lightness;
    }
    else
    {
        lightness = lightness > empty - kThemeDistance ? empty - kThemeDistance : lightness;
    }
    return hslToRgb(hue, saturation, lightness);
}

bool importSideFile(FileIo& io, const char* projectFolder, Store& store)
{
    char path[project::kPathSize];
    const int n = snprintf(path, sizeof(path), "%s/%s", projectFolder, kFileName);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path))
    {
        return false;
    }

    uint8_t head[kFileHeaderSize];
    const size_t size = io.read(path, head, sizeof(head));
    if (size == FileIo::kReadFailed || size < kFileHeaderSize || size > kMaxRecordSize ||
        memcmp(head, kFileMagic, 4) != 0)
    {
        return false;
    }

    uint8_t* image = store.reserve(kRecord, kRecordVersion, static_cast<uint16_t>(size));
    if (image == nullptr)
    {
        return false;
    }
    if (io.read(path, image, size) != size || !imageOk(image, size))
    {
        store.remove(kRecord);
        return false;
    }
    return true;
}

#ifndef MT_HOST_TEST

namespace
{

using CePattern = Sequencer::strPattern;
using CeTrack = CePattern::strTrack;
using CeStep = CeTrack::strStep;

static_assert(sizeof(CeTrack) == kTrackSize);
static_assert(kAllTracks == kTracks);
static_assert(offsetof(CeTrack, step) == 1 && sizeof(CeStep) == kStepSize);
static_assert(offsetof(CeStep, note) == kNote && offsetof(CeStep, instrument) == kInstrument);
static_assert(offsetof(CeStep, fx) == kFx1Type && sizeof(CeStep::strFx) == 2);
static_assert(Sequencer::strFxConsts::FX_TYPE_MICROMOVE == kFxMicromove);
static_assert(Sequencer::strFxConsts::FX_TYPE_VOLUME == kFxVolume);
static_assert(static_cast<uint8_t>(Sequencer::STEP_NOTE_EMPTY) == kNoteEmpty);
static_assert(MIDI_INSTRUMENTS_START_IDX == kLastSampleInstrument + 1);
static_assert(PATTERN_INDEX_MAX == kPatternCount);
static_assert(SONG_MAX >= kPlaylistSize);

// While playing, one file every so often: a pattern read holds up the main loop as long as
// CE's own at each pattern change, and sample streaming runs there too.
constexpr uint32_t kPlayingReadGapMs = 100;

class CeSong : public Song
{
  public:
    uint8_t slot(size_t position) override { return mtProject.song.playlist[position]; }

    uint16_t mask(uint8_t pattern) override
    {
        return static_cast<uint16_t>(mtProject.values.allPatternsBitmask[pattern - 1] |
                                     mtProject.values.allPatternsBitmaskMidi[pattern - 1] << 8);
    }

    const uint8_t* unsavedTracks(uint8_t pattern) override
    {
        if (pattern != mtProject.values.actualPattern ||
            !newFileManager.isPatternStructChanged(pattern) ||
            sequencer.isPerformancePatternChanged())
        {
            return nullptr;
        }
        return reinterpret_cast<const uint8_t*>(sequencer.getActualPattern()->track);
    }

    bool mayReadFile() override
    {
        // The temp buffer and the file transfer are also the file manager's.
        return newFileManager.getStatus() == fmIdle &&
               (!sequencer.isPlay() || sinceRead >= kPlayingReadGapMs);
    }

    const uint8_t* readTracks(uint8_t pattern) override
    {
        sinceRead = 0;
        const CePattern* p = newFileManager.loadWorkspacePatternToTempBuffer(pattern);
        return p != nullptr ? reinterpret_cast<const uint8_t*>(p->track) : nullptr;
    }

  private:
    elapsedMillis sinceRead;
};

CeSong song;

} // namespace

Song& ceSong()
{
    return song;
}

#endif // MT_HOST_TEST

} // namespace colorsong
} // namespace mods

#endif // MT_HAS_COLORSONG
