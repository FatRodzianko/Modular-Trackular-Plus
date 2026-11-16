#!/usr/bin/env python3
"""USB audio on the sound rig, with a computer attached, on both builds.

Usage: python tools/emu/test_usb_audio.py <mods.axf> <reference.axf>

Since CE's 7482cc8 the USB output (UsbOutputStream) is sent blocks only while
UsbAudio::isOutputActive() says a computer takes them: syncActive, at the start of each audio
interrupt, switches it, and on the way off releases what it holds. The mods' audio reaches it:
the master after the dynamics slot (USB channels 0 and 1), the reverb and delay objects that run
the Master FX plugins (18 to 21), and each synth part after its Synth FX slots (22 to 27). Tracks
1 to 8 are channels 2 to 17. The platform's side (audio_unified.c, the USB stack) isn't run: a
fake UsbAudio here takes what UsbOutputStream::update fills, as fillUsbAudioChannelData would.

Checks:
- mods Off: every USB channel and the speaker output as the reference build's, sample for sample;
- Master FX on (Shimmer, BBD, Glue with its threshold down): USB's master is the speaker output,
  sample for sample, also over the silence where the dynamics slot makes its own zeroed blocks;
  the reverb and delay channels carry the plugins' tails;
- a computer attached, then unplugged and attached again while the tails play: the speaker output
  as in a run with no computer, sample for sample; nothing sent while unplugged; the first block
  attached again USB's master silent (the reverb, delay and synth channels not), as on the
  reference build, and USB's master the speaker output after it;
  the audio pool back where the run without a computer has it;
- Synth FX (Flanger and Chorus on part 1): the part's USB channels carry it, its tail included,
  and USB's master is the speaker output.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import test_masterfx_rig as mfx  # noqa: E402
import test_synthfx as sfx  # noqa: E402
from sound_rig import BLOCK, peak  # noqa: E402

failures = 0
CHANNELS = 28                       # AUDIO_IN_FORMAT_CHANNELS
MASTER = (0, 1)
REVERB = (18, 19)                   # UsbOutputStream::kReverb
DELAY = (20, 21)                    # kDelay
SYNTH1 = (22, 23)                   # kFirstSynth
POOL_USED = "_ZN11AudioStream11memory_usedE"


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


class Usb:
    """A computer on USB: UsbAudio's four methods, put where Tracker's init puts the platform's
    (AudioStream::usbAudio). `host` is what isOutputActive answers."""

    def __init__(self, r, host=True):
        m = r.m
        self.r, self.host, self.blocks, self.cur = r, host, [], {}

        def fill(mm):
            ptr, count, channel = mm.arg(1), mm.arg(2), mm.arg(3)
            self.cur[channel] = struct.unpack(f"<{count}h", m.read(ptr, 2 * count))
            return 0
        vtable = m.alloc(16, 4)
        for i, f in enumerate((lambda mm: 0, fill, lambda mm: 0, lambda mm: 1 if self.host else 0)):
            m.w32(vtable + 4 * i, m.add_stub(f))
        obj = m.alloc(8, 4)
        m.w32(obj, vtable)
        m.w32(m.addr("_ZN11AudioStream8usbAudioE"), obj)

    def block(self):
        """One interrupt, as the IMX's updateBuffers: beginFillData, the engine, finishFillData"""
        self.cur = {}
        out = self.r.block()
        self.blocks.append(self.cur)
        return out

    def run(self, n):
        left, right = [], []
        for _ in range(n):
            a, b = self.block()
            left += a
            right += b
        return left, right

    def channel(self, c, first=0, last=None):
        out = []
        for b in self.blocks[first:last]:
            out += b.get(c, (0,) * BLOCK)
        return out


def pool_used(r):
    return struct.unpack("<H", r.m.read(r.m.addr(POOL_USED), 2))[0]


def phrase(u, on=40, tail=260):
    u.r.note(0, 60, True)
    a = u.run(on)
    u.r.note(0, 60, False)
    b = u.run(tail)
    return a[0] + b[0], a[1] + b[1]


def masterfx_on(g):
    mfx.set_mod(g, True, 4, 2)                      # Shimmer, BBD (masterfx-1.x's list indices)
    mfx.set_dynamics(g, "Glue.ptmfx")
    mfx.limiter_knobs(g, 6000)                      # well under the note's peak: Glue works


def test_off(mods, ref):
    print("-- mods Off: as CE --")
    got = {}
    for name, axf in (("ref", ref), ("mods", mods)):
        g = mfx.Graph(axf, 50, 50)
        u = Usb(g)
        out = phrase(u)
        got[name] = (out, [u.channel(c) for c in range(CHANNELS)], [sorted(b) for b in u.blocks])
    (out_r, ch_r, seen_r), (out_m, ch_m, seen_m) = got["ref"], got["mods"]
    check(out_m == out_r, "the speaker output as the reference build's, sample for sample")
    check(seen_m == seen_r and all(len(s) == CHANNELS for s in seen_m),
          f"all {CHANNELS} channels filled every block, as on the reference build")
    differ = [c for c in range(CHANNELS) if ch_m[c] != ch_r[c]]
    check(not differ, f"every USB channel as the reference build's, sample for sample (differ: {differ})")
    check(ch_r[0] == list(out_r[0]) and ch_r[1] == list(out_r[1]),
          "the reference build's USB master is its speaker output (what the rig compares to)")
    for name, pair in (("reverb", REVERB), ("delay", DELAY), ("synth part 1", SYNTH1)):
        check(peak(ch_r[pair[0]]) > 100, f"the {name} channels carry sound (peak {peak(ch_r[pair[0]])})")


