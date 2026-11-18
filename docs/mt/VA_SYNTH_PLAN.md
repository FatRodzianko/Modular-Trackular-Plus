# DCO6: a virtual-analog synth engine after the Juno-106 — plan

Status: **done 2026-11-17, merged into `modular` as `synths-1.1`.** Approved and built the same day
(steps 1-2 in 297c342, 3-6 in 430c998, the device projects in 542375d). The answers to
[section 12](#12-decisions) are recorded there. Step 7: the release tier passed, the device run
passed (E1 to E5), and the merge went through the checklist.

What exists:
- a Python model of the voice that renders listening examples, and is Level A's reference;
- the engine and its `.psyn` build (`make psyn` stages `DCO6.psyn` into `build/Synths`);
- Level A: the engine on Arm in the emulator, 61 checks against the model, and 8 planted faults;
- the Synths mod's registration (type id 66, the picker, the `ownsLayout`/`sidesAlike` split) and
  the unison hook in CE's host ([5.1](#51-the-host-hook));
- Level B, `tools/emu/test_dco6.py`: 16 checks through CE's host on a full build;
- the user page's DCO6 section (`docs/mods/synths.md`) and its mockups in
  `docs/mods/img/synths-dco6-*.svg`, drawn by `tools/docs/mockups_dco6.py` from the firmware's
  strings.

[Section 13](#13-what-was-measured-estimated-and-not-checked) says what was measured, what was only
estimated, and what wasn't checked.

## 1. Summary

- **DCO6** is a simple polysynth after the Juno-106's design. Each voice has one DCO (saw, pulse
  with PWM, sub, noise), a continuous high-pass with a Bass Boost switch, a resonant 4-pole low-pass, and one ADSR
  shared by the filter and the amp. There's one LFO, with seven shapes and a delay.
- **New: Vintage.** One control from 0 to 100 % sets how far each voice strays, both fixed per voice
  and drifting over time: pitch, cutoff, envelope times, pulse width and level.
- **New: Unison.** Stacks of 2 to 8 voices, taken from the voices you give the part on the synth's
  Synth Parameters screen. **Detune** goes up to 50 cents and **Width** spreads the stack in stereo.
- **Feasible, with one firmware change.** A `.psyn` engine alone can't do unison cleanly. CE's host
  hands each note to one voice and has no way to tell an engine how many voices the part has. The
  plan adds a small MT+ hook in CE's open host (`InstrumentImpl`) that hands a note to a group of
  voices. Stereo is already in the API: every voice renders a left and a right side.
- **Cost:** about **18,900 instructions per voice per block** on the Init patch, measured in the emulator through CE's
  host at 8 voices. WAVE measures 14,900 and PHASE 14,100 in the same rig. Unison costs no more
  per voice: 8 voices cost the same whether they play 8 notes or one 8-voice stack. A heavy patch,
  with the HPF and Bass Boost both on, measures 22,700.
- **Memory:** no ITCM, DTCM or SDRAM of the firmware for the engine itself. It lives in CE's
  `.psyn` arena: 45 KB of image and 368 B per voice. The host hook adds a few hundred bytes of
  ITCM. DTCM is untouched, which matters: it has 40 bytes over its floor.

## 2. The Juno-106, and where DCO6 follows it

The 106's design, from Roland's own pages ([Icon Series article][roland-icon],
[tech specs][roland-specs]) and the 106 hardware as commonly documented:
- One DCO per voice, 6 voices. Saw and pulse each switch on or off. A square sub-oscillator one
  octave down and a white noise source each have a level slider.
- PWM is manual, or from the LFO.
- A non-resonant high-pass on a slider with 4 notches, 0 to 3. It can't be set between them. 0 turns
  the high-pass off and boosts the bass; 1 is flat (the high-pass bypassed, no boost); 2 and 3 cut
  the lows at two corners. The boost and the high-pass can't be on together. The Juno-60 before it
  had a smooth HPF slider and no boost.
- A resonant 24 dB/octave low-pass that self-oscillates, with envelope amount, envelope polarity,
  LFO amount and keyboard follow.
- A VCA switched between the envelope and a plain gate.
- One ADSR, used by the filter and the VCA.
- One LFO, triangle only, with rate and delay. It's one LFO for the whole synth.
- The built-in chorus (I, II, I+II).
- Portamento.
- Poly 1, Poly 2, and a unison mode (both Poly buttons together) that stacks all 6 voices with no
  detune.

| Part | Juno-106 | DCO6 | Same or different |
|---|---|---|---|
| Oscillator | One DCO, Range 16' 8' 4' | One DCO, **Range** 16' 8' 4', plus **Tune** ±12 st and **Fine Tune** ±50 ct | Same; tuning added |
| Saw, pulse | On/off switches | **Saw** and **Pulse** levels, 0-100 % | Different: levels instead of switches. 100 % and 0 % give the 106's two settings |
| Sub | Square, one octave down, level | **Sub**, the same | Same |
| Noise | White, level | **Noise**, white | Same |
| Pulse width | PWM slider, Manual or LFO | **Pulse Width** (50 % duty at 0, 5 % at 100 %), **PWM Amt**, **PWM Source** LFO or Env | Different: the width and the PWM depth are separate controls, and Env is added (the Juno-60 had it). On the 106 one slider did both jobs, depending on the switch |
| HPF | A slider with 4 notches: 0 bass boost, 1 flat, 2 and 3 high-pass | **HPF** Off, then 20 Hz - 2 kHz, smooth; **Bass Boost** Off or On | Different, by your choice (2026-11-17): the high-pass is continuous, like the Juno-60's, and the boost is its own switch, so both can be on. The 106's notches are inside the range. The boost (+6 dB below about 110 Hz) is chosen by ear, not measured from a 106 |
| Low-pass | 4-pole, resonant, self-oscillates | 4-pole ladder, **Resonance** self-oscillates at 100 % | Same. The bass drop at high resonance is kept, partly made up |
| Envelope to filter | Amount, plus a polarity switch | **Env Amt** −100 to +100 % | Different: one bipolar control instead of two |
| LFO to filter | Amount | **LFO to Cutoff** | Same |
| Key follow | Slider | **Key Track** 0-100 % | Same |
| VCA | Env or Gate switch | **VCA Mode** Env or Gate | Same. Gate has a 2 ms ramp, so no clicks |
| Envelope | One ADSR | One ADSR: Attack 1 ms to 3 s, Decay and Release 1 ms to 12 s | Same. The segments are exponential, like an analog RC. The ranges are chosen to cover the 106's commonly quoted ranges, not checked against one |
| LFO | Triangle, Rate, Delay; one for the synth | **LFO Wave** (Triangle, Sine, Square, Ramp Up, Ramp Down, Random S&H, Random Smooth), **LFO Rate** 0.05-40 Hz or a note length with **LFO Sync**, **LFO Delay** 0-5 s, **LFO Trigger** Note or Free | Different: more shapes and Sync, as you asked. Trigger Free is meant to be the 106's single LFO; see [section 6.4](#64-the-lfo) |
| LFO to pitch | DCO LFO amount | **LFO to Pitch** | Same |
| Velocity | None | CE's Velocity macro, which turns Volume, as WAVE and PHASE do | Added by CE's patch system |
| Portamento | On/off and time | **Glide** time, 0 = off | Same |
| Bender | DCO and VCF amounts | CE's pitch bend, ±2 st | The VCF part is left out |
| Unison | All 6 voices, no detune | **Unison** Off, 2-8 voices, with **Detune** and **Width** | Different: as you asked |
| Slop | None. The DCOs were the point: stable tuning | **Vintage** 0-100 % | Added, as you asked |
| Chorus | I, II, I+II | None in the engine | Left out, as you asked. Synth FX's Chorus plugin can go after any synth part, DCO6 included |

## 3. Feasibility answers

### 3.1 Unison inside the plugin API

How CE's host works (`synth/source/instrument/InstrumentImpl.cpp`, open):
- A part has V voice objects. V is the Polyphony on Synth Parameters, and the parts' V add up to
  at most 8.
- `findVoiceForNote` picks **one** voice for each note-on: a free one, else a released one, else the
  oldest. Only that voice gets the note.
- A note-off goes to every voice that holds that note.
- The Velocity macro is set per voice.
- A voice knows its own index (`create_voice(voice_idx)`), but nothing tells it V.

The three ways, weighed:

| Way | What it needs | CPU | Risk | Verdict |
|---|---|---|---|---|
| **A. One host note drives several host voices, from the plugin alone** | Voices of one part find each other through plugin globals, keyed by their patch pointer. When the host gives voice 0 a note, voices 1-3 notice it on their next `isActive` call and start the same note | As unison should be: V voices | High. It leans on details the API doesn't promise: the order the host calls `isActive`, how it steals voices (it would steal a stack's members one by one, since only the leader got a note-on), and the Velocity macro reaching only the leader. A later CE change to the allocator would break it quietly | No |
| **B. One host voice renders N sub-voices** | U oscillator-filter sets inside each voice | U × voices. With 8 host voices and Unison 4 that's 32 voices of work, 4× the budget. Making it fit means Polyphony = notes, which breaks your rule ("8 voices, Unison 4 gives 2 notes"), and CE's 8-voice total stops meaning anything for CPU | Over budget, or the rule broken | No |
| **C. An MT+ hook in CE's open host** | `InstrumentImpl` hands each note to a group of U voices instead of one. Groups are `floor(V / U)` runs of U consecutive voices, and voices left over never get a note. Before each member's note-on, the hook tells it its place (slot and stack size) through a parameter that's in no section | Exactly V voices, as now. The hook's own work is a few hundred instructions per note-on | Low. CE's host is MIT and open, so the change is ours and can be read and tested. The engine works without the hook too: a voice then plays alone, as Unison Off | **Yes** |

