// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// See lockupRecorder.h. mtLockupTick and the fault handlers run where nothing else can be trusted:
// no SD, heap, or anything that waits. Only update(), in the main loop, touches the card.

#include "lockupRecorder.h"

#if MT_LOCKUP_RECORDER

#include "Platform.h"
#include "Profiler.h"
#include "TrackerImpl.h"
#include "fileManager.h"
#include "masterfx.h"
#include "masterfxDsp.h"
#include "mods.h"
#include "mtSequencer.h"
#include "modsSwitches.h"

#include <cr_section_macros.h>

#include <cstdio>
#include <cstring>
#include <malloc.h>

extern "C"
{
    void _vStackTop(void);

    // platforms/imx/source/TimeControl.cpp
    uint32_t mtLockupCycles();
    uint32_t mtLockupAudioActive();
    uint32_t mtLockupAudioPending();
    uint32_t mtLockupSavedRead(uint32_t index);
    void mtLockupSavedWrite(uint32_t index, uint32_t value);
    void mtLockupFaultRegisters(uint32_t* out);
    [[noreturn]] void mtLockupCleanAndReset();
}

namespace
{

constexpr char kLogPath[] = "/mfx_log.txt";
constexpr uint32_t kMagic = 0x4D464C4B;
constexpr uint32_t kStallWhilePlayingMs = 3000;
constexpr uint32_t kStallWhileStoppedMs = 20000; // no main-loop job of CE's takes this long in one go
constexpr uint32_t kBlockUs = 2902;              // 128 samples at 44,100 Hz
constexpr uint32_t kWindowTicks = 1024;
constexpr uint32_t kPcSamples = 8;
constexpr uint32_t kStackWords = 16;

enum Kind : uint32_t
{
    kStall = 1,
    kHardFault = 2,
    kMemManage = 3,
    kBusFault = 4,
    kUsageFault = 5,
};

constexpr const char* kKindNames[] = { "?",         "main loop stalled", "hard fault",
                                       "MPU fault", "bus fault",         "usage fault" };

// SNVS's general-purpose registers survive a reset whatever the boot does to RAM, so the essentials
// go there too.
enum Saved : uint32_t
{
    kSavedMagic,
    kSavedKind,
    kSavedPc,
    kSavedLr,
    kSavedCfsr,
    kSavedAddress,
    kSavedLoad,
    kSavedCount,
};

struct Record
{
    uint32_t magic;
    uint32_t kind;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t bfar;
    uint32_t mmfar;
    uint32_t sp;
    uint32_t audioActive;
    uint32_t audioPending;
    uint32_t audioTicksInWindow; // of kWindowTicks: the share of time inside the audio interrupt
    uint32_t stalledMs;
    uint32_t playing;
    uint32_t blocks;
    uint32_t peakUs;
    uint32_t lastUs;
    uint32_t overBlocks;
    uint32_t reverb;
    uint32_t delay;
    uint32_t pcSamples[kPcSamples];
    uint32_t stack[kStackWords];
    uint32_t sum;
};

__NOINIT(EXTERNAL_RAM) Record record;

volatile uint32_t beats = 0;
uint32_t lastBeat = 0;
uint32_t stalled = 0;
uint32_t longestStall = 0;
uint32_t windowTicks = 0;
uint32_t windowAudio = 0;
uint32_t lastWindowAudio = 0;
uint32_t pcSamples[kPcSamples] = {};
bool armed = false;
bool saving = false;

uint32_t cyclesPerUs = 0;
uint32_t audioStartCycles = 0;
uint32_t lastUs = 0;
uint32_t peakUs = 0;
uint32_t sumUs = 0;
uint32_t blocks = 0;
uint32_t overBlocks = 0;
uint32_t backToBack = 0;

uint32_t sumOf(const Record& r)
{
    uint32_t sum = 0;
    const uint32_t* word = reinterpret_cast<const uint32_t*>(&r);
    for (uint32_t i = 0; i < offsetof(Record, sum) / sizeof(uint32_t); i++)
    {
        sum = sum * 31 + word[i];
    }
    return sum;
}

bool isCode(uint32_t value)
{
    return (value & 1) != 0 && ((value >= 0x100 && value < 0x20000) || (value >= 0x60000000 && value < 0x61000000));
}

[[noreturn]] void saveAndRestart(uint32_t kind, const uint32_t* frame)
{
    if (saving)
    {
        mtLockupCleanAndReset(); // a fault in here: what's saved so far will have to do
    }
    saving = true;

    const uint32_t stackTop = reinterpret_cast<uint32_t>(&_vStackTop);
    const uint32_t at = reinterpret_cast<uint32_t>(frame);
    const bool frameIsGood = (at & 3) == 0 && at >= 0x20000000 && at + 32 <= stackTop;

    Record& r = record;
    r.magic = kMagic;
    r.kind = kind;
    r.pc = frameIsGood ? frame[6] : 0;
    r.lr = frameIsGood ? frame[5] : 0;
    r.psr = frameIsGood ? frame[7] : 0;
    uint32_t faultRegisters[4];
    mtLockupFaultRegisters(faultRegisters);
    r.cfsr = faultRegisters[0];
    r.hfsr = faultRegisters[1];
    r.bfar = faultRegisters[2];
    r.mmfar = faultRegisters[3];
    r.sp = at;
    r.audioActive = mtLockupAudioActive();
    r.audioPending = mtLockupAudioPending();
    r.audioTicksInWindow = lastWindowAudio;
    r.stalledMs = stalled;
    r.playing = sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP;
    r.blocks = blocks;
    r.peakUs = peakUs;
    r.lastUs = lastUs;
    r.overBlocks = overBlocks;
    r.reverb = mods::masterfx::activeReverb;
    r.delay = mods::masterfx::activeDelay;
    for (uint32_t i = 0; i < kPcSamples; i++)
    {
        r.pcSamples[i] = pcSamples[i];
    }
    uint32_t found = 0;
    if (frameIsGood)
    {
        for (const uint32_t* word = frame + 8; reinterpret_cast<uint32_t>(word) < stackTop && found < kStackWords; word++)
        {
            if (isCode(*word))
            {
                r.stack[found++] = *word;
            }
        }
    }
    while (found < kStackWords)
    {
        r.stack[found++] = 0;
    }
    r.sum = sumOf(r);

    mtLockupSavedWrite(kSavedKind, kind);
    mtLockupSavedWrite(kSavedPc, r.pc);
    mtLockupSavedWrite(kSavedLr, r.lr);
    mtLockupSavedWrite(kSavedCfsr, r.cfsr);
    mtLockupSavedWrite(kSavedAddress, r.bfar);
    mtLockupSavedWrite(kSavedLoad, lastWindowAudio | (peakUs << 16));
    mtLockupSavedWrite(kSavedMagic, kMagic);

    mtLockupCleanAndReset();
}

char text[1400];
bool textWaiting = false;
bool lookedForRecord = false;
Sequencer::enSeqState lastState = Sequencer::SEQ_STATE_STOP;
uint32_t playStartedMs = 0;

const char* reverbName(uint32_t index)
{
    return index < mods::masterfx::kReverbCount ? mods::masterfx::kReverbNames[index] : "?";
}

const char* delayName(uint32_t index)
{
    return index < mods::masterfx::kDelayCount ? mods::masterfx::kDelayNames[index] : "?";
}

void describeRecord()
{
    const Record& r = record;
    const bool ramGood = r.magic == kMagic && r.sum == sumOf(r);
    const bool snvsGood = mtLockupSavedRead(kSavedMagic) == kMagic;
    if (!ramGood && !snvsGood)
    {
        return;
    }

    int used = 0;
    if (ramGood)
    {
        used = snprintf(text,
                        sizeof(text),
                        "LOCKUP: %s, %s. Main loop stopped for %lu ms. Inside the audio interrupt then: %s (next one "
                        "waiting: %s), %lu of the last %lu ms. Master FX %s + %s. %lu blocks, slowest %lu us, last %lu "
                        "us, %lu over the %lu us a block has.\n  pc %08lx lr %08lx psr %08lx sp %08lx cfsr %08lx hfsr "
                        "%08lx bfar %08lx mmfar %08lx\n  pc samples",
                        r.kind < 6 ? kKindNames[r.kind] : "?",
                        r.playing ? "playing" : "stopped",
                        static_cast<unsigned long>(r.stalledMs),
                        r.audioActive ? "yes" : "no",
                        r.audioPending ? "yes" : "no",
                        static_cast<unsigned long>(r.audioTicksInWindow),
                        static_cast<unsigned long>(kWindowTicks),
                        reverbName(r.reverb),
                        delayName(r.delay),
                        static_cast<unsigned long>(r.blocks),
                        static_cast<unsigned long>(r.peakUs),
                        static_cast<unsigned long>(r.lastUs),
                        static_cast<unsigned long>(r.overBlocks),
                        static_cast<unsigned long>(kBlockUs),
                        static_cast<unsigned long>(r.pc),
                        static_cast<unsigned long>(r.lr),
                        static_cast<unsigned long>(r.psr),
                        static_cast<unsigned long>(r.sp),
                        static_cast<unsigned long>(r.cfsr),
                        static_cast<unsigned long>(r.hfsr),
                        static_cast<unsigned long>(r.bfar),
                        static_cast<unsigned long>(r.mmfar));
        for (uint32_t i = 0; i < kPcSamples && used > 0 && static_cast<size_t>(used) < sizeof(text) - 12; i++)
        {
            used += snprintf(text + used, sizeof(text) - used, " %08lx", static_cast<unsigned long>(r.pcSamples[i]));
        }
        used += snprintf(text + used, sizeof(text) - used, "\n  stack");
        for (uint32_t i = 0; i < kStackWords && static_cast<size_t>(used) < sizeof(text) - 12; i++)
        {
            used += snprintf(text + used, sizeof(text) - used, " %08lx", static_cast<unsigned long>(r.stack[i]));
        }
        used += snprintf(text + used, sizeof(text) - used, "\n");
    }
    else
    {
        const uint32_t kind = mtLockupSavedRead(kSavedKind);
        used = snprintf(text,
                        sizeof(text),
                        "LOCKUP (the short copy): %s. pc %08lx lr %08lx cfsr %08lx bfar %08lx load %08lx\n",
                        kind < 6 ? kKindNames[kind] : "?",
                        static_cast<unsigned long>(mtLockupSavedRead(kSavedPc)),
                        static_cast<unsigned long>(mtLockupSavedRead(kSavedLr)),
                        static_cast<unsigned long>(mtLockupSavedRead(kSavedCfsr)),
                        static_cast<unsigned long>(mtLockupSavedRead(kSavedAddress)),
                        static_cast<unsigned long>(mtLockupSavedRead(kSavedLoad)));
    }

    record.magic = 0;
    mtLockupSavedWrite(kSavedMagic, 0);
    textWaiting = used > 0;
}

void describePlay(uint32_t nowMs)
{
    Synth& synth = TrackerImpl::getInstance()->getSynth();
    struct mallinfo info = mallinfo();
    const uint32_t count = blocks > 0 ? blocks : 1;
    const int used = snprintf(text,
                              sizeof(text),
                              "play %lu s: Master FX %s, %s + %s, voices %d %d %d. Audio interrupt: average %lu%%, "
                              "slowest %lu%%, %lu of %lu blocks over time, %lu back to back. Longest main loop gap %lu "
                              "ms. Pool peak %d of %d. Free heap %d, max stack %d.\n",
                              static_cast<unsigned long>((nowMs - playStartedMs) / 1000),
                              mods::on(mods::Mod::masterfx) ? "On" : "Off",
                              reverbName(mods::masterfx::activeReverb),
                              delayName(mods::masterfx::activeDelay),
                              synth.getMaxVoiceCount(0),
                              synth.getMaxVoiceCount(1),
                              synth.getMaxVoiceCount(2),
                              static_cast<unsigned long>((sumUs / count) * 100 / kBlockUs),
                              static_cast<unsigned long>(peakUs * 100 / kBlockUs),
                              static_cast<unsigned long>(overBlocks),
                              static_cast<unsigned long>(blocks),
                              static_cast<unsigned long>(backToBack),
                              static_cast<unsigned long>(longestStall),
                              static_cast<int>(AudioMemoryUsageMax()),
                              80,
                              static_cast<int>(info.fordblks),
                              static_cast<int>(Profiler::getInstance()->maxStackBytes()));
    textWaiting = used > 0;
}

void startPlayStats()
{
    AudioMemoryUsageMaxReset();
    peakUs = 0;
    sumUs = 0;
    blocks = 0;
    overBlocks = 0;
    backToBack = 0;
    longestStall = 0;
}

void writeText(FileSystem* fileSystem)
{
    auto [isOpen, file] = fileSystem->openFile(kLogPath,
                                               fileSystem->exists(kLogPath) ? FileSystem::FileOpenMode::kWriteAppend
                                                                            : FileSystem::FileOpenMode::kWriteCreateNew);
    if (isOpen)
    {
        fileSystem->writeFile(text, strlen(text), file);
        fileSystem->closeFile(file);
    }
    textWaiting = false;
}

} // namespace

