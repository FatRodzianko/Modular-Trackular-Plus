// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The Synth FX plugins (.ptsfx, ptsfx.h): the files in /SynthFX/ on the card, loaded into the
// mod's own memory and run in the audio interrupt on a synth part's audio.
//
// The catalog: at start, the default pack (synthfxPack.inc) is written to /SynthFX/ for each
// of its files that is missing, then every .ptsfx there is listed and its header checked. A
// plugin's code is loaded when a slot first picks it, into the code pool, and stays while any
// instance uses it; each instance has its state in the instance pool. Neither pool moves a
// block once given (the interrupt may be running it): first fit, holes reused.
//
// The interrupt side (process, reset) only reads what the main loop publishes: an instance's
// `active` byte is set last when it is made and cleared first when it goes, and on this single
// core the main loop never runs while the interrupt does, so a cleared instance is never
// running. Knobs: the main loop stores the value, then sets the knob's dirty byte; the
// interrupt clears the byte, then reads the value, then calls set_param.
//
// Guards, because a plugin is someone else's code in the interrupt:
//   - Time: process() over the budget (Env::budgetCycles) for kOverBlocks blocks in a row,
//     and the instance is bypassed (takeBypassed() tells the page).
//   - Output: NaN or infinite becomes 0 and the plugin is reset before its next block;
//     anything else is held within +-kOutputLimit.
//   - Start: a marker file is on the card from the moment a plugin is first made until it has
//     run kSettleMs. Found at start (the unit stopped with a new plugin running), plugins a
//     project asks for are not loaded until the user picks one (safeMode()).
// A plugin that crashes or never returns can't be guarded against; the start marker is for
// that.
//
// Everything but process() and reset() is main loop only. No heap.

#ifndef MODS_SYNTHFX_SYNTHFXPLUGINS_H_
#define MODS_SYNTHFX_SYNTHFXPLUGINS_H_

#include "modsFileIo.h"
#include "ptsfx.h"

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace synthfx
{
namespace plugins
{

constexpr char kDir[] = "/SynthFX";
constexpr char kSuffix[] = ".ptsfx";
constexpr char kMarker[] = "/SynthFX/.starting";

constexpr size_t kCatalogSize = 64;       // files listed
constexpr size_t kFileNameSize = 48;      // a file's name with its 0 (longer ones aren't listed)
constexpr size_t kLoadedSize = 8;         // different plugins loaded at once
constexpr size_t kInstanceCount = 56;     // 3 parts x 2 slots, and WAVE's Pre: 2 slots x 8 voices x 3

constexpr uint32_t kCodePoolSize = 96u * 1024u;
constexpr uint32_t kStatePoolSize = 192u * 1024u;
constexpr uint32_t kMaxCode = 32u * 1024u;          // ptsfx_builder.py's limits
constexpr uint32_t kMaxState = 64u * 1024u;
constexpr uint32_t kMaxKnobSteps = 100000u;

constexpr uint32_t kOverBlocks = 8;
constexpr float kOutputLimit = 4.0f;
constexpr uint32_t kSettleMs = 5000;

enum class Status : uint8_t
{
    ok,
    missing,      // no such file now
    badFile,      // its header or size is wrong (why: checkHeader)
    tooBig,       // more code or state than a plugin may have
    noCodeRoom,   // the code pool has no hole big enough
    noStateRoom,  // the instance pool has no hole big enough
    tooMany,      // kLoadedSize plugins or kInstanceCount instances already
    skipped,      // safe mode: the last start stopped with a new plugin running
    readFailed,
};

// What the platform gives: memory, the cache sync for new code, a cycle counter, the time,
// and the plugins' host table.
struct Env
{
    uint8_t* codePool;            // kCodePoolSize bytes, 32-byte aligned, executable
    uint8_t* statePool;           // kStatePoolSize bytes, 32-byte aligned
    void (*syncCode)(void* addr, uint32_t size);
    uint32_t (*cycles)();
    uint32_t (*millis)();
    uint32_t budgetCycles;        // a quarter of a block's time
    const PtsfxHost* host;
};

struct CatalogEntry
{
    char file[kFileNameSize];     // in kDir
    char name[32];                // the header's, shown in the list
    uint32_t size;                // the file's
    uint16_t flags;
    uint8_t knobs;
    Status status;                // ok, or why it can't be used
};

// Is this a plugin the host can run: its header and the file's size. why: a short reason.
bool checkHeader(const PtsfxHeader& header, uint32_t fileSize, const char** why);

// The start: the marker, the pack, the list. Again later: only the list.
void start(FileIo& io, const Env& env);
bool started();
// Lists kDir again (the loaded plugins stay as they are).
void scan();
size_t catalogCount();
const CatalogEntry* catalogAt(size_t index);
int catalogFind(const char* file);           // -1 if not listed
bool safeMode();

// The main loop, now and then: removes the start marker once plugins have run kSettleMs.
void update();

// An instance of the plugin in file (catalog name): state zeroed, init, every knob at its
// default. fromProject: a project's setting asked for it (safe mode skips those). Returns the
// instance's id, or -1 with *status set.
int create(const char* file, bool fromProject, Status* status);
void destroy(int id);
bool valid(int id);
const PtsfxHeader* header(int id);
size_t instances();                           // made now
size_t loaded();                              // plugins whose code is in

// A knob: its value is kept on the plugin's steps (nearest; out of range is held at the ends).
void setKnob(int id, uint32_t knob, float value);
float knob(int id, uint32_t knob);
// A knob moved by whole steps.
void stepKnob(int id, uint32_t knob, int steps);
// Its text for the screen: the plugin's format() if it has one and fills it in, else the
// value (one decimal for 'f') and the unit.
void knobText(int id, uint32_t knob, char* out, uint32_t size);

// Asks for a reset before the instance's next block (a new note on a Pre slot, a part
// starting again).
void resetSoon(int id);

// An instance bypassed by the time guard since the last call, or -1.
int takeBypassed();
bool bypassed(int id);

// Audio interrupt. One block (PTSFX_BLOCK frames) in place: R null for a mono instance, or
// with inChannels 1 for a widening plugin on a mono part. Returns false if the instance isn't
// running (nothing was done).
bool process(int id, float* L, float* R, uint32_t inChannels);
void reset(int id);

// The firmware's copy of the default pack (synthfxPack.cpp)
struct PackFile
{
    const char* name;
    const uint8_t* data;
    uint32_t size;
};
size_t packCount();
const PackFile& packAt(size_t index);

// Host tests and the emulator rig: forgets everything, as at power-up.
void forget();

} // namespace plugins

// The firmware's start (synthfxPluginsCe.cpp): plugins::start with its memory and host table.
void startPlugins(FileIo& io);
} // namespace synthfx
} // namespace mods

#endif // MODS_SYNTHFX_SYNTHFXPLUGINS_H_
