#!/usr/bin/env python3
"""A copy of a project made into masterfx's device test: held synth notes, sending to both effects.

    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" [--name "MFX 5 voices"]
        [--voices 1,4,0] [--keep-masterfx] [--effects 4,2] [--worst]
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --ladder
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --engines
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --synths
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --sound
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --synthfx
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --synthfx-plugins
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --masterfx-plugins
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --masterfx-duck
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --masterfx-glue
    python tools/emu/make_masterfx_test_project.py "<project>" "<out folder>" --masterfx-compressor

The project given is only read. In the copy:
- project.mt: the new name, the voices given on the three synth parts, both sends at 60.
- every pattern: one track holding one note for each voice, at volume 30, so nobody needs to turn
  anything down for the test.
- no masterfx.bin, so Master FX starts Off and pressing Play first is Stock. With --keep-masterfx it
  stays, and its effects come in through the importer as any real 1.2.0 project's do.

Everything else stays byte for byte, the synth patches and other mods' files included.

--effects reverb,delay writes masterfx.bin with those numbers (kReverbNames, kDelayNames), so the project
starts with them. --ladder writes the projects of docs/mt/DEVICE_TEST_LADDER.md, in order. --engines
writes that file's second set: all three parts on one engine with its Init patch, so nothing depends
on the source project's sound. The engine comes from project.mt's synthType, not from the patch file.
--synths writes the Synths mod's set the same way, on WAVE and PHASE (a mod build only: plain CE
resets a part whose synthType it doesn't know).

--synthfx writes the Synth FX set (SYNTHFX_LADDER), lightest first: mods.bin has Synth FX and Master FX
On and record 16 holds each part's effect settings, the rest of the source's mods.bin kept.
--synthfx-plugins writes the plugin set (PLUGIN_LADDER) the same way, with record 16 version 3: each
slot's plugin file and knobs (and a Placement byte, 0: Post).
--masterfx-plugins writes the Master FX plugin set (MASTERFX_PLUGIN_SET): mods.bin has Master FX On
and record 4 holds the slots, version 1 (reverb and delay numbers, which today's release reads too) or
version 3 (each slot's file and own knobs, then the Ducks or Tracks set), the rest of the source's mods.bin
kept.

--masterfx-duck writes M4 (MASTERFX_DUCK): the source's sample instruments 1 and 3 (a kick and a closed hat
in the source we use) on tracks 1 and 3, one held ACD Init voice on track 8, and record 4 with Room in the
reverb slot and Ducker in the dynamics slot, the Ducks list on Master. The kick plays at the instrument's
volume every 8 steps, so it is the Sidechain key's loud part and there are gaps to hear the duck recover.

--masterfx-glue writes M5 (MASTERFX_GLUE): M4's kick, hat and held synth voice, and record 4 with Glue in the
dynamics slot on Track 1 and Track 3 (the Tracks list), turned hard (MASTERFX_GLUE_KNOBS) so it is plain to hear
that the drums are squashed and the synth isn't.

--masterfx-compressor writes M6 (MASTERFX_COMPRESSOR): M4's kick, hat and held synth voice, and record 4 with
Compressor in the dynamics slot ducking Synth 1 (the Ducks list), keyed by Track 1, the kick (CE's Sidechain
in project.mt), with CE's Threshold at -20 dB, so the synth plainly dips on every kick.

--sound writes that file's last set. `A1 Master FX sound` is one voice of ACD Init with a short note every 16
steps, so the gaps let the reverb and delay be heard, with no masterfx.bin. `R2 Real project sends` is the source
with its patterns and masterfx.bin kept, and every instrument and synth part sending to both effects: hellish
downtown's samples send nothing, so without this its effects would have nothing to process.

Every voice sounds all the time here, which is a CPU test and no kind of normal song: 8 held voices
take the audio pool to its limit on a build without masterfx too.

--worst is the worst case 1.2.0's headroom runs used (docs/TIMING_HEADROOM_PLAN.md there): every step of
all 16 tracks busy, at 800 BPM, sample tracks with micromoves and rolls, synth tracks with chords.
"""
import argparse
import shutil
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ce_to_120 as ce  # noqa: E402

# SynthPartSettings in core/source/mtStructs.h
SLOT_VOICES = 4
SLOT_TYPE = 8
SLOT_PATCH_NAME = 0x10
SLOT_PATCH_NAME_SIZE = 32
SLOT_REVERB_SEND = 0x30
SLOT_DELAY_SEND = 0x34
PROJECT_CRC = ce.PROJECT_SIZE - 4       # 0 means "no CRC", as older firmware wrote it

