// Modular-Trackular mods. SPDX-License-Identifier: MIT

#include "copypattern.h"

#if MT_HAS_COPYPATTERN

#include <cstdio>
#include <cstring>

#ifndef MT_HOST_TEST
#include <cr_section_macros.h>

#include "Platform.h"
#include "Tracker.h"
#include "core/interfacePopups.h"
#include "fileManager.h"
#include "fileManagerDefs.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#endif

namespace mods
{
namespace copypattern
{

namespace
{

struct Job
{
    bool active;
    uint8_t source;    // the pattern to copy
    uint8_t candidate; // the next pattern to look at, 0 = none left
    bool busy;         // the file manager was busy at the last update
    bool checked;      // a card check has been made
    uint32_t busySince;
    uint32_t lastCheck;
    char text[kMessageSize]; // "Copied to pattern N": the popup keeps the pointer
};

// Main-loop state only, so SDRAM. Startup doesn't zero it: init() does.
#ifdef MT_HOST_TEST
Job job;
#else
__NOINIT(EXTERNAL_RAM) Job job;
#endif

constexpr char kPatternChanged[] = "Pattern changed - try again";
constexpr char kNoneLeft[] = "No empty pattern left";
constexpr char kBusy[] = "Busy - try again";
constexpr char kWriteFailed[] = "Copy failed - SD card";
constexpr char kPerformance[] = "Not in performance mode";

void finish(Ce& ce, const char* text)
{
    job.active = false;
    ce.message(text);
}

} // namespace

void formatCopied(char* text, size_t size, uint8_t pattern)
{
    snprintf(text, size, "Copied to pattern %u", static_cast<unsigned>(pattern));
}

void init()
{
    memset(&job, 0, sizeof(job));
}

void press(Ce& ce)
{
    if (job.active || !ce.switchedOn())
    {
        return;
    }
    if (ce.performanceChanged())
    {
        ce.message(kPerformance);
        return;
    }
    job.active = true;
    job.source = ce.currentPattern();
    job.candidate = nextCandidate(job.source, job.source);
    job.busy = false;
    job.checked = false;
}

void update(Ce& ce)
{
    if (!job.active)
    {
        return;
    }
    if (!ce.switchedOn())
    {
        cancel();
        return;
    }
    if (ce.currentPattern() != job.source)
    {
        finish(ce, kPatternChanged);
        return;
    }
    if (ce.performanceChanged())
    {
        finish(ce, kPerformance);
        return;
    }

    // What RAM rules out costs nothing.
    while (job.candidate != 0 && ce.usedInRam(job.candidate))
    {
        job.candidate = nextCandidate(job.source, job.candidate);
    }
    if (job.candidate == 0)
    {
        finish(ce, kNoneLeft);
        return;
    }

    const uint32_t now = ce.nowMs();
    if (!ce.fileManagerIdle())
    {
        if (!job.busy)
        {
            job.busy = true;
            job.busySince = now;
        }
        else if (now - job.busySince >= kBusyLimitMs)
        {
            finish(ce, kBusy);
        }
        return;
    }
    job.busy = false;

    if (ce.playing() && job.checked && now - job.lastCheck < kPlayingCheckGapMs)
    {
        return;
    }
    job.checked = true;
    job.lastCheck = now;

    // One card check per update; the one that finds the pattern empty also copies.
    if (ce.fileState(job.candidate) == FileState::used)
    {
        job.candidate = nextCandidate(job.source, job.candidate);
        return;
    }

    switch (ce.copy(job.source, job.candidate))
    {
        case CopyResult::copied:
            formatCopied(job.text, sizeof(job.text), job.candidate);
            finish(ce, job.text);
            break;
        case CopyResult::patternChanged: finish(ce, kPatternChanged); break;
        case CopyResult::writeFailed: finish(ce, kWriteFailed); break;
    }
}

void cancel()
{
    job.active = false;
}

bool running()
{
    return job.active;
}

#ifndef MT_HOST_TEST

namespace
{

using CePattern = Sequencer::strPattern;

static_assert(kAllTracks == kTracks);
static_assert(Sequencer::MAXSTEP + 1 == kSteps);
static_assert(Sequencer::STEP_NOTE_EMPTY == kNoteEmpty);
static_assert(PATTERN_INDEX_MIN == kFirstPattern && PATTERN_INDEX_MAX == kLastPattern);

// The pattern as it was in RAM, written from here, so the interrupt switching patterns
// during the write can't change what's written.
__NOINIT(EXTERNAL_RAM) CePattern snapshot;

class CeSide : public Ce
{
  public:
    uint8_t currentPattern() override { return mtProject.values.actualPattern; }

