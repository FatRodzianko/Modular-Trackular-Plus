// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "resample.h"

#if MT_HAS_RESAMPLE

#include "modsProject.h"

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include "Audio.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include "mtStructs.h"

#include <new>

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

// Hook 16: the sources (pads 0 to 47, MIDI notes 100 to 227) whose note-on went past CE's record
// branch, one byte each so the main loop and the interrupt never share a word they rewrite.
constexpr int kSources = 256;
volatile uint8_t playedOnly[kSources];

void forgetGates()
{
    engineOpen = 0;
    liveOn = 0;
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
    // Not at the visit's end: a pad held while leaving still lets go the way it started.
    for (int i = 0; i < kSources; i++)
    {
        playedOnly[i] = 0;
    }
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

Arm armOnRec(Arm now, bool shift, bool gatesOpen, bool onSetup)
{
    if (shift && gatesOpen && onSetup && now == Arm::none)
    {
        return Arm::pattern;
    }
    return now;
}

bool playArms(bool gatesOpen, bool countingIn, bool playing)
{
    return gatesOpen && countingIn && !playing;
}

bool tapWanted(const Settings& s, bool recordShown, int channels, bool takeMoved)
{
    return s.trkRecord && recordShown && channels == 2 && takeMoved;
}

void addClipped(int16_t* dest, const int16_t* lineIn, const int16_t* tracker, uint32_t samples)
{
    for (uint32_t i = 0; i < samples; i++)
    {
        int32_t sum = static_cast<int32_t>(lineIn[i]) + static_cast<int32_t>(tracker[i]);
        if (sum > INT16_MAX)
        {
            sum = INT16_MAX;
        }
        else if (sum < INT16_MIN)
        {
            sum = INT16_MIN;
        }
        dest[i] = static_cast<int16_t>(sum);
    }
}

volatile uint8_t tapRunning = 0;
volatile uint16_t tapMissing = 0;

int pickTrack(int firstTrack, bool sampleInstrument, const TrackFacts* tracks, int count)
{
    if (count <= 0)
    {
        return kNoTrack;
    }
    const int first = ((firstTrack % count) + count) % count;
    for (int i = 0; i < count; i++)
    {
        const int t = (first + i) % count;
        if (sampleInstrument && !tracks[t].sampleTrack)
        {
            continue;
        }
        if (!tracks[t].muted && !tracks[t].noteOpen)
        {
            return t;
        }
    }
    for (int i = 0; i < count; i++)
    {
        const int t = (first + i) % count;
        if (tracks[t].sampleTrack && !tracks[t].muted && tracks[t].ended)
        {
            return t;
        }
    }
    return kNoTrack;
}

bool soundEnded(const SoundFacts& s)
{
    // instrumentPlayMode: playModeSingleShot 0, playModeSlice 4, playModeBeatSlice 5
    const bool playsToItsEnd = s.playMode == 0 || s.playMode == 4 || s.playMode == 5;
    return s.sampleInstrument && playsToItsEnd && !s.repeats && !s.delayed && !s.liveNote &&
           !s.voicePlaying;
}

volatile uint8_t liveOn = 0;

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
        if (!view.countingIn)
        {
            visit.arm = Arm::none; // a count-in given up, or a Rec that started nothing
        }
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

    // Trk Audio turned Off, or a source without it chosen: a sample or synth picked here gives way
    // to the recorder's MIDI instrument, as CE's entry would have chosen it.
    const bool live = settings.trkAudio && view.buttonShown && visit.entered;
    if (liveOn != 0 && !live && !isMidiInstrument(mtProject.values.lastUsedInstrument))
    {
        mtProject.values.lastUsedInstrument = view.midiInstrument;
    }
    liveOn = live ? 1 : 0;
}

bool playPressed(bool shift, bool countingIn)
{
    if (!visitOn ||
        !playArms(gatesAreOpen, countingIn, sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP))
    {
        return false;
    }
    visit.arm = shift ? Arm::song : Arm::pattern;
    return true;
}

void recPressed(bool shift, bool onSetup)
{
    if (visitOn)
    {
        visit.arm = armOnRec(visit.arm, shift, gatesAreOpen, onSetup);
    }
}

