# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Resample mod's mockups (docs/mods/resample.md): the Sample Recorder's setup screen with the
Trk Audio and Trk Record buttons, for Line In, a mic and USB, and the recorder's instrument list.

The screen is cSampleRecorder's config screen on the Tracker+ (core/interface/sampleRecorder/
gSampleRec.cpp, initDisplayControls, showDefaultScreen, hideRadio; sampleRecorder.h
ConfigScreenColumns): the source list in column 1, radioSpaceCover over columns 2-4 for every source
but Radio, the Level and Gain bars in columns 5 and 6, the Monitor list in column 7 (its cover for
USB), blankSpaceCover over column 8, and the bottom labels with bgLabel 0b11111111. Metronome/Config
is drawn with activeButtonLabelsColors and Record with redButtonLabelsColors' red background.

What the mod adds: the texts of label[RADIO_COL1] and label[RADIO_COL2], which CE leaves empty for
every source but Radio (docs/mt/RESAMPLE_PORT_PLAN.md, hook 10). Trk Audio for the four Line Ins and
the mics, Trk Record for the stereo "Line In" only (decision 9), neither for Radio or the two USBs.

The instrument list is InterfacePopups::showInstrumentsPopup (core/interface/core/interfacePopups.cpp)
over the setup screen: bgLabel at (602, 29), 200 x 450, in stepPopupBgLabelColor; the title label
"  Instruments" at (601, 0), 200 x 26, black on white, font 4; the cList at (600, 30), 200 x 390, 14
rows of 27 px from y 34 with the names at x 613, the white frame on the row cList::setData puts it, and
the scroll bar of a list longer than its rows (core/libs/NewDisplay/list.cpp). listInstruments names
the rows: the synths as Synth::getSynthName, the samples "%d. " and the file name, the MIDI
instruments "%d. MIDI Channel %d". Trk Audio off shows CE's stepPopupOnlyMidiInstr; with it on the
mod opens stepPopupInstr, every instrument (hook 9).

    python tools/docs/mockups_resample.py
"""
from __future__ import annotations

from mockups_synths import write
from svgscreen import BLACK, COL, GREY_0A, GREY_57, GREY_77, RED, WHITE, Screen

SOURCES = ["Line In (L)", "Line In (R)", "Line In (M)", "Line In", "Radio", "Mic LG", "Mic HG",
           "USB (M)", "USB"]                                 # ptrSourcesNamesLabels, Tracker+
MONITOR = ["On", "Off"]                                      # monitorNamesLabels
LINE_IN_L, LINE_IN, MIC_LG, USB = 0, 3, 5, 8   # cSampleRecorder::sourceType
USB_MIX = 7
ON, OFF = 0, 1
TRK_AUDIO_COL, TRK_RECORD_COL = 1, 2                         # RADIO_COL1, RADIO_COL2
LEVEL_COL, GAIN_COL, MONITOR_COL = 4, 5, 6


def on_off(on: bool) -> str:
    return "On" if on else "Off"


def setup(title: str, source: int, monitor: int, trk_audio: bool | None, trk_record: bool | None,
          selected: int, level: int, gain: int) -> Screen:
    """trk_audio / trk_record: None where the source has no such button"""
    s = Screen(title)
    s.title_bar("Sample Recorder")

    # the columns above the bottom bar
    s.new_list(2, 29, COL - 3, 394, SOURCES, selected=source)          # sourceListControl
    s.cover(COL * TRK_AUDIO_COL + 2, 29, COL * 3 - 3, 394)              # radioSpaceCover, hideRadio
    s.bar(COL * LEVEL_COL + 2, 29, COL - 3, 394, level)                 # levelBarControl
    s.bar(COL * GAIN_COL + 2, 29, COL - 3, 394, gain)                   # gainBarControl
    if source >= USB_MIX:                                               # isMonitorListVisible
        s.cover(COL * MONITOR_COL + 2, 29, COL, 394)                    # monitorListSpaceCover
    else:
        s.new_list(COL * MONITOR_COL + 2, 29, COL - 3, 394, MONITOR, selected=monitor)
    s.cover(COL * 7 + 2, 29, COL - 3, 394)                              # blankSpaceCover
    # frameControl: CE never puts it on columns 2-4 for these sources (showDefaultScreen sends it back
    # to Source); the buttons are pressed with the key under their label
    s.frame(COL * selected + 1, 31, COL - 3, 391)

    # the bottom labels
    s.bottom_bar(0b11111111)
    s.button(0, "Source", SOURCES[source], 1, color2=WHITE, font2=2)
    if trk_audio is not None:
        s.button(TRK_AUDIO_COL, "Trk Audio", on_off(trk_audio), 1, color2=WHITE, font2=2)
    if trk_record is not None:
        s.button(TRK_RECORD_COL, "Trk Record", on_off(trk_record), 1, color2=WHITE, font2=2)
    s.button(3, "Metronome", "Config", 1, color=GREY_77, color2=GREY_77, font2=3)
    s.button(LEVEL_COL, "Level")
    s.button(GAIN_COL, "Gain", str(gain), 1, color2=WHITE, font2=2, kind2="val")
    if source < USB_MIX:
        s.button(MONITOR_COL, "Monitor", MONITOR[monitor], 1, color2=WHITE, font2=2)
    s.rect(COL * 7 + 3, 425, COL - 6, 54, RED)                          # Record: its background
    s.button(7, "Record", color=GREY_77)

    # marked: the labels the mod adds
    shown = [c for c, v in ((TRK_AUDIO_COL, trk_audio), (TRK_RECORD_COL, trk_record)) if v is not None]
    if shown:
        s.mark(COL * shown[0] + 3, 427, COL * len(shown) - 6, 50)
    return s


SYNTH_NAMES = ["Synth 1 (WAVE)", "Synth 2 (PHASE)", "Synth 3 (Off)"]   # getSynthName: demo engines
SAMPLE_NAMES = ["kick.wav", "snare.wav", "hat closed.wav", "hat open.wav", "clap.wav", "bass C1.wav",
                "pad Am.wav", "vox chop.wav", "perc 1.wav", "perc 2.wav", "fx riser.wav"]  # a project's
MIDI_ROWS = [f"{48 + i + 3}. MIDI Channel {i + 1}" for i in range(16)]
LINES, ROW_H = 14, 27


def instrument_list(s: Screen, rows: list[tuple[str, str]], length: int, start: int) -> None:
    """showInstrumentsPopup over the screen. rows: (text, class) from the list's first row."""
    s.rect(602, 29, 200, 450, GREY_0A)                                  # bgLabel
    s.rect(601, 0, 200, 26, WHITE)                                      # textLabel1
    s.text(601, 13, "  Instruments", 4, BLACK)
    # cList::setData: the frame's row and the first row shown
    if start <= 1:
        bar, first = start, 0
    elif start >= length - 2:
        bar, first = LINES - (length - start), length - LINES
    else:
        bar, first = min(start, LINES - 2), max(0, start - (LINES - 2))
    top = 30 + 4
    s.outline(603, top + bar * ROW_H, 200 - 13, ROW_H, WHITE, 1.5)
    for i, (name, kind) in enumerate(rows[first:first + LINES]):
        s.text(613, top + i * ROW_H + ROW_H / 2, name, 1, WHITE, kind=kind)
    # the scroll bar
    y_length = (390 - 14) * LINES // length
    y = top + 6 + (start * ((390 - 14) - y_length)) // length
    s.rect(600 + 194, top, 3, 390 - 10 - 4, BLACK)
    s.rect(600 + 195, y, 2, y_length, GREY_57)