    bool switchedOn() override { return on(Mod::copypattern); }

    bool performanceChanged() override { return sequencer.isPerformancePatternChanged(); }

    // The temp buffer and the file transfer are also the file manager's.
    bool fileManagerIdle() override { return newFileManager.getStatus() == fmIdle; }

    bool playing() override { return sequencer.isPlay(); }

    uint32_t nowMs() override { return Tracker::getInstance()->getPlatform()->getTimer()->millis(); }

    bool usedInRam(uint8_t pattern) override
    {
        const bool performance = sequencer.isPerformanceMode();
        const InUse use{ mtProject.values.allPatternsBitmask,
                         mtProject.values.allPatternsBitmaskMidi,
                         mtProject.song.playlist,
                         SONG_MAX,
                         sequencer.sequencialSwitch_GetNext(),
                         performance ? mtProject.perfTracksPatterns : nullptr,
                         kMaxAllTracksInTrackers };
        return copypattern::usedInRam(use, pattern);
    }

    FileState fileState(uint8_t pattern) override
    {
        if (newFileManager.isPatternEmpty(pattern)) // CE's name: there's no file
        {
            return FileState::none;
        }
        const CePattern* p = newFileManager.loadWorkspacePatternToTempBuffer(pattern);
        return p != nullptr && patternIsEmpty(*p) ? FileState::empty : FileState::used;
    }

    CopyResult copy(uint8_t source, uint8_t target) override
    {
        // The interrupt switches the RAM patterns and the number together; read both before
        // and after the snapshot.
        const uint8_t numberBefore = mtProject.values.actualPattern;
        const CePattern* patternBefore = sequencer.getPatternToUI();

        sequencer.copyCurrentPatternToBuff(reinterpret_cast<uint8_t*>(&snapshot)); // CE's header and CRC

        if (numberBefore != source || mtProject.values.actualPattern != numberBefore ||
            sequencer.getPatternToUI() != patternBefore)
        {
            return CopyResult::patternChanged;
        }

        // As CE's writePatternFile (private), from the snapshot.
        char path[PATCH_SIZE];
        snprintf(path, sizeof(path), cWorkspacePatternFileFormat, target);
        FileSystem* fileSystem = newFileManager.getFileSystem();
        fileSystem->remove(path);
        bool written = false;
        auto [isOpen, file] = fileSystem->openFile(path, FileSystem::FileOpenMode::kWriteCreateNew);
        if (isOpen)
        {
            written = fileSystem->writeFile(&snapshot, sizeof(snapshot), file) ==
                      static_cast<int32_t>(sizeof(snapshot));
            fileSystem->closeFile(file);
        }
        if (!written)
        {
            fileSystem->remove(path);
            return CopyResult::writeFailed;
        }

        // As CE's saveWorkspacePatternNow after its write. The target's own changed flag stays
        // clear: it means "the RAM pattern is to be saved as this one".
        newFileManager.updatePatternBitmask(target, &snapshot);
        mtProject.values.projectNotSavedFlag |= 1;
        newFileManager.setProjectStructChanged();

#if MT_HAS_TRACKLEN
        tracklenTracksPasted(source, target, 0, 0, kTracks); // only with the mod On
#endif
        return CopyResult::copied;
    }

    void message(const char* text) override { mtPopups.show(0, text); }
};

CeSide ce;

} // namespace

Ce& ceSide()
{
    return ce;
}

#endif // MT_HOST_TEST

} // namespace copypattern
} // namespace mods

#endif // MT_HAS_COPYPATTERN