// Right after recorder.record(), so the pattern's first step and the take's first block start
// together. A pattern already playing starts again, as Stop then Play would.
void takeStarted()
{
    if (!visitOn)
    {
        return;
    }
    const Arm arm = visit.arm;
    visit.arm = Arm::none;
    if (arm == Arm::none || !visit.takeMoved || !gatesAreOpen)
    {
        return;
    }
    if (sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP)
    {
        sequencer.stop();
    }
    if (arm == Arm::song)
    {
        sequencer.playSong();
    }
    else
    {
        sequencer.playPattern();
    }
}

namespace
{
// The tap: static storage, built in place at the first Trk Record take, never destroyed.
constexpr int kTapChannels = 2;
alignas(AudioRecordQueue) uint8_t tapQueueStore[kTapChannels][sizeof(AudioRecordQueue)];
alignas(AudioConnection) uint8_t tapLinkStore[kTapChannels][sizeof(AudioConnection)];
bool tapBuilt = false;
bool tapPending = false; // hook 6 placed a take that records the tracker
bool tapMonitor = false; // Monitor On for it: the limiters already hold the Line In
bool tapMasked = false;  // hook 11 masked the audio interrupt
volatile uint8_t tapDrop = 0;  // Monitor off: the take's first Line In block is still to drop
volatile uint8_t tapAlone = 0; // Monitor on: the take is the tap alone

AudioRecordQueue* tapQueue(int channel)
{
    return reinterpret_cast<AudioRecordQueue*>(tapQueueStore[channel]);
}

// A queue joins the end of the update list, after the limiters and the final mixers, so it gets
// each limiter block in the interrupt that made it (11.1). AudioStream's constructor walks that list,
// so the audio interrupt is masked for it; AudioConnection masks its own list walk. A new
// connection marks the queue active, and its first update while not recording puts it to sleep.
void buildTap()
{
    if (tapBuilt)
    {
        return;
    }
    AudioNoInterrupts();
    for (int channel = 0; channel < kTapChannels; channel++)
    {
        new (tapQueueStore[channel]) AudioRecordQueue();
    }
    AudioInterrupts();
    AudioOutput* output = engine.getAudioOutput();
    for (int channel = 0; channel < kTapChannels; channel++)
    {
        new (tapLinkStore[channel])
          AudioConnection(output->getLimiter(static_cast<AudioOutput::OutputChannels>(channel)),
                          0,
                          tapQueue(channel),
                          0);
    }
    tapBuilt = true;
}
} // namespace

// address and size come in as CE's buffer, the whole sample memory.
void placeBuffer(int16_t** address, uint32_t* size, int channels, bool recordShown, bool monitorOn)
{
    if (!visitOn)
    {
        return;
    }
    visit.takeMoved = false;
    tapPending = false;
    tapMonitor = monitorOn;
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
            tapPending = tapWanted(settings, recordShown, channels, true);
            return;
        }
        closeGates(); // too little room: the take is CE's, over the samples, and silent
    }
    visit.samplesOverwritten = true;
}

static_assert(playModeSingleShot == 0 && playModeSlice == 4 && playModeBeatSlice == 5,
              "soundEnded's play modes");

bool keepsInstrument(bool buttonShown)
{
    const bool live = visitOn && settings.trkAudio && buttonShown && visit.entered;
    liveOn = live ? 1 : 0;
    return live;
}

bool fullPicker()
{
    return visitOn && liveOn != 0;
}

// Reads what the sequencer and the voices already hold in RAM: no display, card or heap, so the
// MIDI path in the audio interrupt may call it.
int liveTrack(int firstTrack, int instrument)
{
    if (liveOn == 0)
    {
        return kCeRule;
    }
    const Sequencer::strPlayer* player = sequencer.ptrPlayer;
    TrackFacts tracks[kAllTracks];
    for (int t = 0; t < kAllTracks; t++)
    {
        const auto& track = player->track[t];
        TrackFacts& f = tracks[t];
        f.muted = sequencer.trackMutedForMods(static_cast<uint8_t>(t)) != 0;
        f.noteOpen = track.noteOpen;
        f.sampleTrack = isSampleTrack(t);
        if (!f.sampleTrack)
        {
            continue;
        }
        SoundFacts s;
        const int last = track.stepSent.instrument;
        s.sampleInstrument = isSampleInstrument(last) && last < kSampleInstrumentsCount;
        s.playMode = s.sampleInstrument ? mtProject.instrument[last].playMode : 0;
        for (const auto& fx : track.stepSent.fx)
        {
            if (fx.type == Sequencer::strFxConsts::FX_TYPE_ROLL ||
                fx.type == Sequencer::strFxConsts::FX_TYPE_MIDI_CHORD ||
                fx.type == Sequencer::strFxConsts::FX_TYPE_ARP)
            {
                s.repeats = true;
            }
        }
        s.repeats = s.repeats || track.roll.isOn != 0 || track.arp.isOn != 0;
        s.delayed = track.micromove.isActive != 0;
        s.liveNote = track.recOpen != 0;
        AudioVoice* voice = instrumentPlayer[t].getAudioVoice();
        s.voicePlaying = voice->getMemoryPlayer(AudioVoice::kLeft)->isPlaying() ||
                         voice->getMemoryPlayer(AudioVoice::kRight)->isPlaying();
        f.ended = soundEnded(s);
    }
    return pickTrack(firstTrack, isSampleInstrument(instrument), tracks, kAllTracks);
}