SYNTH_INSTRUMENTS = 64                  # SYNTH_INSTRUMENTS_START_IDX
VOICES = (4, 2, 2)                      # all 8
SEND = 60.0
# spread out, so each voice is heard and none are the same note
PART_NOTES = ((48, 55, 60, 64), (43, 67, 50, 62), (72, 76, 69, 79))
LAST_STEP = 63
PROJECT_TEMPO = 0x1C0                   # strMtValues::globalTempo, as this version of project.mt holds it
WORST_TEMPO = 800.0
FX_MICROMOVE, FX_ROLL, FX_CHANCE, FX_MIDI_CHORD = 2, 3, 4, 14    # Sequencer::enFxType
STEP_VOLUME = 30                        # of 100, CE's own per-step volume
STEP_VOLUME_AT = 6                      # note, instrument, 2 fx of 2 bytes, volume, micromove
SOUND_NOTES = (48, 55, 60, 67)
SOUND_VOLUME = 60                       # one voice, so still below the 8 held voices at 30
NOTE_OFF_DEFAULT = 0xFC                 # Sequencer::STEP_NOTE_OFF_DEFAULT, -4
REAL_SEND = 40
INSTRUMENT_DELAY_SEND = ce.INSTRUMENT + 262    # strInstrument::delaySend
INSTRUMENT_REVERB_SEND = ce.INSTRUMENT + 368   # strInstrument::reverbSend


def held(voices=VOICES) -> tuple:
    """(track, part, note) for each voice."""
    notes = [(part, note) for part, count in enumerate(voices) for note in PART_NOTES[part][:count]]
    return tuple((track, part, note) for track, (part, note) in enumerate(notes))


def pattern(voices=VOICES) -> bytes:
    data = bytearray(b"KS" + struct.pack("<H", 2) + bytes((1, 0, 0, 1)) + bytes((6,)) * 4
                     + struct.pack("<H", ce.PATTERN_V6_SIZE))
    data += bytes(ce.PATTERN_HEADER - len(data))
    empty = bytes((ce.EMPTY, 0, 0, 0, 0, 0, ce.EMPTY, ce.EMPTY))
    for _ in range(ce.TRACKS):
        data.append(LAST_STEP)
        data += empty * ce.STEPS
    data += bytes(4)
    assert len(data) == ce.PATTERN_V6_SIZE
    for track, part, note in held(voices):
        at = ce.PATTERN_HEADER + track * (1 + ce.STEPS * ce.STEP_V6) + 1
        data[at:at + 2] = bytes((note, SYNTH_INSTRUMENTS + part))
        data[at + STEP_VOLUME_AT] = STEP_VOLUME
    return bytes(data)


def worst_pattern(voices=VOICES) -> bytes:
    """Every step of all 16 tracks busy: samples on the first 8, the three synth parts on the rest."""
    data = bytearray(pattern(voices))
    parts = [part for part in range(len(voices)) if voices[part]] or [0]
    for track in range(ce.TRACKS):
        first = ce.PATTERN_HEADER + track * (1 + ce.STEPS * ce.STEP_V6) + 1
        for step in range(LAST_STEP + 1):
            at = first + step * ce.STEP_V6
            note = 36 + (track * 5 + step * 7) % 48
            if track < 8:
                instrument, fx = track, (FX_ROLL, 1 + step % 8, FX_MICROMOVE, step % 100)
            else:
                instrument = SYNTH_INSTRUMENTS + parts[track % len(parts)]
                fx = (FX_MIDI_CHORD, step % 16, FX_CHANCE, 100)
            data[at:at + 6] = bytes((note, instrument)) + bytes(fx)
            data[at + STEP_VOLUME_AT] = STEP_VOLUME
    return bytes(data)


def sound_pattern() -> bytes:
    """One short note every 16 steps on track 1, each with a note off 2 steps later."""
    data = bytearray(pattern((0, 0, 0)))
    start = ce.PATTERN_HEADER + 1
    for bar, note in enumerate(SOUND_NOTES):
        at = start + bar * 16 * ce.STEP_V6
        data[at:at + 2] = bytes((note, SYNTH_INSTRUMENTS))
        data[at + STEP_VOLUME_AT] = SOUND_VOLUME
        off = at + 2 * ce.STEP_V6
        data[off] = NOTE_OFF_DEFAULT
    return bytes(data)