**The plan is C.** Your rule holds by construction:
- The stack is `min(Unison, V)`.
- Groups are `floor(V / stack)`.
- 8 voices with Unison 4 give 2 groups, so 2 notes.
- 7 voices with Unison 4 give 1 group. Voices 4-6 are never handed a note, and with Voice Sleep On
  they cost nothing.

Details:
- **Which engines.** The hook acts only for an engine whose metadata names a Unison parameter. The
  plan does it by type id: a table in `core/mods/synths/synthsMod.h` (DCO6: parameters 27 and 33).
  The host code stays engine-agnostic, so a later engine of ours can use it.
- **Allocation.** The same three steps as CE's `findVoiceForNote`, applied to each group's first
  voice: free, else released, else oldest. A whole group is always taken together, so a stack is
  never split.
- **Velocity.** The macro is set for every member, not just the first.
- **Note-off.** Unchanged: every member holds the note, so CE's own loop releases them all.
- **Unison changed while notes sound.** Sounding voices keep their place until their next note.
  The hook re-reads Unison at each note-on.
- **Under `MT_HAS_SYNTHS`, with `// MT:` marks.** `synth/` isn't on the PR branches to CE.

### 3.2 Stereo for Width

**Yes, without changes.**
- A voice renders into a left and a right buffer (`voice_render_audio(..., out_left, out_right)`).
- The host adds the voices into the part's two sides.
- An engine packed with `--output-channels 2` makes the part stereo. `PolySynth::update` then sends
  both sides on to the mixer.

DCO6 pans each stack member with an equal-power law. The first `.psyn` build does this already:
the rig's left and right differ only with Unison above 1.

**One catch in our own code.** Synth FX treats WAVE's and PHASE's two sides as alike (their right
is a copy of the left) through `mods::synths::ownsLayout`, which also makes the patch editor show
their tiles' short names. DCO6 needs the short names but has real stereo. The plan splits that
into two checks: `ownsLayout` for the tiles, and a new `sidesAlike` list for Synth FX holding only
WAVE and PHASE. Otherwise Synth FX would drop DCO6's right side.

### 3.3 CPU