bool playOnlyOn(int16_t source)
{
    if (liveOn == 0)
    {
        return false;
    }
    if (source >= 0 && source < kSources)
    {
        playedOnly[source] = 1;
    }
    return true;
}

bool playOnlyOff(int16_t source)
{
    if (source >= 0 && source < kSources)
    {
        if (playedOnly[source] == 0)
        {
            return false;
        }
        playedOnly[source] = 0;
        return true;
    }
    return liveOn != 0; // a source with no byte: as Trk Audio is now
}

// CE begins its Line In queues right after this, and queuesStarted unmasks, so all four queues
// start in the same audio interrupt. The mask is the NVIC's (AudioNoInterrupts): AudioStream::release,
// which begin() can reach through clear(), turns PRIMASK off again.
void queuesStarting()
{
    tapMasked = false;
    const bool want = visitOn && tapPending && gatesAreOpen && visit.takeMoved;
    tapPending = false;
    if (!want)
    {
        return;
    }
    buildTap();
    AudioNoInterrupts();
    tapMasked = true;
    for (int channel = 0; channel < kTapChannels; channel++)
    {
        tapQueue(channel)->begin();
    }
    tapMissing = 0;
    tapAlone = tapMonitor ? 1 : 0;
    tapDrop = tapMonitor ? 0 : 1;
    tapRunning = 1;
}

void queuesStarted()
{
    if (tapMasked)
    {
        tapMasked = false;
        AudioInterrupts();
    }
}

// CE's Line In queues update before their source and get each input block one interrupt late; the
// tap gets the limiters' block in the interrupt that made it (11.1). So with Monitor off the first
// Line In block, from before the take, goes, and from then on each pair was played together
// (decision 10). With Monitor on the Line In is in the tap already and the input isn't used.
bool dropBlock()
{
    if (tapDrop == 0)
    {
        return false;
    }
    tapDrop = 0;
    return true;
}

void copyBlock(int channel, int16_t* dest, const int16_t* lineIn)
{
    constexpr uint32_t kBytes = sizeof(int16_t) * AUDIO_BLOCK_SAMPLES;
    if (tapRunning == 0)
    {
        memcpy(dest, lineIn, kBytes);
        return;
    }
    AudioRecordQueue* queue = tapQueue(channel);
    const int16_t* tracker = queue->readBuffer();
    if (tracker == nullptr)
    {
        tapMissing = static_cast<uint16_t>(tapMissing + 1);
        if (lineIn != nullptr)
        {
            memcpy(dest, lineIn, kBytes);
        }
        else
        {
            memset(dest, 0, kBytes);
        }
        return;
    }
    if (tapAlone != 0 || lineIn == nullptr)
    {
        memcpy(dest, tracker, kBytes);
    }
    else
    {
        addClipped(dest, lineIn, tracker, AUDIO_BLOCK_SAMPLES);
    }
    queue->freeBuffer();
}

// After CE's isActive = false, so no copy of the interrupt reads the tap after this. From the full-
// memory end in the interrupt, or from Recorder::stop in the main loop, which ends and clears CE's
// queues the same way.
void takeFinished()
{
    if (tapRunning == 0)
    {
        return;
    }
    tapRunning = 0;
    tapDrop = 0;
    for (int channel = 0; channel < kTapChannels; channel++)
    {
        tapQueue(channel)->end();
        tapQueue(channel)->clear();
    }
}

#endif // MT_HOST_TEST

} // namespace resample
} // namespace mods

#endif // MT_HAS_RESAMPLE