def project_file(source: bytes, name: str, voices=VOICES, tempo=None, engine=None) -> bytes:
    mt = bytearray(source)
    if len(mt) != ce.PROJECT_SIZE or mt[:2] != b"MT" or mt[ce.HEADER_VERSION] != ce.PROJECT_VERSION:
        raise ce.Refused(f"project.mt: {len(mt)} bytes. I only know version {ce.PROJECT_VERSION} "
                         f"at {ce.PROJECT_SIZE} bytes.")
    mt[ce.PROJECT_NAME:ce.PROJECT_NAME + ce.PROJECT_NAME_SIZE] = name.encode("ascii").ljust(ce.PROJECT_NAME_SIZE, b"\0")
    limit = MAX_VOICES.get(engine, 8)
    if any(count > limit for count in voices):
        raise ValueError(f"{name}: {voices} voices, but {engine} allows {limit} a part")
    for slot, count in enumerate(voices):
        record = ce.SLOT_RECORDS + slot * ce.SLOT_RECORD_SIZE
        struct.pack_into("<i", mt, record + SLOT_VOICES, count)
        struct.pack_into("<f", mt, record + SLOT_REVERB_SEND, SEND)
        struct.pack_into("<f", mt, record + SLOT_DELAY_SEND, SEND)
    for slot in range(3) if engine else ():
        record = ce.SLOT_RECORDS + slot * ce.SLOT_RECORD_SIZE
        struct.pack_into("<i", mt, record + SLOT_TYPE, ENGINES.index(engine))
        mt[record + SLOT_PATCH_NAME:record + SLOT_PATCH_NAME + SLOT_PATCH_NAME_SIZE] = \
            b"Init".ljust(SLOT_PATCH_NAME_SIZE, b"\0")
    if tempo:
        struct.pack_into("<f", mt, PROJECT_TEMPO, tempo)
    mt[PROJECT_CRC:PROJECT_CRC + 4] = bytes(4)
    return bytes(mt)


def masterfx_file(reverb: int, delay: int) -> bytes:
    """As 1.2.0's patch wrote it: magic, reverb, delay, two zero bytes, then their FNV-1a."""
    data = bytes((reverb, delay, 0, 0))
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return b"HMF1" + data + struct.pack("<I", h)


# In the order of Synth::kSynthTypes, which project.mt's synthType counts in. WAVE and PHASE are the
# Synths mod's, after CE's seven in mod builds (core/synth/Synth.h).
# kSynthTypes' order (core/synth/Synth.h): project.mt stores a part's engine by its place here
ENGINES = ("ACD", "FAT", "VAP", "WTFM", "PERC", "DIRT", "CHIP", "WAVE", "PHASE", "DCO6")
# Voices a part allows where it's below 8: the patch's own limit, read on the rig through
# Synth::getMaxVoiceCount. The UI never saves more, but project.mt is written here byte for byte, and CE
# clamps a stored count on open only when the part's engine changes: a count above the limit runs above
# it whenever the project open before had the same engine (CE_FIRMWARE_MAP.md, 2026-11-10).
MAX_VOICES = {"PERC": 1}

# name, voices, (reverb, delay), the parts' engine. 8 voices split as L7 and L8 do: with all 8 on one
# part, the pattern sounded only 4 notes.
ENGINE_LADDER = (
    ("N1 4 voices CHIP Init", (4, 0, 0), None, "CHIP"),
    ("N2 8 voices ACD", (4, 2, 2), None, "ACD"),
    ("N3 8 voices FAT", (4, 2, 2), None, "FAT"),
    ("N4 8 voices VAP", (4, 2, 2), None, "VAP"),
    ("N5 8 voices WTFM", (4, 2, 2), None, "WTFM"),
    ("N6 3 voices PERC", (1, 1, 1), None, "PERC"),      # PERC allows 1 a part
    ("N7 8 voices DIRT", (4, 2, 2), None, "DIRT"),
    ("N8 8 ACD Shimmer BBD", (4, 2, 2), (4, 2), "ACD"),
)

# The Synths mod's set: name, voices, (reverb, delay), engine; lightest first, Init patches.
SYNTHS_LADDER = (
    ("S1 1 voice WAVE Init", (1, 0, 0), None, "WAVE"),
    ("S2 1 voice PHASE Init", (1, 0, 0), None, "PHASE"),
    ("S3 4 voices WAVE Init", (4, 0, 0), None, "WAVE"),
    ("S4 8 voices PHASE Init", (4, 2, 2), None, "PHASE"),
    ("S5 8 voices WAVE Init", (4, 2, 2), None, "WAVE"),
    ("S6 8 WAVE Shimmer BBD", (4, 2, 2), (4, 2), "WAVE"),
)

# name, voices, (reverb, delay), lightest first. Spring and Tape are the lightest of each kind,
# Shimmer and BBD the heaviest: tools/emu/test_bundle_cost.py.
LADDER = (
    ("L1 1 voice none", (1, 0, 0), None),
    ("L2 1 voice Tape", (1, 0, 0), (0, 1)),
    ("L3 1 voice Spring", (1, 0, 0), (3, 0)),
    ("L4 1 voice Shimmer BBD", (1, 0, 0), (4, 2)),
    ("L5 4 voices none", (4, 0, 0), None),
    ("L6 4 voices Shimmer BBD", (4, 0, 0), (4, 2)),
    ("L7 8 voices none", (4, 2, 2), None),
    ("L8 8 voices Shimmer BBD", (4, 2, 2), (4, 2)),
)


