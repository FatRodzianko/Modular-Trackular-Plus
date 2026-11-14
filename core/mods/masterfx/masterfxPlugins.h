// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// The Master FX plugins (.ptmfx, ptmfx.h): the files in /MasterFX/ on the card, each made for
// one of the three master slots (reverb, delay, dynamics), loaded into that slot's own memory
// and run in the audio interrupt in place of CE's effect.
//
// The catalog: at start, the default pack (masterfxPack.inc) is written to /MasterFX/ for each
// of its files that is missing, then every .ptmfx there is listed and its header checked.
//
// One plugin a slot. Each slot owns fixed memory, given by the platform in Env: a code area
// (ITCM on the unit) the plugin's code is copied into, its state (DTCM), and the slot's buffers
// (PtmfxMemory: CE's reverb and delay buffers, the dynamics slot's own). Loading is two steps,
// both in the main loop:
//   - read(): the file is read into the load buffer and checked. What plays keeps playing.
//   - install(): with the slot stopped (the caller blocks CE's object first), the code goes
//     into the slot's area and its cache is synced, the state is zeroed and init runs. The
//     slot then runs, with a reset before its first block; the caller clears the memory
//     (CE's own clear) and calls cleared().
//
// The interrupt side (process) only reads what the main loop publishes: a slot's `active` byte
// is set last when it is installed and cleared first when it goes, and on this single core the
// main loop never runs while the interrupt does. Own knobs: the main loop stores the value,
// then sets the knob's dirty byte; the interrupt clears the byte, then reads the value, then
// calls set_param.
//
// Guards, because a plugin is someone else's code in the interrupt:
//   - Time: process() over Env::budgetCycles for kOverBlocks blocks in a row, and the slot is
//     bypassed (takeBypassed() tells the page). A bypassed reverb or delay returns silence; a
//     bypassed dynamics slot passes the master through.
//   - Output (float plugins): NaN or infinite becomes 0 and the plugin is reset before its
//     next block; anything else is held within +-kOutputLimit. 16-bit output can't be either.
//   - Start: a marker file is on the card from the moment a plugin is first installed until it
//     has run kSettleMs. Found at start (the unit stopped with a new plugin running), plugins a
//     project asks for are not loaded until the user picks one (safeMode()).
// A plugin that crashes or never returns can't be guarded against; the start marker is for
// that.
//
// Everything but process() and cleared() is main loop only. No heap.

#ifndef MODS_MASTERFX_MASTERFXPLUGINS_H_
#define MODS_MASTERFX_MASTERFXPLUGINS_H_

#include "modsFileIo.h"
#include "ptmfx.h"

#include <cstddef>
#include <cstdint>

