// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "resample.h"

#if MT_HAS_RESAMPLE

#include "modsProject.h"

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"

extern strMtProject mtProject;
#endif

namespace mods
{
namespace resample
{

Settings settings;

namespace
{

constexpr uint16_t kRecordSize = 1;

// resample.bin from the 1.2.0 patch, 12 bytes: "RSM1", the settings byte, three zero bytes,
// then a little-endian FNV-1a over bytes 4 to 7.
constexpr char kSideFileName[] = "resample.bin";
constexpr char kSideMagic[4] = { 'R', 'S', 'M', '1' };
constexpr size_t kSideSize = 12;
constexpr size_t kSideDataStart = 4;
constexpr size_t kSideDataSize = 4;
constexpr size_t kSideSumStart = kSideDataStart + kSideDataSize;

uint32_t fnv1a(const uint8_t* bytes, size_t count)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < count; i++)
    {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

uint32_t readLe32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) | static_cast<uint32_t>(bytes[1]) << 8 |
           static_cast<uint32_t>(bytes[2]) << 16 | static_cast<uint32_t>(bytes[3]) << 24;
}

} // namespace

uint8_t toBits(const Settings& s)
{
    if (!s.trkAudio)
    {
        return 0;
    }
    return static_cast<uint8_t>(kTrkAudioBit | (s.trkRecord ? kTrkRecordBit : 0));
}

bool fromBits(uint8_t bits, Settings* s)
{
    if (bits > kMaxBits)
    {
        return false;
    }
    s->trkRecord = (bits & kTrkRecordBit) != 0;
    s->trkAudio = s->trkRecord || (bits & kTrkAudioBit) != 0;
    return true;
}

void load(const Store& store)
{
    Settings s;
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    if (data == nullptr || length != kRecordSize || !fromBits(data[0], &s))
    {
        s = Settings{};
    }
    settings = s;
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
    if (io.read(path, file, sizeof(file)) != kSideSize ||
        memcmp(file, kSideMagic, sizeof(kSideMagic)) != 0 ||
        readLe32(file + kSideSumStart) != fnv1a(file + kSideDataStart, kSideDataSize))
    {
        return false;
    }

    Settings s;
    if (!fromBits(file[kSideDataStart], &s))
    {
        return false;
    }
    const uint8_t bits = toBits(s);
    return store.put(kRecord, kRecordVersion, &bits, kRecordSize) && bits != 0;
}

volatile uint8_t engineOpen = 0;

namespace
{
bool visitOn = false;
Visit visit;
bool gatesAreOpen = false;   // main loop's view; engineOpen follows it
bool mixerConnected = false; // this visit's last mixer call was a connect

void forgetGates()
{
    engineOpen = 0;
    gatesAreOpen = false;
    mixerConnected = false;
    visit = Visit{};
}
} // namespace

Buttons buttonsFor(bool lineIn, bool stereo, bool mic)
{
    if (lineIn)
    {
        return stereo ? Buttons::both : Buttons::trkAudio;
    }
    return mic ? Buttons::trkAudio : Buttons::none;
}

// The gates are closed at both ends: CE's entry follows a start, and CE's stop() undoes the
// entry whether they were open or not (section 4.3, "Leaving").
void visitStarted(bool modOn)
{
    visitOn = modOn;
    forgetGates();
}

void visitEnded()
{
    visitOn = false;
    forgetGates();
}

bool inVisit()
{
    return visitOn;
}

bool press(uint8_t slot, Buttons buttons, Settings* s)
{
    if (slot == kTrkAudioSlot && buttons != Buttons::none)
    {
        s->trkAudio = !s->trkAudio;
        if (!s->trkAudio)
        {
            s->trkRecord = false;
        }
        return true;
    }
    if (slot == kTrkRecordSlot && buttons == Buttons::both)
    {
        s->trkRecord = !s->trkRecord;
        if (s->trkRecord)
        {
            s->trkAudio = true;
        }
        return true;
    }
    return false;
}

bool store(const Settings& s, Store& store)
{
    const uint8_t bits = toBits(s);
    uint16_t length = 0;
    const uint8_t* data = store.find(kRecord, nullptr, &length);
    if (data != nullptr && length == kRecordSize && data[0] == bits)
    {
        return false;
    }
    return store.put(kRecord, kRecordVersion, &bits, kRecordSize);
}

const char* title(uint8_t slot, Buttons buttons)
{
    if (slot == kTrkAudioSlot && buttons != Buttons::none)
    {
        return "Trk Audio";
    }
    if (slot == kTrkRecordSlot && buttons == Buttons::both)
    {
        return "Trk Record";
    }
    return nullptr;
}

const char* value(uint8_t slot, const Settings& s)
{
    const bool isOn = slot == kTrkRecordSlot ? s.trkRecord : s.trkAudio;
    return isOn ? "On" : "Off";
}