# Synth FX (core/mods/synthfx): mods.bin's set is record 0, a byte per mod id; Synth FX's settings
# are record 16, each part's ten steps in synthFx.h's order (synthfxSettings.cpp):
# Effect, Rate, Depth, Mix, Feedback, Distortion, Gain, Tone, Placement, Level.
MODS_MAGIC = b"MTP1"
MOD_MASTERFX = 3
MOD_SYNTHFX = 15
MASTERFX_RECORD = MOD_MASTERFX + 1      # reverb, delay (masterfx.cpp)
SYNTHFX_RECORD = MOD_SYNTHFX + 1
SYNTHFX_OFF = (0, 50, 50, 50, 145, 0, 50, 50, 1, 60)
# The heaviest: Distortion at full Gain into the Phaser, all of it wet, Feedback +90 %.
SYNTHFX_WORST = (3, 100, 100, 100, 185, 2, 100, 50, 1, 60)
SYNTHFX_DRIVE = (0, 50, 50, 50, 145, 1, 60, 50, 1, 60)      # Overdrive only

# The Synth FX set: name, voices, (reverb, delay), engine, each part's settings; lightest first.
SYNTHFX_LADDER = (
    ("F1 1 voice ACD Overdrive", (1, 0, 0), None, "ACD", (SYNTHFX_DRIVE,) * 3),
    ("F2 1 voice PERC worst FX", (1, 0, 0), None, "PERC", (SYNTHFX_WORST,) * 3),
    ("F3 4 voices ACD worst FX", (4, 0, 0), None, "ACD", (SYNTHFX_WORST,) * 3),
    ("F4 3 voices PERC worst FX", (1, 1, 1), None, "PERC", (SYNTHFX_WORST,) * 3),
    ("F5 3 PERC worst Shimmer BBD", (1, 1, 1), (4, 2), "PERC", (SYNTHFX_WORST,) * 3),
    # DIRT was the heaviest engine at 8 voices on the unit (MASTERFX_PORT_PLAN.md section 8)
    ("F6 8 voices DIRT worst FX", (4, 2, 2), None, "DIRT", (SYNTHFX_WORST,) * 3),
)


def store_records(data: bytes) -> list:
    """A mods.bin's records as (id, version, bytes); none if it isn't one."""
    if len(data) < 12 or data[:4] != MODS_MAGIC:
        return []
    _, length, crc = struct.unpack_from("<HHI", data, 4)
    payload = data[12:12 + length]
    if len(payload) != length or zlib.crc32(payload) != crc:
        return []
    records, at = [], 0
    while at + 4 <= len(payload):
        record, version, size = struct.unpack_from("<BBH", payload, at)
        records.append((record, version, payload[at + 4:at + 4 + size]))
        at += 4 + size
    return records


# Record 16 version 3 (synthfxSettings.cpp): for each part, FX1 then FX2, a slot's file name (48 B)
# and its 12 knobs (NaN: the plugin's default); then a Placement byte for each slot (1 was Pre on
# builds with WAVE inside the firmware; read as Post now).
SLOTS_VERSION = 3
SLOT_FILE, SLOT_KNOBS = 48, 12


def slots_record(parts) -> bytes:
    """parts: for each of the 3, (FX1, FX2), each None or (plugin, knobs, pre)"""
    slots, placement = b"", b""
    for part in parts:
        for slot in part:
            name, knobs, pre = slot or ("", (), False)
            file = f"{name}.ptsfx".encode() if name else b""
            assert len(file) < SLOT_FILE and len(knobs) <= SLOT_KNOBS
            vals = list(knobs) + [float("nan")] * (SLOT_KNOBS - len(knobs))
            slots += file.ljust(SLOT_FILE, b"\0") + struct.pack("<12f", *vals)
            placement += bytes([1 if name and pre else 0])
    assert len(slots + placement) == 582
    return slots + placement