extern "C" void mtLockupBeat()
{
    beats = beats + 1;
}

extern "C" void mtLockupTick(uint32_t* frame)
{
    if (mtLockupAudioActive())
    {
        windowAudio++;
    }
    if (++windowTicks == kWindowTicks)
    {
        lastWindowAudio = windowAudio;
        windowAudio = 0;
        windowTicks = 0;
    }

    const uint32_t beat = beats;
    if (beat != lastBeat)
    {
        lastBeat = beat;
        stalled = 0;
        return;
    }
    stalled++;
    if (stalled > longestStall)
    {
        longestStall = stalled;
    }
    if (!armed)
    {
        return;
    }
    if ((stalled & 255) == 0)
    {
        pcSamples[(stalled >> 8) % kPcSamples] = frame[6];
    }
    if (stalled >= kStallWhilePlayingMs
        && (stalled >= kStallWhileStoppedMs || sequencer.getSeqState() != Sequencer::SEQ_STATE_STOP))
    {
        saveAndRestart(kStall, frame);
    }
}

extern "C" void mtLockupFault(uint32_t kind, uint32_t* frame)
{
    saveAndRestart(kind, frame);
}

namespace mods
{
namespace lockup
{

void audioStart()
{
    audioStartCycles = mtLockupCycles();
}

void audioStop()
{
    if (cyclesPerUs == 0)
    {
        return;
    }
    const uint32_t us = (mtLockupCycles() - audioStartCycles) / cyclesPerUs;
    lastUs = us;
    sumUs += us;
    blocks++;
    if (us > peakUs)
    {
        peakUs = us;
    }
    if (us > kBlockUs)
    {
        overBlocks++;
    }
    if (mtLockupAudioPending())
    {
        backToBack++; // the next block is already due, so the main loop gets nothing in between
    }
}

void arm()
{
    cyclesPerUs = TrackerImpl::getInstance()->getPlatform()->getTimer()->clockCyclesPerUs();
    armed = true;
}

void update()
{
    if (!armed)
    {
        return;
    }

    if (!lookedForRecord)
    {
        lookedForRecord = true;
        describeRecord();
    }

    const Sequencer::enSeqState state = sequencer.getSeqState();
    const uint32_t nowMs = TrackerImpl::getInstance()->getPlatform()->getTimer()->millis();
    if (state != lastState)
    {
        if (lastState == Sequencer::SEQ_STATE_STOP)
        {
            startPlayStats();
            playStartedMs = nowMs;
        }
        else if (state == Sequencer::SEQ_STATE_STOP && !textWaiting)
        {
            describePlay(nowMs);
        }
        lastState = state;
    }

    if (textWaiting && newFileManager.getStatus() == fmIdle && newFileManager.getCurrentOperation() == fmNoOperation)
    {
        FileSystem* fileSystem = TrackerImpl::getInstance()->getPlatform()->getFileSystem();
        if (fileSystem->isSdCardInserted())
        {
            writeText(fileSystem);
        }
    }
}

} // namespace lockup
} // namespace mods

#endif // MT_LOCKUP_RECORDER