bool gatesWanted(bool visitOn, const Settings& s, const Visit& v, const RecorderView& view)
{
    if (!visitOn || !s.trkAudio || !view.buttonShown || !v.entered || v.samplesOverwritten ||
        view.samplesBusy)
    {
        return false;
    }
    return view.takeRunning ? v.takeMoved : view.onSetup;
}

uint32_t framesInRam(bool streamed, uint32_t ramFrames, uint32_t length)
{
    if (streamed || ramFrames > 0)
    {
        return ramFrames;
    }
    return length;
}

uintptr_t sampleEnd(uintptr_t address, uint32_t frames, bool stereo)
{
    return address + static_cast<uintptr_t>(frames) * (stereo ? 2u : 1u) * sizeof(int16_t);
}

Placement place(uintptr_t bankStart, uintptr_t bankEnd, uintptr_t loadedEnd, uint8_t channels)
{
    Placement p;
    if (channels == 0 || bankEnd <= bankStart)
    {
        return p;
    }
    uintptr_t start = loadedEnd > bankStart ? loadedEnd : bankStart;
    start = (start + (kTakeStartAlign - 1)) & ~static_cast<uintptr_t>(kTakeStartAlign - 1);
    if (start >= bankEnd)
    {
        return p;
    }
    const uint32_t perChannel =
      static_cast<uint32_t>((bankEnd - start) / channels) & ~(kTakeBlockBytes - 1);
    if (perChannel < kMinTakeFrames * sizeof(int16_t))
    {
        return p;
    }
    p.address = start;
    p.size = perChannel * channels;
    return p;
}

#ifndef MT_HOST_TEST

// Each one as CE's recorder entry and exit (fSampleRec.cpp start and stop) make it, in their
// order. connectMixerImpact and disconnectMixerImpact go strictly in turns: a second disconnect
// would store the defaults over the project's mix and lose it.
namespace
{
void openGates()
{
    engine.unblockAllNoteOn();
    sequencer.setSynthMute(Sequencer::MuteState::kUnmuted);
    engine.unblockDelayRefresh();
    if (!mixerConnected)
    {
        engine.connectMixerImpact();
        mixerConnected = true;
    }
    engine.setLimiterSidechain(mtProject.values.limiterSidechain);
    engine.refreshAllSends();
    engine.refreshTrackVolume();
    engineOpen = 1;
    gatesAreOpen = true;
}

void closeGates()
{
    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
    {
        sequencer.stop();
    }
    engineOpen = 0;
    engine.blockAllNoteOn();
    sequencer.setSynthMute(Sequencer::MuteState::kMuted);
    engine.blockDelayRefresh();
    engine.clearDelay();
    engine.clearReverb();
    if (mixerConnected)
    {
        engine.disconnectMixerImpact();
        mixerConnected = false;
    }
    engine.setLimiterSidechain(0);
    gatesAreOpen = false;
}
} // namespace

void recorderEntered()
{
    if (!visitOn)
    {
        return;
    }
    visit.entered = true;
    mixerConnected = false; // CE's entry just disconnected
}

void recorderUpdate(const RecorderView& view)
{
    if (!visitOn)
    {
        return;
    }
    if (!view.takeRunning)
    {
        visit.takeMoved = false;
    }
    const bool want = gatesWanted(visitOn, settings, visit, view);
    if (want && !gatesAreOpen)
    {
        openGates();
    }
    else if (!want && gatesAreOpen)
    {
        closeGates();
    }
}

// address and size come in as CE's buffer, the whole sample memory.
void placeBuffer(int16_t** address, uint32_t* size, int channels)
{
    if (!visitOn)
    {
        return;
    }
    visit.takeMoved = false;
    if (gatesAreOpen)
    {
        const uintptr_t bankStart = reinterpret_cast<uintptr_t>(*address);
        uintptr_t loadedEnd = bankStart;
        for (int i = 0; i < kSampleInstrumentsCount; i++)
        {
            const strInstrument& instrument = mtProject.instrument[i];
            const strSampleMemory& memory = mtProject.sampleMemory[i];
            if (!instrument.isActive || instrument.sample.address == nullptr)
            {
                continue;
            }
            const bool stereo =
              memory.isStereo ||
              memory.address[strSampleMemory::Channel::kRight] !=
                memory.address[strSampleMemory::Channel::kLeft];
            const uintptr_t end =
              sampleEnd(reinterpret_cast<uintptr_t>(instrument.sample.address),
                        framesInRam(memory.isStreamed, memory.ramFrames, instrument.sample.length),
                        stereo);
            if (end > loadedEnd)
            {
                loadedEnd = end;
            }
        }
        const Placement p =
          place(bankStart, bankStart + *size, loadedEnd, static_cast<uint8_t>(channels));
        if (p.size != 0)
        {
            *address = reinterpret_cast<int16_t*>(p.address);
            *size = p.size;
            visit.takeMoved = true;
            return;
        }
        closeGates(); // too little room: the take is CE's, over the samples, and silent
    }
    visit.samplesOverwritten = true;
}

#endif // MT_HOST_TEST

} // namespace resample
} // namespace mods

#endif // MT_HAS_RESAMPLE