# The plugin set: name, voices, (reverb, delay), engine, each part's slots; lightest first and the
# hang last. The Delay and the two Guard plugins are files of build/device/synthfx_card
# (tools/synthfx/device/build.sh), which go in /SynthFX/ on the card.
DIST_WORST = ("Distortion", (100, 50, 0), False)               # Gain 100 %, Tone 50 %, 0 dB
PHASER_WORST = ("Phaser", (50, 50, 90, 100), False)            # Rate, Depth, Feedback +90 %, Mix 100 %
NONE = (None, None)                                             # a part with both slots empty
PLUGIN_LADDER = (
    ("P1 1 voice ACD Delay", (1, 0, 0), None, "ACD", ((None, ("Delay", (), False)), NONE, NONE)),
    ("P2 8 WAVE worst FX", (4, 2, 2), None, "WAVE", ((DIST_WORST, PHASER_WORST),) * 3),
    ("G1 1 voice ACD GuardSlow", (1, 0, 0), None, "ACD", ((("GuardSlow", (), False), None), NONE, NONE)),
    ("G2 1 voice ACD GuardHang", (1, 0, 0), None, "ACD", ((("GuardHang", (), False), None), NONE, NONE)),
)


# The Master FX plugin set (docs/mt/DEVICE_TEST_LADDER.md): name, voices, engine, and record 4: a
# (reverb, delay) pair is version 1, which today's release reads too; three file names (reverb, delay,
# dynamics) are version 3, with the dynamics slot's own knobs (None: the plugin's default).
MASTERFX_PLUGIN_SET = (
    ("M1 8 ACD Shimmer BBD", (4, 2, 2), "ACD", (4, 2), ()),
    ("M2 8 ACD Shimmer BBD Glue", (4, 2, 2), "ACD", ("Shimmer.ptmfx", "BBD.ptmfx", "Glue.ptmfx"), ()),
    ("M3 1 voice ACD GuardSlow", (1, 0, 0), "ACD", ("", "", "GuardSlow.ptmfx"), (1.0,)),   # Load: Heavy
)
# M4: the Ducks list and the own knobs (DEVICE_TEST_LADDER.md). Tracks are 0-based here.
MASTERFX_DUCK = ("M4 1 voice ACD Room Ducker", ("Room.ptmfx", "", "Ducker.ptmfx"))
DUCK_KICK = (0, 0, 8)                   # track, instrument (the file "1 ..."), every 8 steps
DUCK_HAT = (2, 2, 2, 40)                # track, instrument (the file "3 ..."), every 2 steps, volume
DUCK_SYNTH_TRACK = 7
MASTERFX_UNSET = struct.pack("<I", 0x7FC00000)    # masterfx.cpp's unset(): the plugin's default
MASTERFX_OWN_KNOBS = 5
# M5: Glue on the drums only. Its own knobs: Attack (step 20: 1 ms), Release (the default), Threshold
# -30 dB, Ratio 10:1, Makeup +6 dB. The Tracks list: Track 1 and Track 3 (bits 0 and 2).
MASTERFX_GLUE = ("M5 1 voice ACD Glue drums", ("", "", "Glue.ptmfx"))
MASTERFX_GLUE_KNOBS = (20.0, None, -30.0, 10.0, 6.0)
MASTERFX_GLUE_TRACKS = (1 << DUCK_KICK[0]) | (1 << DUCK_HAT[0])
# M6: Compressor ducking Synth 1 (bit 8) from Track 1, the kick. CE's limiter values in project.mt (the
# file is strProjectFile: strMtValues at 0x110, as cFileManager::writeProjectFileToFileStruct copies
# it): limiterThreshold (uint16, 0 to 32767 linear) at 0x128 and limiterSidechain (uint8: 0 Disable,
# 1 to 8 Track 1 to 8) at 0x4FB (0x110 + 0x3EB).
MASTERFX_COMPRESSOR = ("M6 1 voice ACD Compressor duck", ("", "", "Compressor.ptmfx"))
MASTERFX_COMPRESSOR_DUCKS = 1 << 8      # Synth 1
PROJECT_LIMITER_THRESHOLD = 0x128
PROJECT_LIMITER_SIDECHAIN = 0x4FB
COMPRESSOR_THRESHOLD = 3277             # -20 dB
COMPRESSOR_SIDECHAIN = DUCK_KICK[0] + 1 # Track 1


def masterfx_record(files, dynamics_knobs=(), ducks=0) -> bytes:
    """Record 4 version 3 (masterfx.h's Settings): each slot's file (48 B) and five own knobs, the
    dynamics slot's from dynamics_knobs; then the Ducks or Tracks mask and two reserved bytes. 208 B."""
    out = b""
    for slot, name in enumerate(files):
        knobs = tuple(dynamics_knobs) if slot == 2 else ()
        knobs = knobs + (None,) * (MASTERFX_OWN_KNOBS - len(knobs))
        assert len(name) < 48
        out += name.encode("ascii").ljust(48, b"\0")
        out += b"".join(MASTERFX_UNSET if k is None else struct.pack("<f", k) for k in knobs)
    out += struct.pack("<H", ducks) + bytes(2)
    assert len(out) == 208
    return out


