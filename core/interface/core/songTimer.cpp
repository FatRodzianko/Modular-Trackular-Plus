

#include "songTimer.h"

#include "display.h"

#include "mtSequencer.h"
#include "patternEditor/patternEditor.h"
#include "songloop.h" // MT:

cSongTimer songTimer;

static elapsedMillis refreshTimer;
elapsedMillisAudio songTimeCounter;

void cSongTimer::initControls()
{
    strControlProperties prop1;
    prop1.style = controlStyleFont4 | controlStyleCenterY;
    prop1.x = 280;
    prop1.y = 0;
    prop1.h = 26;
    prop1.colors = display.getTheme().songTimerLabelColors; // textLabelColors
    timerLabel = display.createControl<cLabel>(&prop1);
}

void cSongTimer::show()
{
    refresh();

    display.setControlShow(timerLabel);
    display.refreshControl(timerLabel);

    visible = true;
}

void cSongTimer::hide()
{
    display.setControlHide(timerLabel);
    display.refreshControl(timerLabel);

    visible = false;
}

AUDIO_CORE_RAM void cSongTimer::start()
{
    songTimeCounter = 0;
    counting = true;
}

void cSongTimer::stop()
{
    actualData.minutes = 0;
    actualData.seconds = 0;
    counting = false;
}

void cSongTimer::refresh()
{
    if (refreshTimer < 100 || !visible)
        return;
    refreshTimer = 0;

    if (counting)
    {
        actualData.minutes = songTimeCounter / 60000;
        actualData.seconds = (songTimeCounter - (actualData.minutes * 60000)) / 1000;
    }

    actualData.pattern = mtProject.values.actualPattern;
    actualData.seqState = sequencer.getSeqState();

    if (sequencer.isPlay())
        actualData.step = sequencer.getActualPos();
    else
        actualData.step = patternEditor.trackerPattern.actualStep;

#if MT_HAS_SONGLOOP // MT: Song Loop: its icon changed
    if (mods::on(mods::Mod::songloop) && mods::songloop::takeStatusDirty())
    {
        lastData.minutes = -1;
    }
#endif

    // przerwanie operacji jesli dane do wyswietlenia nie ulegly zmianie
    if (lastData.step == actualData.step && lastData.seconds == actualData.seconds &&
        lastData.minutes == actualData.minutes && lastData.pattern == actualData.pattern &&
        lastData.seqState == actualData.seqState)
        return;

    lastData.step = actualData.step;
    lastData.seconds = actualData.seconds;
    lastData.minutes = actualData.minutes;
    lastData.pattern = actualData.pattern;
    lastData.seqState = actualData.seqState;

    // NOTE: seqState icons only available in fonts[3] (Bold 17)
    const char* seqState = "\x18\x19";
    const char* perfState = "";
    if (actualData.seqState == Sequencer::SEQ_STATE_PLAY_PERFORMANCE)
    {
        perfState = "\x1E\x1F";
        seqState = sequencer.isSongMode() ? "\x1A\x1B" : "\x1C\x1D";
    }
    else if (actualData.seqState == Sequencer::SEQ_STATE_PLAY_SONG)
    {
        seqState = "\x1A\x1B";
    }
    else if (actualData.seqState == Sequencer::SEQ_STATE_PLAY_PATTERN)
    {
        seqState = "\x1C\x1D";
    }
    else if (actualData.seqState == Sequencer::SEQ_STATE_PLAY_SELECTION)
    {
        seqState = "\x1A\x1B";
    }
    else if (sequencer.isPlay())
    {
        seqState = "  ";
    }

#if MT_HAS_SONGLOOP // MT: Song Loop: the repeat signs in place of the play icon
    if (mods::on(mods::Mod::songloop) && mods::songloop::glyphsReady())
    {
        const Sequencer::strPlayer* player = sequencer.ptrPlayer;
        seqState = mods::songloop::statusIcon(
          seqState,
          !player->isStop && player->isPlay && !player->selectionMode && player->songMode,
          mtProject.song.playlistPos);
    }
#endif

    const char* timerFormat = "%3im:%02is %2s P%i.%03i%2s";

    int stepNum = actualData.step;
    if (mtConfig.general.numberingMode == NumberingMode::kHexadecimal)
    {
        timerFormat = "%3im:%02is %2s P%02X.%02X%2s";
    }
    else if (mtConfig.general.numberingMode == NumberingMode::kDecimalWithStepsFrom1)
    {
        stepNum += 1;
    }

    sprintf(timerText,
            timerFormat,
            actualData.minutes,
            actualData.seconds,
            seqState,
            mtProject.values.actualPattern,
            stepNum,
            perfState);

    display.setControlText(timerLabel, timerText);
    display.refreshControl(timerLabel);
}

void cSongTimer::refreshColors()
{
    display.setControlColors(timerLabel, display.getTheme().songTimerLabelColors);
}