See [section 8](#8-cpu-and-memory). In short: 18,900 instructions per voice per block, about 151,000
at 8 voices. That's 1.27× WAVE's Init in the same rig. A heavy patch (all four sources, Bass Boost and HPF 80 Hz,
Resonance 70 %, Vintage 100 %, the Random Smooth LFO on cutoff) reads 22,700, 1.52× WAVE's Init.

### 3.4 Memory

See [section 8](#8-cpu-and-memory). The engine is all in CE's `.psyn` arena, in SDRAM. The firmware
gets the host hook (ITCM, since `InstrumentImpl`'s code is placed there) and two rows in the Synths
mod's tables (flash).

### 3.5 Parameters and pages

- **35 parameters:** 34 shown, plus Unison Place (in no section, set by the hook). WAVE has 72.
- **5 sections**, so 5 tiles: DCO, Filter, Env, LFO, Voice. CE's patch editor fits 6.
- **The longest list is DCO's**, with 11 rows. The parameter list shows 14 rows before it scrolls.
- **Six default macros:** Cutoff, Resonance, Env Amt, Attack, Decay / Release, Detune. Velocity
  turns Volume.
- **Names stay within 18 characters**, the guide's limit. The longest are "LFO to Cutoff" and
  "LFO to Pitch".
- **Unison, LFO Sync and the lists are `isEnum`.** LFO Sync renames LFO Rate's values (Hz or a note
  length), so it has `invalidatesOtherMetadata`, as WAVE's Sync has.

### 3.6 Patches

- A CE patch (`Patch.proto`) stores the engine's type id and **one value per parameter, by
  position**: parameter n is the nth entry.
- The macros store parameter ids.
- `instrumentTypeVersion` exists in the format, but nothing in the host reads it.

What that means for later changes:
- **Add only at the end.** A new parameter must take the next free id (34, 35, …). A patch saved
  before it has no value there and gets the default.
- **Never reuse or reorder an id.** An old patch would put its value into the wrong control.
- **A changed range or curve** keeps the stored number and reads it with the new meaning. For
  example, turning Detune's 0-50 cents into 0-100 % would turn 12 cents into 1200 %. Change a range
  only by adding a new parameter, and leave the old id in no section, as WAVE did with its old
  effect parameters.
- **The type id stays 66.** The hook, Synth FX and the picker all find DCO6 by it.
- **The DSP's sound** can change between builds without breaking patches, but your saved patches
  would sound different. The plan freezes the sound at the build's first release and treats later
  changes as new parameters.

### 3.7 The picker and the name

- **Name: DCO6** (your choice, 2026-11-17; the plan's first name was EMBER). It says what the
  engine is, a DCO synth, and the 6 echoes the 106's six voices, without Roland's marks. The README and
  user page can say it's "modelled on the Roland Juno-106's design", which is a description, not a
  product name. Names from Juno's mythology (Hera) were turned down: an open-source Juno-60 clone
  already uses "Hera", and with a model number it points straight at Roland's names. The `.psyn`
  header keeps at most 7 characters of the short name; DCO6 fits.
- **In the picker,** CE's Synth Parameters screen lists every `.psyn` on the card. DCO6 comes after
  PHASE as the 10th model (see the mockup). The Synths mod's switch hides it when Off, as it hides
  WAVE and PHASE (`pickerShows`).
- **Files:** `DCO6.psyn` in `/Synths`, patches in `/Patches/DCO6/`, type id 66.

## 4. The voice and its parameters

Ids are the patch positions ([3.6](#36-patches)). CCs follow WAVE's where one exists (cutoff 74,
resonance 71, env amount 47), and use free numbers otherwise. "Macro" says how a macro adds to the
parameter.

**DCO**

| Id | Label | Range | Default | Notes |
|---|---|---|---|---|
| 0 | Range | 16', 8', 4' | 8' | octave −1, 0, +1 |
| 3 | Saw | 0-100 % | 100 % | CC 20 |
| 4 | Pulse | 0-100 % | 0 % | CC 21 |
| 5 | Sub | 0-100 % | 0 % | square, an octave down; CC 22 |
| 6 | Noise | 0-100 % | 0 % | white; CC 23 |
| 7 | Pulse Width | 0-100 % | 0 % | duty 50 % → 5 %; CC 24 |
| 8 | PWM Amt | 0-100 % | 0 % | LFO: ±50 % of the width range; Env: up to +100 %; CC 25 |
| 9 | PWM Source | LFO, Env | LFO | |
| 10 | LFO to Pitch | 0-100 % | 0 % | depth = 12 st × amount² (10 % = 0.12 st, 30 % = 1.1 st); CC 26 |
| 1 | Tune | −12 to +12 st | 0 st | |
| 2 | Fine Tune | −50 to +50 ct | 0 ct | |

**Filter**

| Id | Label | Range | Default | Notes |
|---|---|---|---|---|
| 12 | Cutoff | 20 Hz - 20 kHz, exponential | 2.4 kHz | kAdd for its bipolar macro (WAVE's lesson); CC 74 |
| 13 | Resonance | 0-100 % | 15 % | self-oscillates at 100 %; CC 71 |
| 14 | Env Amt | −100 to +100 % | +35 % | ±8 octaves at full; CC 47 |
| 15 | LFO to Cutoff | 0-100 % | 0 % | ±4 oct × amount²; CC 28 |
| 16 | Key Track | 0-100 % | 50 % | 100 % = one octave per octave, around middle C; CC 29 |
| 11 | HPF | Off, then 20 Hz - 2 kHz, exponential | Off | a one-pole high-pass; Off (the minimum) bypasses it; CC 27 |
| 34 | Bass Boost | Off, On | Off | +6 dB below about 110 Hz; before the HPF, so both can be on |

**Env**

| Id | Label | Range | Default | Notes |
|---|---|---|---|---|
| 17 | Attack | 1 ms - 3 s, exponential | 5 ms | CC 73 |
| 18 | Decay | 1 ms - 12 s | 0.60 s | CC 75 |
| 19 | Sustain | 0-100 % | 60 % | CC 79 |
| 20 | Release | 1 ms - 12 s | 0.35 s | CC 72 |
| 21 | VCA Mode | Env, Gate | Env | |

**LFO**

| Id | Label | Range | Default | Notes |
|---|---|---|---|---|
| 22 | LFO Wave | Triangle, Sine, Square, Ramp Up, Ramp Down, Random S&H, Random Smooth | Triangle | |
| 23 | LFO Rate | 0.05-40 Hz (exponential); with Sync a note length, 1 to 1/32 T | 5.10 Hz | CC 3 |
| 25 | LFO Sync | Off, On | Off | |
| 24 | LFO Delay | 0-5 s | 0 s | the LFO fades in over this time after each note |
| 26 | LFO Trigger | Note, Free | Note | |

**Voice**

| Id | Label | Range | Default | Notes |
|---|---|---|---|---|
| 27 | Unison | Off, 2-8 | Off | [section 5](#5-unison) |
| 28 | Detune | 0-50 ct, power curve | 0 ct | outer voices at ±Detune |
| 29 | Width | 0-100 % | 50 % | |
| 30 | Vintage | 0-100 % | 0 % | [section 6](#6-vintage-slop) |
| 31 | Glide | 0-10 s, power curve | 0 s | 0 = off; CC 5 |
| 32 | Volume | 0-100 % | 100 % | kMult; the Velocity macro turns it |
| 33 | Unison Place | — | — | in no section; set by the host hook |

Bass Boost is id 34 though it shows in the Filter section: ids only ever grow (3.6), and the order
on the screen comes from the sections, not the ids. Nothing is released yet, so the ids could still
be renumbered before the build; the plan keeps them as the prototype has them.

Voice Sleep is CE's own row in the Voice section, added by the host. It's On by default, because
DCO6 is packed with `--supports-idle-sleep 1`.

**The DSP, per sample:**
1. **DCO.** A phase accumulator. Saw and pulse take a 4-sample step correction at each edge, from
   the integrated cubic B-spline (Välimäki, Pekonen and Nam, JASA 2012), so the DCO's output runs
   2 samples late. The sub flips at each wrap and takes the same correction. (The prototype used
   2-sample PolyBLEP, Välimäki and Huovilainen; decision 7 replaced it.) Noise is xorshift32. Each voice's DCO runs free from a random start phase, as the
   106's do.
2. **Bass Boost, then the HPF.** Each is a trapezoidal one-pole. The boost adds its low-pass at
   110 Hz, a +6 dB shelf. The HPF subtracts its low-pass. Their coefficients are worked out once a
   block, and each is skipped when it's off.
3. **Low-pass.** Four trapezoidal one-poles with the global feedback solved in the same sample
   (Zavalishin's topology-preserving transform). The ladder's input goes through a tanh-like
   rational soft clip, which bounds self-oscillation. The output is made up by 1 + 0.25 k for part
   of the passband's loss at high resonance.
4. **VCA and pan.** The envelope's level (or the gate's), then the equal-power pan.

Pitch, cutoff, pulse width and the LFO update every 8 samples. The envelope, the oscillator and the
filter run every sample. Gain is set so that one Init note plays about −20 dBFS, like PHASE.

## 5. Unison

- **Unison** sets how many voices each note takes: Off (1) to 8. The stack is `min(Unison, the
  part's Polyphony)`, so it never takes more voices than the part has. Notes at once is
  `floor(Polyphony / stack)`, and the rest of the voices stay unused (and asleep):

  | Polyphony | Unison | Notes at once | Unused |
  |---|---|---|---|
  | 8 | Off | 8 | 0 |
  | 8 | 4 | 2 | 0 |
  | 7 | 4 | 1 | 3 |
  | 4 | 8 | 1 (a stack of 4) | 0 |
  | 3 | 2 | 1 | 1 |

  The Unison row shows the setting, not the result: the engine can't see the part's Polyphony when
  it formats a value. Decided: the user page explains it (section 12).
- **A new note when all groups are busy** takes the group that's free, else released, else the
  oldest, as CE does for single voices.
- **Detune** spreads the stack's pitches evenly from −Detune to +Detune. 0 is no detune, as the
  106's unison. **The maximum is 50 cents**, so the outer voices are at most a semitone apart:
  - Up to about 7 cents (the Take 5's limit) it's thickening. 10-25 cents is the classic
    supersaw/hoover range. At 50 the stack is wide and beating fast, which is the DeepMind 12's
    limit.
  - Past a semitone of spread the stack stops sounding like one pitch and becomes a cluster. That's
    a sound effect rather than a voice setting; the Radias's 99 is there for it.
  - The power curve gives the bottom of the knob the most room: half way is about 9 cents.
- **Width** spreads the stack across the stereo field: 0 % all in the middle, 100 % from hard left to
  hard right. The default is 50 %. Pans are evenly spaced. Detunes are dealt out so that neighbours
  in the field are far apart in pitch, so the beating is spread across the image rather than sitting
  left-low, right-high.
- **Level** is 1/√stack per voice, so a stack is about as loud as one voice (a detuned stack sums
  like noise). The examples measure the same loudness within 0.2 dB from 1 to 8 voices.
- **Vintage** applies to each member on its own, so even at Detune 0 a Vintage stack isn't
  phase-locked.

### 5.1 The host hook

Built in step 4 (2026-11-17), in CE's open host, `synth/source/instrument/InstrumentImpl.cpp`:
- `InstrumentImpl::handleNoteOn` first offers the note to `mtUnisonNoteOn`. That asks
  `polysynth::mtUnisonLookup` (`synth/include/PolySynth/mtUnison.h`) whether the part's engine has
  a Unison parameter. The pointer is null in CE's builds; the Synths mod sets it in `Synth::init`
  (the main loop) to `mods::synths::unisonParams`, which names DCO6's Unison (27) and Unison Place
  (33). For any other engine it returns false and CE's own allocation runs, unchanged.
- The synth library is built once for the product, without core's mod switches, so the hook is a
  pointer rather than an `#if MT_HAS_SYNTHS`. It is compiled out under `MT_NO_MODS`: `synth/makefile`
  now passes `CUSTOM_DEFINES` on, and the `MT_NO_MODS` image is byte for byte the last commit's.
- The stack is `clamp(round(Unison), 1, voices)`; the part's voices split into `voices / stack`
  groups of neighbours. A group is picked as CE picks a voice: one with no voice sounding; else
  none holding a note and the same last note; else none holding, the oldest; else the oldest.
- Each member gets, in order, its place (`16 × stack + slot`, straight to the voice, not the patch),
  the Velocity macro, the note-on, and CE's bookkeeping (`noteSourceIds`, `ticksSinceNoteOn`).
  Note-offs need nothing new: each member holds the note's source id, so CE's own note-off reaches
  all of them.
- Our engines take the hook even at Unison Off, so a stale place can't stay on a voice.
- In the interrupt, it allocates nothing and calls nothing that blocks: a few loops over at most 8
  voices. Level B checks no malloc in a note-on.
- The plugin reads the place at note-on only. Without the hook (a place under 16) every voice
  plays alone, as before.

CE's macro destination lists show every parameter, Unison Place included; the user page says to
leave it unassigned. A macro on it changes nothing a note-on doesn't set again, except the Velocity
macro, which the hook applies after the place (reasoned, not tested).

## 6. Vintage (slop)

One control, 0-100 %, scales everything below linearly. The 100 % figures are this plan's
choices. They're meant to sound like a well-kept analog polysynth at 100 %, not a broken one
(decision 4).

### 6.1 Per voice, fixed (like part tolerances)

Drawn once from the voice's index, so a voice keeps its character:

| What | At 100 % |
|---|---|
| Pitch | ±4 ct |
| Cutoff | ±0.15 octave (about ±11 %) |
| Attack, Decay and Release times | ±8 % |
| Pulse width | ±3 % of its range |
| Level | ±0.4 dB |

### 6.2 Over time (drift)

Per voice, a slow random walk (a leaky integrator of noise: about a 0.2 Hz corner, standard
deviation 0.5) on two things:
- pitch, ±3 ct per unit, so typically ±1.5 ct and peaks about ±4;
- cutoff, ±0.08 octave per unit.

In the model at 100 %, each of the 8 voices wandered within about −7 to +6 cents, 4 to 6 cents of
movement over 4 seconds.

### 6.3 Per note

At each note-on: pitch ±1.5 ct and cutoff ±0.05 octave, drawn fresh.

### 6.4 The LFO

Not touched by Vintage. With **LFO Trigger** Free, the part's voices share one phase, as the 106's
single LFO does. Built in step 1 (2026-11-17), with no host change:
- The plugin keeps one table of 8 slots, shared by all its voices. A slot is keyed by the LFO's
  rate (in Hz, after Sync) and its shape.
- At the end of each block, every sounding voice in Free mode writes its LFO phase and cycle count
  to its slot. A voice starting a note in Free mode takes the phase and cycle from its slot, so it
  joins the LFO already running. Note-ons happen between blocks, so it never reads half a write.
- The voices don't know their part (the host passes only the tempo and the bend range), so two
  DCO6 parts with the same rate and shape share one LFO. That's rare and harmless: they'd sound
  alike anyway.
- When no voice sounds, the slot isn't written and the LFO stands still. The next note picks it up
  where it stopped, not where a free-running LFO would be. The 106's LFO keeps running in silence,
  but no one can hear the difference on the first note.
- The random shapes (S&H and Smooth) take their values from a hash of a seed and the cycle count
  (Chris Wellons's "lowbias32" integer hash), not from a running generator. So voices sharing a
  cycle get the same random value. Free uses one fixed seed; Note mode draws a new seed per note.
- With LFO Trigger Note, the phase starts at 0 with each note, as before.

### 6.5 What Vintage doesn't touch

- Oscillator start phase: always random, at every Vintage.
- Resonance.
- The HPF.
- The envelope's sustain level.

## 7. Prototype results

### 7.1 The Python model

`tools/synths/dco6_proto.py`. It's now the reference Level A checks the C engine against
([7.3](#73-level-a-the-c-engine-against-the-model)), so it mirrors the engine's formulas, its
4-sample step correction and its fast `exp2` included. It writes 35 WAVs to
`build/dco6_wav/` in the worktree, with `index.txt` there listing each file, its level and what it shows:
- the bass;
- the PWM pad;
- a filter sweep;
- self-oscillation;
- the HPF Off, at 240 Hz and at 720 Hz, Bass Boost alone, Bass Boost with HPF 60 Hz, and a sweep
  of the HPF from 20 Hz to 2 kHz and back with Bass Boost on (`05_hpf_*.wav`);
- Unison 1, 2, 4 and 8;
- Detune 0, 7, 25 and 50 at Width 100 %;
- 8 voices at Unison 4 with a stolen stack, and 7 voices at Unison 4;
- Vintage 0, 50 and 100 %;
- each LFO shape, and the LFO delay;
- VCA Gate;
- negative Env Amt;
- LFO Trigger Note and Free, three notes started apart (`15_lfo_note.wav`, `15_lfo_free.wav`).

Checked in the model (float64, with the engine's own formulas):
- **Pitch** is within 0.01 cent, notes 36-108, for saw, pulse and sub.
- **Aliasing**, filter open, by the worst non-harmonic component below 16 kHz relative to the
  fundamental. The prototype's 2-sample PolyBLEP gave −38 dB at note 96 and −32 dB at 108.
  Decision 7's 4-sample correction replaced it; the figures for it are Level A's, through the C
  engine ([7.3](#73-level-a-the-c-engine-against-the-model)).
- **The filter** falls 24 dB per octave (−26.5 dB at 2× cutoff, −48 at 4×, Resonance 0). At
  Resonance 100 % it self-oscillates at the cutoff, within 11 cents from 110 Hz to 7 kHz. It doesn't
  at 95 %.
- **Unison** keeps its level within 0.2 dB from 1 to 8 voices, and adds stereo: side-to-mid −5.9 dB
  at Unison 2 and Width 50 %, about −1 dB at Width 100 %.

### 7.2 The `.psyn` build

- Built from `core/mods/synths/d6Engine.h` and `psyn/dco6Synth.cpp`, packed as `DCO6.psyn` (code
  name, display name and patch folder all `DCO6`, type id 66, idle sleep on). The step 1 build:
  43,800 B code, 1,704 B rodata, 296 B bss, 45,912 B in the arena. The plugin's voice is 368 B, of
  which the engine's state is 132 B. Built by hand in WSL so far; `psyn/Makefile` gets its target
  in step 3.
- Run in `tools/emu/sound_rig.py` (CE's start-up, `Synth`, the audio graph and its interrupt entry),
  with CE's factory engines, WAVE and PHASE on the fake card. Prototype script:
  `tools/emu/measure_dco6.py`.
- It runs, sounds and stops in the rig. No asserts, no rig failures.
- The first build's output in the rig followed the Python model: correlation 0.9995 against CE's
  mixed and limited output, at a 389-sample latency (3 blocks). Not repeated for the step 1 build:
  Level A now checks the engine against the model directly.
- WAVs from the rig: `build/dco6_rig_wav/`.

**Not built yet** (the firmware steps, 3 on):
- the unison host hook (the rig takes each voice's place from `voice index % Unison`, which the
  test fills by sending each note Unison times);
- the tile labels, the picker filter and Synth FX's stereo check (no firmware change at all);
- patch save and load, and the Init patch's level through the host;
- Level B and the rest of [section 11](#11-test-plan) beyond Level A.

### 7.3 Level A: the C engine against the model

Build step 2, run 2026-11-17. The engine (`d6Engine.h`) is built for Arm with the firmware's FPU
flags behind plain C entry points (`tools/synths/dco6_level_a/`, using WAVE's Level A link script
and support code), and run in the emulated Cortex-M7. `tools/emu/test_dco6_dsp.py` drives it:

```
python tools/emu/test_dco6_dsp.py build/emu/dco6_level_a.axf          # 61 checks
python tools/emu/test_dco6_dsp.py build/emu/dco6_level_a.axf --quick  # the planted-fault subset
```

**Result: 61 of 61 checks passed** (`build/dco6_level_a.txt`).

- **Port against the model**, 26 cases (every source, Range, Tune, PWM from LFO and envelope, the
  HPF and Bass Boost, resonance, Env Amt, Key Track, VCA Gate, LFO Delay, Vintage 100 %, a unison
  slot, glide, each LFO shape, LFO Free).

  "Sample for sample within float tolerance" was too strict as the plan wrote it. The C engine runs
  in float32 and the model in float64, so the oscillator's phase drifts apart by a fraction of a
  sample over a second, and a waveform edge that lands one sample later shows as a large
  difference. So each case checks three things:
  - the first 4 blocks within −60 dB of the peak (before any drift; they measured −62 to −95 dB);
  - every block within −30 dB (measured −39 to −88 dB);
  - each third-octave band's level within 0.5 dB (measured 0.00-0.02 dB).

  That catches a wrong formula, which changes the spectrum at once, and allows the drift, which
  doesn't. To get there the model mirrors the engine's polynomial `exp2` instead of the exact one:
  its error alone put the two 30 dB apart within a second.
- **Pitch:** within 0.44 cent, notes 24-108, Range, Tune and Fine.
- **Aliasing**, filter open, worst non-harmonic component below 16 kHz against the fundamental:

  | Note | Saw, 2-sample (prototype) | Saw, 4-sample (kept) | 30 % pulse, 4-sample | Bound |
  |---|---|---|---|---|
  | 96 | −38 dB | −56.4 dB | −54.6 dB | −54 dB |
  | 108 | −32 dB | −50.8 dB | −49.0 dB | −48 dB |

  The 4-sample correction (the integrated cubic B-spline step, from Välimäki, Pekonen and Nam,
  "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial
  interpolation", JASA 2012) is about 18 dB better and costs less than the 2-sample one did. It
  delays the DCO by 2 samples. It's kept (decision 7). It still misses WAVE's −60 dB by 4-11 dB
  at the very top (risk 3).
- **Filters:**
  - the ladder at Resonance 0 follows four one-poles within 0.03 dB, 200 Hz-8 kHz, and falls
    25.3 dB from 4 to 8 kHz;
  - Resonance 100 % self-oscillates at the cutoff (1000.0 Hz), 90 % doesn't;
  - everything at full with both filters swept stays finite, and a NaN parameter recovers;
  - the HPF is −3.01 dB at its corner (240 Hz and 1 kHz); its lowest setting above Off changes
    nothing above 200 Hz (0.045 dB);
  - Bass Boost is +5.83 dB at 30 Hz and 0.02 dB at 3 kHz.
- **Envelope:** Attack 0.1 s and 1 s peak at 0.102 s and 0.998 s; Decay 0.2 s and 1 s, and Release
  0.5 s, reach 99 % within 1.5 %; the voice stops after its release, and in VCA Gate mode after
  the gate's 2 ms ramp, whatever the Release.
- **LFO Trigger:** three voices started apart share one phase and cycle in Free, and start their
  own in Note.
- **Unison** 2, 4 and 8: the stack's level within 0.12 dB of one voice's; Width 0 centred, Width
  100 % spread; Unison Off alike in L and R.
- **Vintage 100 %:** the drift over 10 minutes stays within ±2.1 of its ±3 units.
- **Cost** of one voice in the harness: 19,568 instructions a block on Init, 22,837 on the heavy
  patch, 528 idle (mostly the harness clearing its buffers). These are regression numbers with
  ceilings (21,000, 25,500 and 700), not the rig's figures in 8.1.

**Engine bugs Level A found and step 1 fixed:**
- **Attack** reached its peak in about a third of the Attack time: it used the decay's
  99 %-in-the-time coefficient, and the attack aims above 1.0 and stops there. It now has its own
  (time constant ln(1.3/0.3) for the target 1.3), and the model was fixed to match.
- **VCA Gate:** a released voice kept running until the envelope died, though the gate had
  already silenced it. It now stops when the gate closes.
- **Long times:** the fast `exp2` is off by about 2 parts in a million, which is fine for pitch but
  not for `1 − exp2(−x)` with a tiny x: Attack 1 s peaked at 0.80 s, Decay 1 s took 0.93 s. The
  engine now uses a short series for small x (`d6_one_minus_exp2`).

**Planted faults** (`tools/synths/dco6_level_a/plant.py`): each puts one mistake into a copy of
`d6Engine.h`, builds Level A from it and runs the quick checks, which must fail on the check named.

Run 2026-11-17 (`build/dco6_plant.txt`): **8 planted, 0 missed.**

| Fault | Caught by |
|---|---|
| A sign flipped in the 4-sample correction | aliasing, and 17 port cases |
| The sub's step left out | port: saw + sub |
| The ladder's feedback through a unit delay | 14 port cases |
| Pan sides swapped | port: the unison slot |
| The attack's old 99 % coefficient | Attack, and 15 port cases |
| Free: a new voice doesn't take the phase | LFO Trigger Free, and port: LFO Free |
| VCA Gate waits for the envelope | VCA Gate |
| The HPF's corner an octave high | HPF 240 and 1000, and port: HPF 240 Hz |

## 8. CPU and memory

### 8.1 CPU

Instructions inside the engine's own `.psyn` image per 128-sample block, after the attack: the
voice's render, the SDK bridge and `isActive`. Counted with a code hook over the image's range in
`measure_dco6.py`, one part with all 8 voices. CE's host (InstrumentImpl, mixing) isn't counted.
The firmware image is the Master FX branch's build of 2026-11-16 (`3cd0d3d`), whose synth code is
the same as `modular`'s HEAD.

| Case | Voices sounding | Per block | Per voice |
|---|---|---|---|
| WAVE Init, 8 notes | 8 | 119,008 | 14,876 |
| PHASE Init, 8 notes | 8 | 112,458 | 14,057 |
| DCO6 Init, 8 notes | 8 | 151,322 | 18,915 |
| DCO6 heavy, 8 notes | 8 | 181,398 | 22,675 |
| DCO6 Unison 2, 4 notes | 8 | 151,547 | 18,943 |
| DCO6 Unison 4, 2 notes | 8 | 151,500 | 18,938 |
| DCO6 Unison 8, 1 note | 8 | 151,474 | 18,934 |
| DCO6 heavy, Unison 8 | 8 | 181,730 | 22,716 |

DCO6's rows are the step 1 build (re-measured 2026-11-17, `build/dco6_measure.txt`), with the
4-sample correction, LFO Free and the envelope fixes. The 2-sample build before them read 19,661
on Init and 23,016 heavy, so the step 1 changes made it about 4 % cheaper. WAVE and PHASE read the
same as before.

"Heavy" means: all four sources on, Bass Boost On with HPF 80 Hz, Resonance 70 %, PWM from the LFO, Vintage 100 %,
the Random Smooth LFO on pitch and cutoff.

**The HPF and Bass Boost** (measured 2026-11-17 on the 2-sample build, after the change to a continuous HPF):
- Both off (Init): 19,661 per voice. The 4-position HPF's build read 19,942, so off costs nothing.
- The heavy patch had only the boost before (20,983). With the boost and the HPF 80 Hz both on it
  reads 23,016: about 2,000 more per voice, 10 %. That's more than one one-pole's arithmetic
  (about 5 instructions a sample, 640 a block), so part of it is how the compiler laid out the
  loop. Each filter's own share wasn't measured separately.
- A first build that checked the two filters separately in every sample cost 20,861 on Init, so
  the engine checks one flag first and skips both when both are off.

**CE's engines** can't be counted this way: their licence rules out looking inside them. The only
figures for them are end to end, on the unit, from the Profiler (`MASTERFX_PORT_PLAN.md`, the
2026-11-05 run of each engine at 8 voices on Init):

| Engine at 8 voices | Average | Slowest |
|---|---|---|
| ACD | 54-58 % | 75 % |
| FAT | 54-56 % | 73 % |
| VAP | 58 % | 75 % |
| WTFM | 48-51 % | 67 % |
| DIRT | 63-66 % | 83 % |
| WAVE (S5, `CE_PORT_PLAN.md`) | 35 % | 44 % (50 % in another run) |

**Measured on the unit since** (step 7): 8 voices on Init average 27-29 %, slowest 40-48 %;
the heavy patch 31-32 % and 44-46 %.

**Estimate, before it was measured:** DCO6 at 8 voices sounding is about 32,000 instructions a block more
than WAVE on Init, and 62,000 more on the heavy patch. At the old device ratio of about 3,800
instructions per point, that's roughly +8.5 points on Init (an average near 43-48 % and a slowest
near 52-58 % on the S5 kind of project) and +16 on the heavy patch (near 51-56 % and 60-66 %). That's under VAP and
ACD. The unit is stall-bound, so only the Profiler can say. The device ladder measures it at 1, 4
and 8 voices.

**Cost of the hook (estimate):** a few hundred instructions per note-on, none per block.

### 8.2 Memory

Floors from `tools/mem_budget.py` on the same image:

| Region | Free before | Floor | DCO6 engine | Host hook (estimate) |
|---|---|---|---|---|
| ITCM | 39,808 | 1,024 | 0 | +300 to 600 B: `InstrumentImpl`'s code is placed in ITCM by the linker script |
| DTCM | 4,136 | 4,096 | 0 | 0: no static state; the hook keeps nothing between notes but what `InstrumentImpl` (on the heap) already has |

**Measured on step 5's build** (`mem_budget.py` on this branch's mods build): ITCM 38,560 free
(`mtUnisonNoteOn` is 1,124 B, in ITCM), DTCM 4,128 (8 B: the hook's pointer and its alignment),
OCRAM 9,556, SDRAM 610,824 (unchanged), flash 14.96 MB. All above their floors. `unisonParams` is
26 B of flash.
| SDRAM (mods' 1 MiB) | 610,824 | 262,144 | 0 | 0 |
| Flash | 14.96 MB | 1 MiB | 0 | under 1 KB (tables, hook) |
| `.psyn` arena (SDRAM, CE's) | 1,920 KB shared by loaded engines | — | 45,912 B image + 368 B per voice (2,944 B at 8) | — |
| CE heap | — | — | the host's wrapper per voice, the same for every `.psyn` engine | none |

For comparison, WAVE's image is 523,088 B (its wavetables) and PHASE's 49,048 B.

## 9. Risks

1. **The host hook touches CE's allocator, which runs in the audio interrupt** (`midiUpdate` →
   note-on). It must allocate nothing, block on nothing, and leave CE's path unchanged for every
   other engine. A mistake there hits every synth. **Guard:** the hook runs only when the patch's
   type id is in our table, and the Off = CE check (`test_sound_rig.py`) compares the stock engines
   sample for sample.
2. **CE may change `InstrumentImpl`.** The next CE merge's checklist (CLAUDE.md, steps 2-3) must look
   at the hook. CE has changed this file before (idle sleep).
3. **Aliasing at the top of the keyboard**, with the filter open. The 4-sample correction (decision
   7) brought it to −56 dB at note 96 and −51 dB at 108 ([7.3](#73-level-a-the-c-engine-against-the-model)),
   still short of WAVE's −60 dB there. Only the top octave and a half is affected, and the filter
   usually takes it out. Going further means 2× oversampling (about +80 % CPU), which isn't planned.
4. **The cost is 27 % above WAVE on Init, 52 % on the heavy patch.** Within what the unit has run (VAP and DIRT cost more on the
   unit), but not yet measured there.
5. **Unison changed mid-note.** Sounding stacks keep their old place until their next note.
   Harmless; Level B's `changed` checks it.
6. **Synth FX's stereo check** ([3.2](#32-stereo-for-width)). If it's missed, DCO6's right side
   is lost under Synth FX, silently. `sidesAlike` is WAVE and PHASE only, and `test_synthfx.py`'s
   `wave_off_and_mono` checks DCO6's slots get two channels.
7. **Patch stability** ([3.6](#36-patches)). Once released, ids and ranges are frozen.
8. **The rig's image was from the Master FX branch** for steps 1-2. Step 5 re-measured on a fresh
   build of this branch: 18,916 own-code instructions per voice on Init against 18,915.
9. **ITCM and DTCM.** The hook took 1,124 B of ITCM (the estimate was 300-600 B: the linker puts
   all of `InstrumentImpl` there) and 8 B of DTCM (the pointer). DTCM is now 32 B above its floor
   (4,128 free, floor 4,096); the next mod that needs DTCM has to find it.

## 10. Build steps

Each step ends with its tests passing, and nothing is committed without your OK.

1. **The engine, finished.** Done 2026-11-17, not committed.
   - LFO Trigger Free's shared phase ([6.4](#64-the-lfo)).
   - The aliasing correction (decision 7): measured better and kept.
   - Your answers on defaults and ranges, and the name DCO6.
   - Three bugs Level A found: Attack time, VCA Gate's stop, long envelope times
     ([7.3](#73-level-a-the-c-engine-against-the-model)).
   - The Init level checked through the host: moved to step 5 (Level B), since it needs the
     Synths mod's macros.
2. **Level A**, as `test_synths_dsp.py` does for WAVE: DCO6's voice on Arm in the emulator against
   the Python model, plus the checks in [section 11](#11-test-plan). Done 2026-11-17, with the
   planted faults ([7.3](#73-level-a-the-c-engine-against-the-model)). Not yet in
   `tools/run_tests.py`'s tiers: step 3 adds it with the Synths mod.
3. **The Synths mod.** Done 2026-11-17, not committed.
   - The type id 66 in `synthsMod.h` and `pickerShows`; DCO6 appended to `kSynthTypes`, so WAVE
     and PHASE keep their places (7 and 8) in saved projects and DCO6 is 9.
   - `ownsLayout` (tiles) split from a new `sidesAlike` (Synth FX's mono, and its conversion of
     old WAVE and PHASE effects, which must never touch DCO6's parameters 62-71).
   - `psyn/Makefile`'s DCO6 target with idle sleep; `make psyn` stages it into `build/Synths`,
     which CI zips for the release.
   - Config's two Synths descriptions name DCO6.
   - The host tests (`core/mods/tests`): 14 suites, 0 failures; `test_registry` checks the
     picker, `ownsLayout`, `sidesAlike` and `unisonParams`.
   - Level A and Level B are jobs in `tools/run_tests.py`'s quick tier (`dco6 level a`, `dco6`).
4. **The unison hook** ([5.1](#51-the-host-hook)). Done 2026-11-17, not committed.
5. **Level B**, `tools/emu/test_dco6.py`. Done 2026-11-17, not committed: 16 checks, 0 failures.
   With the hook's pointer cleared, 7 of its 8 unison checks fail, so they test the hook.
6. **The user page** `docs/mods/synths.md`: a DCO6 section with every control, its label, range
   and default, and the cost. The mockups were redrawn from the final strings, with CE's Voice
   Sleep row the host adds; `check_mockups.py`: 54 images, 0 failures. The README's Synths row,
   install step and build note, and Synth FX's intro. Done 2026-11-17, not committed.
7. **Release tier** (`tools/run_tests.py --tier release`), then the device ladder with the projects
   below, then the merge checklist into `modular` and a `synths-1.x` tag.
   - Release tier, 2026-11-17, on this branch's mods build, its `MT_NO_MODS` build as the reference,
     the recorder build as `--diag`, and the usual source project: 55 of 55 passed, 0 failed. Its inputs were
     built from this branch (the Synth FX rig, both packs, whose `--check` passed, and both guard
     cards); the two older firmwares it compares with (`p1`, `d9a6795`) and WAVE and PHASE's Level A
     come from the last release run.
   - The device image: the recorder build (`MT_LOCKUP_RECORDER`). `mem_budget`: DTCM 2,608 free,
     under the 4,096 floor, as every recorder build has been (the recorder takes 1,512 B); the
     normal build has 4,128. Level B on it: 16 of 16.
   - The DCO6 set, E1 to E5 (`DEVICE_TEST_LADDER.md`), by `tools/emu/make_dco6_test_projects.py`.
     Each opened in the boot rig with the recorder build and played 200 blocks, no failures. E4's
     and E5's patches opened with their values only once they were put in the Workspace by hand:
     CE copies a project's patches with a `\` in the path, which FatFs on the unit takes as a
     separator (`ff.c`, `IsSeparator`) but the rigs' fake card didn't, so no rig had ever opened
     a project's own synth patches. The boot rig with E3: deepest stack 7,808 B, as before.
   - Fixed in the rigs: the fake card now finds a path as FatFs does (`test_step0.card_path`: `/` and
     `\` both separators, runs of them as one, no leading one needed). E4 then opens with its own
     patch on all three parts, through CE's open. `tools/emu/test_project_patches.py` (release job
     "project patches") checks the open and the save's copy; with the old exact matching it fails 3
     of 4. The boot tests' "held", "worst" and "tracklen" projects keep the source project's
     patches, which now reach the Workspace on open, where before every part was on Init. Looked at
     for "held": parts 1 and 3 open the source's files byte for byte; part 2's file is for another
     engine than project.mt sets, and CE's open replaces it with that engine's Init (its own code,
     whatever the card). The release tier with the fix: 52 run, 0 failed, 4 cached from the run
     before (jobs that don't use the fake card).
   - On the unit, 2026-11-17, the recorder build, E1 to E5 in order, each played two to five times
     (18 plays, 0 to 38 s each). No lockup or fault, 0 blocks over time in any play, the synth
     never over its limit. Free heap 68,740 B at the least, max stack 8,053 B (of 20,480), longest
     main loop gap 26 ms. E4 sounds wide, and changing Width while it plays is heard at once.

     | Project | Average | Slowest |
     |---|---|---|
     | E1, 1 voice, Init | 13 % | 17-24 % |
     | E2, 4 voices, Init | 18-19 % | 23-28 % |
     | E3, 8 voices, Init | 27-29 % | 40-48 % |
     | E4, 8 voices, Unison 4 | 24-25 % | 37-40 % |
     | E5, 8 voices, heavy patch | 31-32 % | 44-46 % |

     The last play, 43 blocks at 2 % (most likely stopped before the notes sounded), is left out of E5's row.
     Under the estimate in 8.1, and near WAVE's 8 voices (S5: 35 %, 44 %), on a different project.
     Not measured: the Profiler's figure with all 8 voices, and DCO6 with Synth FX or Master FX on.

## 11. Test plan

From `PAST_FAILURES.md` and `SOUND_TEST_PLAN.md` ("our engines"). Each line becomes a named check.

**Level A** (the voice alone, on Arm in the emulator, against the Python model). Built and passing
since 2026-11-17; [7.3](#73-level-a-the-c-engine-against-the-model) has the results and the
tolerance the port check uses.
- **Pitch:** within 1 cent, notes 24-108, Range 16' 8' 4', Tune and Fine.
- **Aliasing:** saw and 30 % pulse, −54 dB at note 96 and −48 dB at 108.
- **Filter:**
  - slope (24 dB/oct);
  - cutoff within 5 %;
  - self-oscillation at 100 % and none at 90 %;
  - no NaN or runaway at Resonance 100 % with every source at 100 % and cutoff 20 Hz-20 kHz swept;
  - the NaN reset.
- **HPF:** the corner within 5 % from 30 Hz to 2 kHz; Off is an exact bypass; Bass Boost is +6 dB
  (±0.5) at 30 Hz and flat above 1 kHz; both on together; the HPF swept while a note plays makes no
  clicks or NaN.
- **Envelope:** Attack peaks at its time, Decay and Release reach 99 % in theirs, within 5 %, at
  short and long times; the Gate ramp; a voice stops (`isActive` false) once the release ends, and
  in VCA Gate mode once the gate closes.
- **LFO:** each shape's form, the Sync lengths at 120 BPM, the delay's fade, Free's shared phase.
- **Unison:** for each stack size 1-8:
  - each slot's detune and pan;
  - level within 1 dB of one voice;
  - the stereo image: L−R zero at Unison Off, rising with Width.
- **Vintage:** 0 % is exactly the clean voice. 100 % stays within the plan's bounds over 10 minutes
  of drift.
- **Planted faults** (`tools/synths/dco6_level_a/plant.py`): flip a sign in the step correction,
  lose the sub's step, put the ladder's feedback through a unit delay, swap pan sides, the old
  attack coefficient, Free not taking the phase, VCA Gate waiting for the envelope, the HPF an
  octave high. Each must fail its check.

**Level B** (through CE's host, `sound_rig.py`):
- **The Init values** each voice renders with, macros as the picker leaves them. Cutoff's bipolar
  macro must leave 2.4 kHz: WAVE's kMult lesson.
- **Notes:** a note sounds and stops; the same twice; more notes than voices; chords at 8 voices
  and 3 + 3 + 2.
- **Unison and polyphony:** the table in section 5, checked by which voices render:
  - V 8 / U 4 gives 2 notes, and a third note steals the oldest whole stack;
  - V 7 / U 4: voices 4-6 never render;
  - V 3 / U 8: a stack of 3.
- **Unison changed while notes are held.**
- **Velocity** reaches every stack member.
- **No malloc and no `createMetadata` in the interrupt** (the SDK rules check), including the hook's
  note-ons.
- **Off = CE:** with the hook in, every stock engine sample for sample against the reference build.
  The hook must change nothing for them.
- **Synth FX on an DCO6 part keeps both sides:** Chorus on, Unison 4, Width 100 %.
- **Engine switches:** 30 rounds of DCO6, WAVE, CHIP, and the heap steady.
- **Patch:** a changed value saved and loaded back. An old patch without a later parameter loads
  its default (a hand-made file).
- **Own-code instructions** as a regression number (18,900 ± 10 % per voice on Init), never as a budget.
- **Stress:** 2,000 blocks of random notes, parameters (NaN included), Unison and Polyphony changes.

**Boot rig** (`boot_rig.py`): the device projects open with DCO6 parts, Play, the heap and the pool
at 8 voices.

**Device ladder:** a new "DCO6 set" in `DEVICE_TEST_LADDER.md`, made by
`make_masterfx_test_project.py --dco6`. It runs on the recorder build, numbers from
`/mfx_log.txt`, lightest first, and is a few minutes in all:
- E1: 1 voice Init.
- E2: 4 voices.
- E3: 8 voices Init, chords.
- E4: 8 voices, Unison 4 + Detune 25 + Width 100 %, 2-note chords.
- E5: 8 voices of the heavy patch.
- By ear on E1: Vintage 0 against 100, the HPF swept and Bass Boost on and off, self-oscillation.
- The Profiler reading at E3 and E5 decides the CPU question.

**What only the unit shows:**
- CPU and drop-outs at 8 voices;
- how it sounds;
- chords at 8 voices: the 1.2.0 screech only ever showed on the unit.

## 12. Decisions

You approved the plan on 2026-11-17 ("everything looks good"), so where you didn't choose
otherwise, the plan's recommendation stands:

1. **Unison:** the hook in CE's host (way C), under the Synths mod.
2. **Name:** DCO6, type id 66 (your choice).
3. **Vintage's default:** 0 %.
4. **Vintage at 100 %:** as in [section 6](#6-vintage-slop) (about ±7 cents between voices, a few
   cents of drift, ±11 % cutoff spread).
5. **Detune:** up to 50 cents, power curve, outer voices at ±Detune.
6. **The Unison row** shows the setting, not the stack; the user page explains it. No change to CE's
   Polyphony screen.
7. **Aliasing:** try a 4-sample step correction, measured before it's kept (option b). The other
   options were keeping 2-sample PolyBLEP (a) or running the DCO and filter at 2× (c, about +80 %
   CPU).
8. **Saw and Pulse** are levels, not switches.
9. **HPF:** continuous, with a separate Bass Boost switch (your choice).
10. **PWM Source** has Env; Pulse Width and PWM Amt are two controls.

## 13. What was measured, estimated and not checked

**Measured:**
- In the Python model: pitch, the filter's slope and self-oscillation, unison level and stereo,
  and Vintage's spread.
- In Level A, the C engine on Arm in the emulator: the port against the model, pitch, aliasing,
  both filters and Bass Boost, the envelope's times and stops, LFO Free and Note, unison level and
  width, Vintage's drift, and the voice's own cost. The planted faults show those checks catch
  8 kinds of mistake.
- In the emulator, through CE's host, on the 2026-11-16 Master FX image: DCO6's, WAVE's and
  PHASE's own instructions at 8 voices sounding, Unison 1, 2, 4 and 8, re-measured on the step 1
  build. Also the first build's output against the model (correlation 0.9995).
- DCO6's image and voice sizes, from the build.
- The firmware's memory, by `mem_budget.py` on step 5's build ([8.2](#82-memory)).
- Step 5, on fresh builds of this branch (mods and `MT_NO_MODS`), after Synth FX 1.1's merge:
  - Level B (`test_dco6.py`): 16 checks, 0 failures: Init values through the host, notes,
    the polyphony table, Unison changed while held, Velocity on every member, stereo, the SDK's
    rules in note-ons and the interrupt, 30 engine switches, the switch Off, patch save and load,
    own-code cost, 1,500 blocks of stress. With the hook off, 7 of its 8 unison checks fail.
  - `test_synths.py`: 36 checks, 0 failures; `test_sound_rig.py` (CE's engines, mods build
    against `MT_NO_MODS`, sample for sample): 0 failures.
  - `test_synthfx.py`, with DCO6 in `wave_off_and_mono` (its slots get two channels): 146 of 147,
    the one failure a missing input (the guard plugins' card); `--only guards` with the main
    checkout's card (built 2026-11-12, before the last change to `tools/synthfx/device` on
    2026-11-14): 7 of 7.
  - The `MT_NO_MODS` image against the last commit's (297c342's tree with c2df9d4's merge): all
    6,771 loaded sections byte for byte.
  - `check_mockups.py`: 54 images, 0 failures.
  - The host tests: 14 suites, 0 failures.

**Estimated:**
- DCO6's Profiler reading on the unit (section 8.1).
- The hook's note-on cost in instructions (a few loops over at most 8 voices; not counted).
- The aliasing options' cost (decision 7).

**Not checked:**
- **Device:** anything on the unit.
- **Not run:**
  - the release tier, and the quick tier as a whole (this worktree lacks the Master FX packs, the
    Synth FX rig and the older builds those jobs compare with); the jobs above were run on their own;
  - the boot rig with DCO6 projects, and the device projects themselves (not made yet);
  - Level A since step 2 (the engine, `d6Engine.h`, hasn't changed since).
- **Not tested, only reasoned:** a macro assigned to Unison Place ([5.1](#51-the-host-hook)); MIDI
  in to a DCO6 part with Unison (it reaches the same `handleNoteOn`); two DCO6 parts with the same
  LFO rate and shape in Free (they share one LFO; see [6.4](#64-the-lfo)).
- **Shown, not fixed:** Attack's 5 ms default shows as "0.00 s" (the SDK's time formatter has two
  decimals); the user page says so.
- **Analysis:** LFO Free between two DCO6 parts with the same rate and shape (they share one LFO;
  see [6.4](#64-the-lfo)). Not tested, only reasoned.
- **Sources:** the 106 facts beyond Roland's pages: the envelope ranges, the Bass Boost's corner and
  gain, and the 106's LFO being one for all voices.

[roland-icon]: https://www.roland.com/uk/blog/roland-icon-series-juno-106-synthesizer/
[roland-specs]: https://support.roland.com/hc/en-us/articles/201966419-Juno-106-Technical-Specifications