def masterfx_mods_file(source: bytes, version: int, record: bytes) -> bytes:
    """The source's mods.bin (or none) with Master FX On and record 4 as given"""
    records = [r for r in store_records(source) if r[0] not in (0, MASTERFX_RECORD)]
    old = [r for r in store_records(source) if r[0] == 0]
    switches = bytearray(max(MOD_MASTERFX + 1, len(old[0][2]) if old else 0))
    if old:
        switches[:len(old[0][2])] = old[0][2]
    switches[MOD_MASTERFX] = 1
    records = [(0, 1, bytes(switches))] + records + [(MASTERFX_RECORD, version, record)]
    payload = b"".join(struct.pack("<BBH", i, v, len(d)) + d for i, v, d in records)
    return MODS_MAGIC + struct.pack("<HHI", 1, len(payload), zlib.crc32(payload)) + payload


def make_masterfx_plugins(source: Path, parent: Path) -> list:
    out = []
    for name, voices, engine, slots, knobs in MASTERFX_PLUGIN_SET:
        folder = make(source, parent, name, voices, engine=engine)
        old = folder / "mods.bin"
        old_bytes = old.read_bytes() if old.exists() else b""
        if isinstance(slots[0], int):
            old.write_bytes(masterfx_mods_file(old_bytes, 1, bytes(slots)))
        else:
            old.write_bytes(masterfx_mods_file(old_bytes, 3, masterfx_record(slots, knobs)))
        out.append(folder)
    return out


def duck_pattern(synth: bool = True, drums: bool = True, drum_volume=None) -> bytes:
    """The kick, the hat and the held synth voice of MASTERFX_DUCK (the rig leaves some out, or
    turns the drums down to drum_volume so the mix doesn't clip)."""
    data = bytearray(pattern((0, 0, 0)))

    def step(track, at_step):
        return ce.PATTERN_HEADER + track * (1 + ce.STEPS * ce.STEP_V6) + 1 + at_step * ce.STEP_V6
    if drums:
        track, instrument, every = DUCK_KICK
        for s in range(0, LAST_STEP + 1, every):
            data[step(track, s):step(track, s) + 2] = bytes((60, instrument))    # volume left unset: the instrument's
            if drum_volume is not None:
                data[step(track, s) + STEP_VOLUME_AT] = drum_volume
        track, instrument, every, volume = DUCK_HAT
        for s in range(0, LAST_STEP + 1, every):
            at = step(track, s)
            data[at:at + 2] = bytes((60, instrument))
            data[at + STEP_VOLUME_AT] = volume if drum_volume is None else drum_volume
    if synth:
        at = step(DUCK_SYNTH_TRACK, 0)
        data[at:at + 2] = bytes((PART_NOTES[0][0], SYNTH_INSTRUMENTS))
        data[at + STEP_VOLUME_AT] = STEP_VOLUME
    return bytes(data)


def make_masterfx_duck(source: Path, parent: Path) -> list:
    name, files = MASTERFX_DUCK
    folder = make(source, parent, name, (1, 0, 0), engine="ACD")
    for path in (folder / "patterns").glob("pattern_*.mtp"):
        path.write_bytes(duck_pattern())
    old = folder / "mods.bin"
    old_bytes = old.read_bytes() if old.exists() else b""
    old.write_bytes(masterfx_mods_file(old_bytes, 3, masterfx_record(files)))
    return [folder]


def make_masterfx_glue(source: Path, parent: Path, name: str = MASTERFX_GLUE[0], knobs=MASTERFX_GLUE_KNOBS,
                       tracks: int = MASTERFX_GLUE_TRACKS, synth: bool = True, drums: bool = True,
                       drum_volume=None) -> list:
    """M5, or (for the rig) the same with other knobs or tracks, the synth or the drums left out, or
    the drums quieter"""
    folder = make(source, parent, name, (1, 0, 0), engine="ACD")
    for path in (folder / "patterns").glob("pattern_*.mtp"):
        path.write_bytes(duck_pattern(synth, drums, drum_volume))
    old = folder / "mods.bin"
    old_bytes = old.read_bytes() if old.exists() else b""
    old.write_bytes(masterfx_mods_file(old_bytes, 3, masterfx_record(MASTERFX_GLUE[1], knobs, tracks)))
    return [folder]


