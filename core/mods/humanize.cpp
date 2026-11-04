// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "humanize.h"

#if MT_HAS_HUMANIZE

#include "modsProject.h"

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include "mtSequencer.h"
#include "util_math.h"
#endif

namespace mods
{
namespace humanize
{

Settings settings;

namespace
{

constexpr size_t kRecordSize = 5;
static_assert(sizeof(Settings) == kRecordSize);

// humanize.bin from the 1.2.0 patch: magic, the values, a checksum. The values are
// timing, volume on, MIDI, sample, volume range, with numbers as 0..99 like ours.
constexpr char kSideFileName[] = "humanize.bin";
constexpr char kSideMagic2[] = "HHZ2"; // 5 values and 3 zero bytes
constexpr size_t kSideSize2 = 16;
constexpr size_t kSideData2 = 8;
constexpr char kSideMagic1[] = "HHZ1"; // 4 values: one range for both
constexpr size_t kSideSize1 = 12;
constexpr size_t kSideData1 = 4;

bool inRange(uint8_t value)
{
    return value <= 99;
}

bool valid(const Settings& s)
{
    return inRange(s.timingRange) && s.volumeOn <= 1 && inRange(s.volumeRange) &&
           inRange(s.midiVelocity) && inRange(s.sampleVolume);
}

Settings fromRecord(const Store& store)
{
    Settings s;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    if (data != nullptr && length == kRecordSize)
    {
        Settings stored;
        memcpy(&stored, data, kRecordSize);
        if (valid(stored))
        {
            s = stored;
        }
    }
    return s;
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

} // namespace

void load(const Store& store)
{
    settings = fromRecord(store);
}

bool storeIfEdited(Store& store)
{
    const Settings stored = fromRecord(store);
    if (memcmp(&stored, &settings, kRecordSize) == 0)
    {
        return false;
    }
    Settings copy = settings;
    return store.put(kRecord, kRecordVersion, &copy, kRecordSize);
}

bool importSideFile(FileIo& io, const char* projectFolder, Store& store)
{
    char path[project::kPathSize];
    const int n = snprintf(path, sizeof(path), "%s/%s", projectFolder, kSideFileName);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(path))
    {
        return false;
    }

    uint8_t file[kSideSize2];
    const size_t size = io.read(path, file, sizeof(file));
    const bool two = size == kSideSize2 && memcmp(file, kSideMagic2, 4) == 0;
    const bool one = size == kSideSize1 && memcmp(file, kSideMagic1, 4) == 0;
    if (!two && !one)
    {
        return false;
    }
    const size_t dataSize = two ? kSideData2 : kSideData1;
    if (get32(file + 4 + dataSize) != sideSum(file + 4, dataSize))
    {
        return false;
    }

    const uint8_t* v = file + 4;
    Settings s;
    s.timingRange = v[0];
    s.volumeOn = v[1];
    s.midiVelocity = v[2];
    s.sampleVolume = v[3];
    s.volumeRange = two ? v[4] : v[0];
    if (!valid(s))
    {
        return false;
    }
    return store.put(kRecord, kRecordVersion, &s, kRecordSize);
}

int pick(int center, int range, int lo, int hi, Random random)
{
    int a = center - range / 2;
    int b = a + range;
    if (b - a >= hi - lo)
    {
        a = lo;
        b = hi;
    }
    else if (a < lo)
    {
        b += lo - a;
        a = lo;
    }
    else if (b > hi)
    {
        a -= b - hi;
        b = hi;
    }
    return random(a, b);
}

#ifndef MT_HOST_TEST

namespace
{

int ceRandom(int lo, int hi)
{
    return utilMathRandom::random(lo, hi + 1);
}

} // namespace

void applyToSelection()
{
    Sequencer::strSelection* sel = &sequencer.selection;
    if (!sequencer.isSelectionCorrect(sel))
    {
        return;
    }

    const FxTypes types{ sequencer.fx.FX_TYPE_MICROMOVE, sequencer.fx.FX_TYPE_VOLUME };
    Sequencer::strPattern* pattern = sequencer.getPatternToUI();
    for (uint8_t t = sel->firstTrack; t <= sel->lastTrack; t++)
    {
        for (uint8_t s = sel->firstStep; s <= sel->lastStep; s++)
        {
            auto& step = pattern->track[t].step[s];
            humanizeStep(step, settings, isSampleInstrument(step.instrument), types, ceRandom);
        }
    }
}

#endif // MT_HOST_TEST

} // namespace humanize
} // namespace mods

#endif // MT_HAS_HUMANIZE