def instruments_screen(title: str, trk_audio: bool) -> Screen:
    s = setup(title, LINE_IN, OFF, trk_audio, False, 0, 0, 50)
    if trk_audio:
        rows = ([(n, "val") for n in SYNTH_NAMES] +
                [(f"{i + 1}. {SAMPLE_NAMES[i]}" if i < len(SAMPLE_NAMES) else f"{i + 1}. ", "demo")
                 for i in range(48)] +
                [(n, "val") for n in MIDI_ROWS])
        instrument_list(s, rows, len(rows), 3)                          # sample 1 picked
        s.mark(603, 31, 194, 444)
    else:
        instrument_list(s, [(n, "val") for n in MIDI_ROWS], 16, 0)
    return s


def main() -> None:
    write("resample-linein-off.svg",
          setup("Resample: Line In, both Off", LINE_IN, OFF, False, False, 0, 0, 50),
          "Sample Recorder, Line In: Trk Audio Off and Trk Record Off, the recorder as CE's")
    write("resample-linein-trkaudio.svg",
          setup("Resample: Line In, Trk Audio On", LINE_IN, OFF, True, False, 0, 0, 50),
          "Sample Recorder, Line In: Trk Audio On, so the tracker is heard; Trk Record Off")
    write("resample-linein-both.svg",
          setup("Resample: Line In, both On", LINE_IN, OFF, True, True, 0, 35, 50),
          "Sample Recorder, Line In: Trk Audio and Trk Record On, Monitor Off; the take holds the "
          "Line In and the tracker")
    write("resample-linein-mono.svg",
          setup("Resample: a mono Line In", LINE_IN_L, OFF, True, None, 0, 0, 50),
          "Sample Recorder, Line In (L): Trk Audio On; no Trk Record for a mono Line In")
    write("resample-mic.svg",
          setup("Resample: a mic", MIC_LG, OFF, True, None, 0, 0, 50),
          "Sample Recorder, Mic LG: Trk Audio On; no Trk Record for a mic")
    write("resample-usb.svg",
          setup("Resample: USB", USB, OFF, None, None, 0, 0, 50),
          "Sample Recorder, USB: no Resample buttons, the screen as CE's")
    write("resample-instruments-midi.svg",
          instruments_screen("Resample: the instrument list, Trk Audio Off", False),
          "Sample Recorder, Line In, Trk Audio Off: Instr shows CE's list of the 16 MIDI instruments")
    write("resample-instruments-all.svg",
          instruments_screen("Resample: the instrument list, Trk Audio On", True),
          "Sample Recorder, Line In, Trk Audio On: Instr shows every instrument, the synths, the "
          "samples and MIDI; sample 1 picked")

if __name__ == "__main__":
    main()