def make_masterfx_compressor(source: Path, parent: Path, name: str = MASTERFX_COMPRESSOR[0],
                             ducks: int = MASTERFX_COMPRESSOR_DUCKS, sidechain: int = COMPRESSOR_SIDECHAIN,
                             dynamics: str = MASTERFX_COMPRESSOR[1][2], synth: bool = True,
                             drums: bool = True) -> list:
    """M6, or (for a check) the same with another set, key or dynamics file, or the synth or the drums
    left out"""
    folder = make(source, parent, name, (1, 0, 0), engine="ACD")
    for path in (folder / "patterns").glob("pattern_*.mtp"):
        path.write_bytes(duck_pattern(synth, drums))
    old = folder / "mods.bin"
    old_bytes = old.read_bytes() if old.exists() else b""
    old.write_bytes(masterfx_mods_file(old_bytes, 3, masterfx_record(("", "", dynamics), (), ducks)))
    mt = bytearray((folder / "project.mt").read_bytes())
    struct.pack_into("<H", mt, PROJECT_LIMITER_THRESHOLD, COMPRESSOR_THRESHOLD)
    mt[PROJECT_LIMITER_SIDECHAIN] = sidechain
    mt[PROJECT_CRC:PROJECT_CRC + 4] = bytes(4)      # no CRC, as make() leaves it
    (folder / "project.mt").write_bytes(mt)
    return [folder]


def synthfx_mods_file(source: bytes, parts, effects=None, version=1) -> bytes:
    """The source's mods.bin (or none) with Synth FX and Master FX On and record 16 holding parts
    (version 1: each part's ten steps; 3: the bytes of slots_record). effects: (reverb, delay) for
    Master FX's record, or the source's kept."""
    dropped = (0, SYNTHFX_RECORD) + ((MASTERFX_RECORD,) if effects else ())
    records = [r for r in store_records(source) if r[0] not in dropped]
    switches = bytearray(MOD_SYNTHFX + 1)
    old = [r for r in store_records(source) if r[0] == 0]
    if old:
        switches[:len(old[0][2])] = old[0][2][:len(switches)]
    switches[MOD_MASTERFX] = switches[MOD_SYNTHFX] = 1
    if version == 1:
        settings = b"".join(bytes(p) for p in parts)
        assert len(settings) == 30
    else:
        settings = bytes(parts)
    records = [(0, 1, bytes(switches))] + records + [(SYNTHFX_RECORD, version, settings)]
    if effects:
        records.append((MASTERFX_RECORD, 1, bytes(effects)))
    payload = b"".join(struct.pack("<BBH", i, v, len(d)) + d for i, v, d in records)
    return MODS_MAGIC + struct.pack("<HHI", 1, len(payload), zlib.crc32(payload)) + payload


def make_synthfx(source: Path, parent: Path, name: str = "synthfx", voices=(1, 1, 1), worst: bool = True,
                 engine: str = "PERC", parts=(SYNTHFX_WORST,) * 3, effects=(4, 2)) -> Path:
    """By default the release tier's worst case: the worst pattern on PERC (1 voice a part, its limit),
    the heaviest insert on every part, and Shimmer and BBD, the heaviest Master FX."""
    folder = make(source, parent, name, voices, worst=worst, engine=engine)
    old = folder / "mods.bin"
    (folder / "mods.bin").write_bytes(synthfx_mods_file(old.read_bytes() if old.exists() else b"", parts,
                                                        effects or (0, 0)))
    return folder


def make_synthfx_ladder(source: Path, parent: Path) -> list:
    out = []
    for name, voices, effects, engine, parts in SYNTHFX_LADDER:
        out.append(make_synthfx(source, parent, name, voices, worst=False, engine=engine, parts=parts,
                                effects=effects))
    return out


def make_plugin_project(source: Path, parent: Path, entry, name=None) -> Path:
    """One of PLUGIN_LADDER, under its own name or the one given"""
    title, voices, effects, engine, parts = entry
    folder = make(source, parent, name or title, voices, engine=engine)
    old = folder / "mods.bin"
    old_bytes = old.read_bytes() if old.exists() else b""
    old.write_bytes(synthfx_mods_file(old_bytes, slots_record(parts), effects or (0, 0), SLOTS_VERSION))
    return folder


def make_plugin_ladder(source: Path, parent: Path) -> list:
    return [make_plugin_project(source, parent, entry) for entry in PLUGIN_LADDER]


def make_ladder(source: Path, parent: Path) -> list:
    out = []
    for name, voices, effects in LADDER:
        folder = make(source, parent, name, voices)
        if effects:
            (folder / "masterfx.bin").write_bytes(masterfx_file(*effects))
        out.append(folder)
    return out


def make_engines(source: Path, parent: Path, ladder=ENGINE_LADDER) -> list:
    out = []
    for name, voices, effects, engine in ladder:
        folder = make(source, parent, name, voices, engine=engine)
        if effects:
            (folder / "masterfx.bin").write_bytes(masterfx_file(*effects))
        out.append(folder)
    return out


