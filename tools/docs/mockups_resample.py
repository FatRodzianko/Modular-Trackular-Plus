# Modular-Trackular mods. SPDX-License-Identifier: MIT
"""The Resample mod's mockups (docs/mods/resample.md): the Sample Recorder's setup screen with the
Trk Audio and Trk Record buttons, for Line In, a mic and USB.

The screen is cSampleRecorder's config screen on the Tracker+ (core/interface/sampleRecorder/
gSampleRec.cpp, initDisplayControls, showDefaultScreen, hideRadio; sampleRecorder.h
ConfigScreenColumns): the source list in column 1, radioSpaceCover over columns 2-4 for every source
but Radio, the Level and Gain bars in columns 5 and 6, the Monitor list in column 7 (its cover for
USB), blankSpaceCover over column 8, and the bottom labels with bgLabel 0b11111111. Metronome/Config
is drawn with activeButtonLabelsColors and Record with redButtonLabelsColors' red background.

What the mod adds: the texts of label[RADIO_COL1] and label[RADIO_COL2], which CE leaves empty for
every source but Radio (docs/mt/RESAMPLE_PORT_PLAN.md, hook 10). Trk Audio for the four Line Ins and
the mics, Trk Record for the stereo "Line In" only (decision 9), neither for Radio or the two USBs.

    python tools/docs/mockups_resample.py
"""
from __future__ import annotations

from mockups_synths import write
from svgscreen import COL, GREY_77, RED, WHITE, Screen

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

if __name__ == "__main__":
    main()