def test_masterfx(mods):
    print("-- Master FX on --")
    g = mfx.Graph(mods, 50, 50)
    masterfx_on(g)
    u = Usb(g)
    out = phrase(u)
    master = (u.channel(0), u.channel(1))
    check(master[0] == list(out[0]) and master[1] == list(out[1]),
          "USB's master is the speaker output, sample for sample, Shimmer, BBD and Glue playing")
    # the tail: 40 blocks after the note's end on
    tail = 80 * BLOCK
    check(peak(u.channel(REVERB[0], 80)) > 0 and peak(u.channel(DELAY[0], 80)) > 0,
          f"the reverb and delay channels carry the plugins' tails (peaks {peak(u.channel(REVERB[0], 80))}, "
          f"{peak(u.channel(DELAY[0], 80))})")
    silent = [i for i in range(len(u.blocks)) if peak(out[0][i * BLOCK:(i + 1) * BLOCK]) == 0]
    check(peak(out[0][tail:]) > 0, f"the tail reaches the speaker (peak {peak(out[0][tail:])})")
    print(f"     ({len(silent)} silent master blocks in {len(u.blocks)})")
    check(not g.failures, "no asserts")


def test_unplug(mods, ref):
    print("-- unplugged and attached again while playing --")
    runs = {}
    plug = ((True, 100), (False, 80), (True, 120))
    for name, axf, plan in (("none", mods, ((False, 300),)), ("plug", mods, plug), ("ce", ref, plug)):
        g = mfx.Graph(axf, 50, 50)
        if axf == mods:
            masterfx_on(g)
        u = Usb(g, host=plan[0][0])
        g.note(0, 60, True)
        left, right, used, marks = [], [], [], []
        done = 0
        for host, n in plan:
            u.host = host
            marks.append((done, done + n, host))
            for _ in range(n):
                if done == 40:
                    g.note(0, 60, False)
                a, b = u.block()
                left += a
                right += b
                used.append(pool_used(g))
                done += 1
        runs[name] = (u, (left, right), used, marks, g)
    u0, out0, used0, _, _ = runs["none"]
    u1, out1, used1, marks, g1 = runs["plug"]
    check(out1 == out0, "the speaker output as in the run with no computer, sample for sample")
    check(all(peak(v) == 0 for b in u0.blocks for v in b.values()), "no computer: nothing but silence filled")
    first, last, _ = marks[1]
    sent_off = max((peak(v) for b in u1.blocks[first:last] for v in b.values()), default=0)
    check(sent_off == 0, f"unplugged (blocks {first} to {last - 1}): nothing but silence filled")
    # Attached again, USB's master is silent for one block, on CE's own build too: USB, like the
    # speaker, is handed the master block the last interrupt made, and that block wasn't queued for
    # USB while it was unplugged (an inactive destination takes no block). The reverb, delay and
    # synth channels come from objects that run in this interrupt, so they carry sound at once.
    again = marks[2][0]
    for name in ("plug", "ce"):
        u, out = runs[name][0], runs[name][1]
        b = u.blocks[again]
        check(peak(out[0][again * BLOCK:(again + 1) * BLOCK]) > 0 and peak(b.get(0, ())) == 0 and
              peak(b.get(1, ())) == 0 and peak(b.get(DELAY[0], ())) > 0,
              f"{name}: the first block attached again (block {again}): USB's master silent, the speaker "
              f"and USB's delay channel playing")
    for a, b in ((marks[0][0], marks[0][1]), (again + 1, marks[2][1])):
        m_ok = u1.channel(0, a, b) == list(out1[0][a * BLOCK:b * BLOCK]) and \
            u1.channel(1, a, b) == list(out1[1][a * BLOCK:b * BLOCK])
        check(m_ok, f"attached (blocks {a} to {b - 1}): USB's master is the speaker output, sample for sample")
    check(peak(u1.channel(DELAY[0], marks[2][0])) > 0,
          f"attached again: the delay channel carries BBD's echoes (peak {peak(u1.channel(DELAY[0], marks[2][0]))})")
    off_end = marks[1][1] - 1
    check(used1[off_end] == used0[off_end] and used1[-1] == used0[-1],
          f"the audio pool: unplugged and at the end as with no computer ({used1[off_end]}/{used0[off_end]}, "
          f"{used1[-1]}/{used0[-1]} blocks in use)")
    check(not g1.failures, "no asserts")


def test_synthfx(mods):
    print("-- Synth FX on part 1 --")
    r = sfx.Rig(mods)
    r.set_instrument(0, sfx.ACD)
    u = Usb(r)
    r.fx(0, 0, "Flanger")
    r.fx(0, 1, "Chorus")
    r.update()
    r.note(0, 60, True)
    a = u.run(30)
    r.note(0, 60, False)
    b = u.run(120)
    out = (a[0] + b[0], a[1] + b[1])
    check(r.instances() == 2, f"both plugins made ({r.instances()})")
    left, right = u.channel(SYNTH1[0]), u.channel(SYNTH1[1])
    check(peak(left) > 100 and left != right,
          f"part 1's USB channels carry it, in stereo as the Flanger makes it (peak {peak(left)})")
    check(peak(u.channel(SYNTH1[0], 35)) > 0, f"and its tail after the note (peak {peak(u.channel(SYNTH1[0], 35))})")
    check(u.channel(0) == list(out[0]) and u.channel(1) == list(out[1]),
          "USB's master is the speaker output, sample for sample")
    check(all(peak(u.channel(c)) == 0 for c in range(24, 28)), "parts 2 and 3 silent")
    check(not r.failures, "no asserts")


def main():
    mods, ref = Path(sys.argv[1]), Path(sys.argv[2])
    test_off(mods, ref)
    test_masterfx(mods)
    test_unplug(mods, ref)
    test_synthfx(mods)
    print(f"{failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