def make_sound(source: Path, parent: Path) -> list:
    sound = make(source, parent, "A1 Master FX sound", (1, 0, 0), engine="ACD")
    for path in (sound / "patterns").glob("pattern_*.mtp"):
        path.write_bytes(sound_pattern())

    name = "R2 Real project sends"
    real = parent / name
    if real.exists():
        raise ce.Refused(f"{real} already exists, and I won't write over it.")
    shutil.copytree(source, real)
    mt = bytearray((real / "project.mt").read_bytes())
    mt[ce.PROJECT_NAME:ce.PROJECT_NAME + ce.PROJECT_NAME_SIZE] = name.encode("ascii").ljust(ce.PROJECT_NAME_SIZE, b"\0")
    for slot in range(3):
        record = ce.SLOT_RECORDS + slot * ce.SLOT_RECORD_SIZE
        struct.pack_into("<f", mt, record + SLOT_REVERB_SEND, float(REAL_SEND))
        struct.pack_into("<f", mt, record + SLOT_DELAY_SEND, float(REAL_SEND))
    mt[PROJECT_CRC:PROJECT_CRC + 4] = bytes(4)
    (real / "project.mt").write_bytes(mt)
    for path in (real / "instruments").glob("*.pti"):
        data = bytearray(path.read_bytes())
        data[INSTRUMENT_DELAY_SEND] = data[INSTRUMENT_REVERB_SEND] = REAL_SEND
        data[ce.INSTRUMENT_CRC:ce.INSTRUMENT_CRC + 4] = bytes(4)  # as 1.2.0 writes it; CE doesn't check it
        path.write_bytes(data)
    return [sound, real]


def make(source: Path, parent: Path, name: str, voices=VOICES, keep_masterfx: bool = False, worst: bool = False,
         engine=None) -> Path:
    out = parent / name
    if out.exists():
        raise ce.Refused(f"{out} already exists, and I won't write over it.")
    new_project = project_file((source / "project.mt").read_bytes(), name, voices, WORST_TEMPO if worst else None,
                               engine)
    parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, out)
    (out / "project.mt").write_bytes(new_project)
    for slot in range(3) if engine else ():
        (out / f"{slot}.patch").unlink(missing_ok=True)  # the source's own sound, and perhaps another engine's
    (out / "patterns").mkdir(exist_ok=True)
    names = {p.name for p in (out / "patterns").glob("pattern_*.mtp")} | {"pattern_01.mtp"}
    for filename in names:
        (out / "patterns" / filename).write_bytes(worst_pattern(voices) if worst else pattern(voices))
    if not keep_masterfx and (out / "masterfx.bin").exists():
        (out / "masterfx.bin").unlink()
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--name", default="MFX test")
    parser.add_argument("--voices", default="4,2,2", help="on the three synth parts")
    parser.add_argument("--keep-masterfx", action="store_true")
    parser.add_argument("--worst", action="store_true")
    parser.add_argument("--effects", help="reverb,delay numbers for masterfx.bin")
    parser.add_argument("--ladder", action="store_true")
    parser.add_argument("--engines", action="store_true")
    parser.add_argument("--synths", action="store_true")
    parser.add_argument("--sound", action="store_true")
    parser.add_argument("--synthfx", action="store_true")
    parser.add_argument("--synthfx-plugins", action="store_true")
    parser.add_argument("--masterfx-plugins", action="store_true")
    parser.add_argument("--masterfx-duck", action="store_true")
    parser.add_argument("--masterfx-glue", action="store_true")
    parser.add_argument("--masterfx-compressor", action="store_true")
    args = parser.parse_args()
    try:
        if (args.ladder or args.engines or args.sound or args.synths or args.synthfx or args.synthfx_plugins
                or args.masterfx_plugins or args.masterfx_duck or args.masterfx_glue or args.masterfx_compressor):
            maker = (make_ladder if args.ladder else make_engines if args.engines else make_sound if args.sound
                     else make_synthfx_ladder if args.synthfx
                     else make_plugin_ladder if args.synthfx_plugins
                     else make_masterfx_plugins if args.masterfx_plugins
                     else make_masterfx_duck if args.masterfx_duck
                     else make_masterfx_glue if args.masterfx_glue
                     else make_masterfx_compressor if args.masterfx_compressor
                     else lambda source, parent: make_engines(source, parent, SYNTHS_LADDER))
            for folder in maker(args.project, args.out):
                print(f"Copy written: {folder}")
            return 0
        voices = tuple(int(v) for v in args.voices.split(","))
        folder = make(args.project, args.out, args.name, voices, args.keep_masterfx, args.worst)
        if args.effects:
            (folder / "masterfx.bin").write_bytes(masterfx_file(*(int(v) for v in args.effects.split(","))))
        print(f"Copy written: {folder}")
    except ce.Refused as e:
        print(e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