namespace mods
{
namespace masterfx
{
namespace plugins
{

constexpr char kDir[] = "/MasterFX";
constexpr char kSuffix[] = ".ptmfx";
constexpr char kMarker[] = "/MasterFX/.starting";

enum Slot : uint8_t
{
    kReverb = 0,
    kDelay = 1,
    kDynamics = 2,
};
constexpr size_t kSlots = 3;

constexpr size_t kCatalogSize = 64;       // files listed
constexpr size_t kFileNameSize = 48;      // a file's name with its 0 (longer ones aren't listed)

constexpr uint32_t kCodeArea = PTMFX_MAX_CODE;
constexpr uint32_t kStateSize[kSlots] = { PTMFX_STATE_REVERB, PTMFX_STATE_DELAY, PTMFX_STATE_DYNAMICS };
constexpr uint32_t kMaxFile = sizeof(PtmfxHeader) + kCodeArea;
constexpr uint32_t kMaxKnobSteps = 100000u;
constexpr uint32_t kLabels[kSlots] = { 4, 6, 3 };  // CE's knobs on each slot's page

constexpr uint32_t kOverBlocks = 8;
constexpr float kOutputLimit = 4.0f;
constexpr uint32_t kSettleMs = 5000;
constexpr uint32_t kScratchFloats = 4 * PTMFX_BLOCK; // float plugins' blocks: L, R and two inputs

// The PTMFX_SLOT_* a plugin for this slot has
constexpr uint8_t slotType(Slot s)
{
    return static_cast<uint8_t>(s + 1);
}

enum class Status : uint8_t
{
    ok,
    missing,      // no such file now
    badFile,      // its header or size is wrong (why: checkHeader)
    tooBig,       // more code or state than the slot has
    wrongSlot,    // made for another slot
    skipped,      // safe mode: the last start stopped with a new plugin running
    readFailed,
    notRead,      // install() without a read() for that slot
};

// What the platform gives: each slot's memory, a load buffer, the float scratch, the cache sync
// for new code, a cycle counter, the time, and the plugins' host table.
struct Env
{
    uint8_t* code[kSlots];        // kCodeArea bytes each, 32-byte aligned, executable
    uint8_t* state[kSlots];       // kStateSize[slot] bytes each, 8-byte aligned
    PtmfxMemory memory[kSlots];
    uint8_t* loadBuffer;          // kMaxFile bytes, 4-byte aligned
    float* scratch;               // kScratchFloats, 8-byte aligned
    void (*syncCode)(void* addr, uint32_t size);
    uint32_t (*cycles)();
    uint32_t (*millis)();
    uint32_t budgetCycles;        // an eighth of a block's time
    const PtmfxHost* host;
};

struct CatalogEntry
{
    char file[kFileNameSize];     // in kDir
    char name[32];                // the header's, shown in the list
    uint32_t size;                // the file's
    uint16_t flags;
    uint8_t slot;                 // PTMFX_SLOT_*, 0 if unreadable
    uint8_t knobs;
    Status status;                // ok, or why it can't be used
};

// Is this a plugin the host can run in a slot: its header and the file's size. why: a short
// reason.
bool checkHeader(const PtmfxHeader& header, uint32_t fileSize, const char** why);

// The start: the marker, the pack, the list. Again later: only the list.
void start(FileIo& io, const Env& env);
bool started();
void scan();
size_t catalogCount();
const CatalogEntry* catalogAt(size_t index);
int catalogFind(const char* file);           // -1 if not listed
// The usable plugins for one slot, in catalog order
size_t slotCount(Slot slot);
const CatalogEntry* slotAt(Slot slot, size_t index);
bool safeMode();

// The main loop, now and then: removes the start marker once plugins have run kSettleMs.
void update();

// The plugin in file (catalog name) read into the load buffer and checked for slot.
// fromProject: a project's setting asked for it (safe mode skips those).
Status read(Slot slot, const char* file, bool fromProject);
// The plugin read last for slot into it, with the slot stopped: see the top. Its own knobs at
// their defaults.
Status install(Slot slot);
// The slot plays nothing of ours.
void remove(Slot slot);
bool installed(Slot slot);
const PtmfxHeader* header(Slot slot);
const char* file(Slot slot);                 // "" when nothing is installed

// The slot's memory was cleared (CE's clear): the plugin is reset before its next block. Main
// loop or interrupt.
void cleared(Slot slot);

// An own knob: its value is kept on the plugin's steps (nearest; out of range is held at the
// ends).
uint32_t knobs(Slot slot);
void setKnob(Slot slot, uint32_t knob, float value);
float knob(Slot slot, uint32_t knob);
void stepKnob(Slot slot, uint32_t knob, int steps);
uint32_t knobSteps(Slot slot, uint32_t knob);
uint32_t knobStep(Slot slot, uint32_t knob);
void setKnobStep(Slot slot, uint32_t knob, uint32_t step);
// Its label ("" if there is no such knob), and back to its default
const char* knobLabel(Slot slot, uint32_t knob);
void resetKnob(Slot slot, uint32_t knob);
// Its text: the plugin's format() if it has one and fills it in, else the value (one decimal
// for 'f') and the unit. At: the text for one of its steps.
void knobText(Slot slot, uint32_t knob, char* out, uint32_t size);
void knobTextAt(Slot slot, uint32_t knob, uint32_t step, char* out, uint32_t size);
// A CE knob's label for the slot's page: the plugin's, or nullptr for CE's.
const char* label(Slot slot, uint32_t index);

uint32_t now();

// A slot bypassed by the time guard since the last call, or -1.
int takeBypassed();
bool bypassed(Slot slot);

// Audio interrupt. One block through the slot's plugin. block has the 16-bit pointers and the
// knobs; the host fills the float ones for a float plugin. gain: the plugin's meter value.
enum class Ran : uint8_t
{
    none,         // nothing installed: CE's effect plays
    ran,
    bypassed,     // stopped by the time guard: the hook plays silence (reverb, delay) or the dry master
};
Ran process(Slot slot, PtmfxBlock& block, float* gain);

// The firmware's copy of the default pack (masterfxPack.cpp)
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
} // namespace masterfx
} // namespace mods

#endif // MODS_MASTERFX_MASTERFXPLUGINS_H_
